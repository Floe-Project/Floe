// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if IS_LINUX || IS_MACOS
#include <dlfcn.h>
#endif

#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/reader.hpp"

#include "clap/entry.h"
#include "clap/ext/gui.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/ext/thread-check.h"
#include "clap/factory/plugin-factory.h"
#include "plugin/processing_utils/midi.hpp"

#if IS_LINUX || IS_MACOS
struct TestHost {
    clap_host_params const host_params {
        .rescan =
            [](clap_host_t const* h, clap_param_rescan_flags) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .clear =
            [](clap_host_t const* h, clap_id, clap_param_clear_flags) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .request_flush =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
    };

    clap_host_gui const host_gui {
        .resize_hints_changed =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
            },
        .request_resize =
            [](clap_host_t const* h, uint32_t, uint32_t) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return false;
            },

        .request_show = [](clap_host_t const*) { return false; },
        .request_hide = [](clap_host_t const*) { return false; },
        .closed = [](clap_host_t const*, bool) { Panic("floating windows are not supported"); },
    };

    clap_host_thread_check const host_thread_check {
        .is_main_thread =
            [](clap_host const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return CurrentThreadId() == test_host.main_thread_id;
            },
        .is_audio_thread =
            [](clap_host const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                return CurrentThreadId() == test_host.audio_thread_id.Load(LoadMemoryOrder::Relaxed);
            },
    };

    clap_host_t const host {
        .clap_version = CLAP_VERSION,
        .host_data = this,
        .name = "Test Host",
        .vendor = "Tester",
        .url = "https://example.com",
        .version = "1",

        .get_extension = [](clap_host_t const* ch, char const* extension_id) -> void const* {
            auto& test_host = *(TestHost*)ch->host_data;
            ASSERT(test_host.plugin_created);

            if (NullTermStringsEqual(extension_id, CLAP_EXT_PARAMS))
                return &test_host.host_params;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_GUI))
                return &test_host.host_gui;
            else if (NullTermStringsEqual(extension_id, CLAP_EXT_THREAD_CHECK))
                return &test_host.host_thread_check;

            return nullptr;
        },
        .request_restart = [](clap_host_t const*) { PanicIfReached(); },
        .request_process =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                // Don't think we need to do anything here because we always call process() regardless
            },
        .request_callback =
            [](clap_host_t const* h) {
                auto& test_host = *(TestHost*)h->host_data;
                ASSERT(test_host.plugin_created);
                test_host.callback_requested.Store(true, StoreMemoryOrder::Relaxed);
            },
    };

    Atomic<u64> audio_thread_id {};
    u64 main_thread_id {CurrentThreadId()};
    Atomic<bool> callback_requested {false};
    bool plugin_created = false;
};

static String TestPresetPath(tests::Tester& tester, String filename) {
    return path::Join(tester.scratch_arena,
                      Array {TestFilesFolder(tester), tests::k_preset_test_files_subdir, filename});
}
#endif

