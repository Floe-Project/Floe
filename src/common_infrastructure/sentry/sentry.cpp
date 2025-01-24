// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sentry.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

namespace sentry {

static Optional<fmt::UuidArray> DeviceId(Atomic<u64>& seed) {
    PathArena path_arena {PageAllocator::Instance()};
    auto const path = KnownDirectoryWithSubdirectories(path_arena,
                                                       KnownDirectoryType::UserData,
                                                       Array {"Floe"_s},
                                                       "device_id",
                                                       {.create = true});

    auto file = TRY_OR(OpenFile(path, FileMode::ReadWrite), {
        g_log.Error(k_log_module, "Failed to create device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    TRY_OR(file.Lock(FileLockType::Exclusive), {
        g_log.Error(k_log_module, "Failed to lock device_id file: {}, {}", path, error);
        return k_nullopt;
    });
    DEFER { auto _ = file.Unlock(); };

    auto const size = TRY_OR(file.FileSize(), {
        g_log.Error(k_log_module, "Failed to get size of device_id file: {}, {}", path, error);
        return k_nullopt;
    });

    if (size == fmt::k_uuid_size) {
        fmt::UuidArray uuid {};
        TRY_OR(file.Read(uuid.data, uuid.size), {
            g_log.Error(k_log_module, "Failed to read device_id file: {}, {}", path, error);
            return k_nullopt;
        });

        bool valid = true;
        for (auto c : uuid) {
            if (!IsHexDigit(c)) {
                valid = false;
                break;
            }
        }
        if (valid) return uuid;
    }

    // File is invalid or empty, let's recreate it
    auto const uuid = detail::Uuid(seed);

    TRY_OR(file.Seek(0, File::SeekOrigin::Start),
           { g_log.Error(k_log_module, "Failed to seek device_id file: {}, {}", path, error); });

    TRY_OR(file.Truncate(0),
           { g_log.Error(k_log_module, "Failed to truncate device_id file: {}, {}", path, error); });

    TRY_OR(file.Write(uuid),
           { g_log.Error(k_log_module, "Failed to write device_id file: {}, {}", path, error); });

    auto _ = file.Flush();

    return uuid;
}

static void CheckDsn(DsnInfo dsn) {
    ASSERT(dsn.dsn.size);
    ASSERT(dsn.host.size);
    ASSERT(dsn.project_id.size);
    ASSERT(dsn.public_key.size);
}

static void CheckTags(Span<Tag const> tags) {
    ASSERT(tags.size < 20);
    for (auto const& tag : tags) {
        ASSERT(tag.key.size);
        ASSERT(tag.value.size);
        ASSERT(tag.key.size < 200);
        ASSERT(tag.value.size < 200);
    }
}

// not thread-safe, dsn must be static, tags are cloned
static void InitSentry(Sentry& sentry, DsnInfo dsn, Span<Tag const> tags) {
    CheckDsn(dsn);
    CheckTags(tags);

    sentry.dsn = dsn;
    sentry.seed.Store(RandomSeed(), StoreMemoryOrder::Relaxed);
    sentry.session_id = detail::Uuid(sentry.seed);
    sentry.device_id = DeviceId(sentry.seed);

    // clone the tags
    ASSERT(sentry.tags.size == 0);
    sentry.tags = sentry.arena.Clone(tags, CloneType::Deep);

    // this data is common to every event we want to send, so let's cache it as a JSON blob
    {
        DynamicArray<char> event_context_json {sentry.arena};
        event_context_json.Reserve(1024);
        json::WriteContext json_writer {
            .out = dyn::WriterFor(event_context_json),
            .add_whitespace = false,
        };

        auto _ = json::WriteObjectBegin(json_writer);

        // user
        if (sentry.device_id) {
            auto _ = json::WriteKeyObjectBegin(json_writer, "user");
            auto _ = json::WriteKeyValue(json_writer, "id", *sentry.device_id);
            auto _ = json::WriteObjectEnd(json_writer);
        }

        // contexts
        {
            auto _ = json::WriteKeyObjectBegin(json_writer, "contexts");

            // device
            {
                auto const system = CachedSystemStats();
                auto _ = json::WriteKeyObjectBegin(json_writer, "device");
                auto _ = json::WriteKeyValue(json_writer, "name", "desktop");
                auto _ = json::WriteKeyValue(json_writer, "arch", system.Arch());
                auto _ = json::WriteKeyValue(json_writer, "cpu_description", system.cpu_name);
                auto _ = json::WriteKeyValue(json_writer, "processor_count", system.num_logical_cpus);
                auto _ = json::WriteKeyValue(json_writer, "processor_frequency", system.frequency_mhz);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            // os
            {
                auto const os = GetOsInfo();
                auto _ = json::WriteKeyObjectBegin(json_writer, "os");
                auto _ = json::WriteKeyValue(json_writer, "name", os.name);
                if (os.version.size) auto _ = json::WriteKeyValue(json_writer, "version", os.version);
                if (os.build.size) auto _ = json::WriteKeyValue(json_writer, "build", os.build);
                if (os.kernel_version.size)
                    auto _ = json::WriteKeyValue(json_writer, "kernel_version", os.kernel_version);
                if (os.pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "pretty_name", os.pretty_name);
                if (os.distribution_name.size)
                    auto _ = json::WriteKeyValue(json_writer, "distribution_name", os.distribution_name);
                if (os.distribution_version.size)
                    auto _ =
                        json::WriteKeyValue(json_writer, "distribution_version", os.distribution_version);
                if (os.distribution_pretty_name.size)
                    auto _ = json::WriteKeyValue(json_writer,
                                                 "distribution_pretty_name",
                                                 os.distribution_pretty_name);
                auto _ = json::WriteObjectEnd(json_writer);
            }

            auto _ = json::WriteObjectEnd(json_writer);
        }
        auto _ = json::WriteObjectEnd(json_writer);

        // we want to be able to print the context directly into an existing JSON event so we remove the
        // object braces
        dyn::Pop(event_context_json);
        dyn::Remove(event_context_json, 0);

        sentry.event_context_json = event_context_json.ToOwnedSpan();
    }
}

alignas(Sentry) u8 g_sentry_storage[sizeof(Sentry)];
Atomic<Sentry*> g_instance {nullptr};

Sentry* InitGlobalSentry(DsnInfo dsn, Span<Tag const> tags) {
    auto existing = g_instance.Load(LoadMemoryOrder::Acquire);
    if (existing) return existing;

    auto sentry = PLACEMENT_NEW(g_sentry_storage) Sentry {};
    InitSentry(*sentry, dsn, tags);

    g_instance.Store(sentry, StoreMemoryOrder::Release);
    return sentry;
}

// signal-safe
void InitBarebonesSentry(Sentry& sentry) {
    sentry.seed.Store((u64)MicrosecondsSinceEpoch() + __builtin_readcyclecounter(),
                      StoreMemoryOrder::Relaxed);
    sentry.session_id = detail::Uuid(sentry.seed);

    // this data is common to every event we want to send, so let's cache it as a JSON blob
    {
        DynamicArray<char> event_context_json {sentry.arena};
        event_context_json.Reserve(1024);
        json::WriteContext json_writer {
            .out = dyn::WriterFor(event_context_json),
            .add_whitespace = false,
        };

        auto _ = json::WriteObjectBegin(json_writer);

        // contexts
        {
            auto _ = json::WriteKeyObjectBegin(json_writer, "contexts");

            // device
            {
                auto _ = json::WriteKeyObjectBegin(json_writer, "device");
                auto _ = json::WriteKeyValue(json_writer, "name", "desktop");
                auto _ = json::WriteKeyValue(json_writer, "arch", ({
                                                 String s;
                                                 switch (k_arch) {
                                                     case Arch::X86_64: s = "x86_64"; break;
                                                     case Arch::Aarch64: s = "aarch64"; break;
                                                 }
                                                 s;
                                             }));
                auto _ = json::WriteObjectEnd(json_writer);
            }

            // os
            {
                auto _ = json::WriteKeyObjectBegin(json_writer, "os");
                auto _ = json::WriteKeyValue(json_writer, "name", ({
                                                 String s {};
                                                 if (IS_WINDOWS) s = "Windows";
                                                 if (IS_LINUX) s = "Linux";
                                                 if (IS_MACOS) s = "macOS";
                                                 s;
                                             }));
                auto _ = json::WriteObjectEnd(json_writer);
            }

            auto _ = json::WriteObjectEnd(json_writer);
        }
        auto _ = json::WriteObjectEnd(json_writer);

        // we want to be able to print the context directly into an existing JSON event so we remove the
        // object braces
        dyn::Pop(event_context_json);
        dyn::Remove(event_context_json, 0);

        sentry.event_context_json = event_context_json.ToOwnedSpan();
    }
}

TEST_CASE(TestSentry) {
    SUBCASE("Parse DSN") {
        auto o = ParseDsn("https://publickey@host.com/123");
        REQUIRE(o.HasValue());
        CHECK_EQ(o->dsn, "https://publickey@host.com/123"_s);
        CHECK_EQ(o->host, "host.com"_s);
        CHECK_EQ(o->project_id, "123"_s);
        CHECK_EQ(o->public_key, "publickey"_s);

        CHECK(!ParseDsn("https://host.com/123"));
        CHECK(!ParseDsn("https://publickey@host.com"));
        CHECK(!ParseDsn("  "));
        CHECK(!ParseDsn(""));
    }

    SUBCASE("Basics") {
        auto sentry = InitGlobalSentry(ParseDsnOrThrow("https://publickey@host.com/123"), {});
        REQUIRE(sentry);
        CHECK(sentry->device_id);

        // build an envelope
        DynamicArray<char> envelope {tester.scratch_arena};
        auto writer = dyn::WriterFor(envelope);
        TRY(EnvelopeAddHeader(*sentry, writer));
        TRY(EnvelopeAddSessionUpdate(*sentry, writer, SessionStatus::Ok));
        TRY(EnvelopeAddEvent(*sentry,
                             writer,
                             {
                                 .level = ErrorEvent::Level::Info,
                                 .message = "Test event message"_s,
                                 .tags = ArrayT<Tag>({
                                     {"tag1", "value1"},
                                     {"tag2", "value2"},
                                 }),
                             },
                             false));
        TRY(EnvelopeAddSessionUpdate(*sentry, writer, SessionStatus::EndedNormally));

        CHECK(envelope.size > 0);
    }

    return k_success;
}

} // namespace sentry

TEST_REGISTRATION(RegisterSentryTests) { REGISTER_TEST(sentry::TestSentry); }