TEST_CASE(TestHostingClap) {
#if IS_LINUX || IS_MACOS
    struct Fixture {
        [[maybe_unused]] Fixture(tests::Tester&) {}
        [[maybe_unused]] ~Fixture() {
            if (handle) dlclose(handle);
        }
        DynamicArrayBounded<char, path::k_max> clap_path {};
        bool initialised = false;
        void* handle = nullptr;
    };

    auto& fixture = CreateOrFetchFixtureObject<Fixture>(tester);

    if (!fixture.initialised) {
        fixture.initialised = true;
        auto const exe_path = TRY(CurrentExecutablePath(tester.scratch_arena));
        auto p = exe_path;

        for (auto _ : Range(6)) {
            auto dir = path::Directory(p);
            if (!dir) break;

            dyn::Assign(fixture.clap_path, *dir);
            path::JoinAppend(fixture.clap_path, "Floe.clap"_s);
            if (auto const o = GetFileType(fixture.clap_path); o.HasValue() && o.Value() == FileType::File)
                break;
            else
                dyn::Clear(fixture.clap_path);
        }

        if (!fixture.clap_path.size) {
            LOG_WARNING("Failed to find Floe.clap");
            return k_success;
        }

        fixture.handle =
            dlopen(NullTerminated(fixture.clap_path, tester.scratch_arena), RTLD_LOCAL | RTLD_NOW);
        if (!fixture.handle)
            TEST_FAILED("Failed to load clap: {}", dlerror()); // NOLINT(concurrency-mt-unsafe)
    }

    auto const entry = (clap_plugin_entry const*)dlsym(fixture.handle, "clap_entry");
    REQUIRE(entry != nullptr);

    CHECK(entry->init("plugin-path"));
    DEFER { entry->deinit(); };

    SUBCASE("double init") { CHECK(entry->init("plugin-path")); }

    SUBCASE("double deinit") { entry->deinit(); }

    SUBCASE("plugin") {
        TestHost test_host {};

        auto factory = (clap_plugin_factory const*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        CHECK_EQ(factory->get_plugin_count(factory), 1u);
        auto const plugin_id = factory->get_plugin_descriptor(factory, 0)->id;

        auto const plugin = factory->create_plugin(factory, &test_host.host, plugin_id);
        REQUIRE(plugin);
        test_host.plugin_created = true;

        DEFER { plugin->destroy(plugin); };

        SUBCASE("no init") {}

        SUBCASE("init") {
            REQUIRE(plugin->init(plugin));
            REQUIRE(plugin->activate(plugin, 44100, 1, 1024));
            DEFER { plugin->deactivate(plugin); };

            {
                auto const preset_data =
                    TRY(ReadEntireFile(TestPresetPath(tester, "sine.floe-preset"), tester.scratch_arena));
                auto const state = (clap_plugin_state const*)plugin->get_extension(plugin, CLAP_EXT_STATE);
                REQUIRE(state);

                auto reader = Reader::FromMemory(preset_data.ToByteSpan());

                clap_istream const stream {
                    .ctx = (void*)&reader,

                    .read = [](const struct clap_istream* stream, void* buffer, uint64_t size) -> s64 {
                        auto& reader = *(Reader*)stream->ctx;
                        auto const read = reader.Read(Span<u8>((u8*)buffer, size));
                        if (read.HasError()) return -1;
                        return CheckedCast<s64>(read.Value());
                    },
                };
                state->load(plugin, &stream);
            }

            Thread audio_thread {};
            audio_thread.Start(
                [&] {
                    test_host.audio_thread_id.Store(CurrentThreadId(), StoreMemoryOrder::Relaxed);
                    REQUIRE(plugin->start_processing(plugin));
                    DEFER { plugin->stop_processing(plugin); };

                    clap_input_events const in_events {
                        .ctx = nullptr,
                        .size = [](clap_input_events const*) -> u32 { return 1; },
                        .get = [](clap_input_events const*, uint32_t index) -> clap_event_header_t const* {
                            ASSERT(index == 0);
                            auto msg = MidiMessage::NoteOn(60, 80);
                            static clap_event_midi note_on {
                                .header =
                                    {
                                        .size = sizeof(clap_event_midi),
                                        .time = 0,
                                        .type = CLAP_EVENT_MIDI,
                                        .flags = CLAP_EVENT_IS_LIVE,
                                    },
                                .port_index = 0,
                                .data = {msg.status, msg.data1, msg.data2},
                            };
                            return &note_on.header;
                        },
                    };

                    clap_output_events const out_events {
                        .ctx = nullptr,
                        .try_push = [](clap_output_events const*, clap_event_header const*) -> bool {
                            return false;
                        },
                    };

                    f32 data_l[100] {};
                    f32 data_r[100] {};
                    Array data {data_l, data_r};

                    clap_audio_buffer out {
                        .data32 = data.data,
                        .channel_count = 2,
                        .latency = 0,
                        .constant_mask = 0,
                    };

                    clap_process process {
                        .steady_time = -1,
                        .frames_count = 3,
                        .transport = nullptr,
                        .audio_inputs = nullptr,
                        .audio_outputs = &out,
                        .audio_inputs_count = 0,
                        .audio_outputs_count = 1,
                        .in_events = &in_events,
                        .out_events = &out_events,
                    };

                    auto const status = plugin->process(plugin, &process);
                    CHECK(status != CLAP_PROCESS_ERROR);
                },
                "audio");
            audio_thread.Join();
        }
    }

#endif

    return k_success;
}

TEST_REGISTRATION(RegisterHostingTests) { REGISTER_TEST(TestHostingClap); }
