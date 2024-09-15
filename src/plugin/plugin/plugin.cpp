// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clap/plugin.h"

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "clap/ext/audio-ports.h"
#include "clap/ext/note-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/posix-fd-support.h"
#include "clap/ext/state.h"
#include "clap/ext/timer-support.h"
#include "clap/host.h"
#include "clap/id.h"
#include "clap/process.h"
#include "descriptors/param_descriptors.hpp"
#include "engine/engine.hpp"
#include "engine/shared_engine_systems.hpp"
#include "gui_framework/gui_platform.hpp"
#include "plugin.hpp"
#include "processing_utils/scoped_denormals.hpp"
#include "processor/processor.hpp"
#include "settings/settings_file.hpp"

//
#include "os/undef_windows_macros.h"

[[clang::no_destroy]] Optional<SharedEngineSystems> g_shared_engine_systems {};

static u16 g_floe_instance_id_counter = 0;

struct FloePluginInstance {
    FloePluginInstance(clap_host const* clap_host);
    ~FloePluginInstance() { g_log.Trace(k_main_log_module); }

    clap_host const& host;
    clap_plugin clap_plugin;

    bool initialised {false};
    bool active {false};
    bool processing {false};

    u16 id = g_floe_instance_id_counter++;

    TracyMessageConfig trace_config {
        .category = "clap",
        .colour = 0xa88e39,
        .object_id = id,
    };

    ArenaAllocator arena {PageAllocator::Instance()};

    Optional<Engine> engine {};

    Optional<GuiPlatform> gui_platform {};
};

static u16 g_num_init_plugins = 0;

clap_plugin_state const floe_plugin_state {
    .save = [](clap_plugin const* plugin, clap_ostream const* stream) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "state save");
        ASSERT(IsMainThread(floe.host));

        if (!EngineCallbacks().save_state(*floe.engine, *stream)) return false;
        return true;
    },

    .load = [](clap_plugin const* plugin, clap_istream const* stream) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "state load");
        ASSERT(IsMainThread(floe.host));

        if (!EngineCallbacks().load_state(*floe.engine, *stream)) return false;
        return true;
    },
};

static bool LogIfError(ErrorCodeOr<void> const& ec, String name) {
    if (ec.HasError()) {
        g_log.Error(k_main_log_module, "{}: {}", name, ec.Error());
        return false;
    }
    return true;
}

constexpr u32 k_largest_gui_size = LargestRepresentableValue<u16>();

// Size (width, height) is in pixels; the corresponding windowing system extension is
// responsible for defining if it is physical pixels or logical pixels.
// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init)
clap_plugin_gui const floe_gui {
    .is_api_supported = [](clap_plugin_t const* plugin, char const* api, bool is_floating) -> bool {
        (void)is_floating;
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ASSERT(IsMainThread(floe.host));
        return NullTermStringsEqual(k_supported_gui_api, api);
    },

    .get_preferred_api = [](clap_plugin_t const* plugin, char const** api, bool* is_floating) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ASSERT(IsMainThread(floe.host));
        if (is_floating) *is_floating = false;
        if (api) *api = k_supported_gui_api;
        return true;
    },

    .create = [](clap_plugin_t const* plugin, char const* api, bool is_floating) -> bool {
        ASSERT(NullTermStringsEqual(api, k_supported_gui_api));
        ASSERT(!is_floating); // not supported at the moment
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui create");
        ASSERT(IsMainThread(floe.host));

        floe.gui_platform.Emplace(floe.host, g_shared_engine_systems->settings);

        auto const result = LogIfError(CreateView(*floe.gui_platform, *floe.engine), "CreateView");

        return result;
    },

    .destroy =
        [](clap_plugin_t const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ASSERT(IsMainThread(floe.host));
            ZoneScopedMessage(floe.trace_config, "gui destroy");
            DestroyView(*floe.gui_platform);
            floe.gui_platform.Clear();
        },

    .set_scale = [](clap_plugin_t const* plugin, f64 scale) -> bool {
        (void)plugin;
        g_log.Debug(k_clap_log_module, "set_scale {}", scale);
        return false; // we (pugl) negotiate this with the OS ourselves
    },

    .get_size = [](clap_plugin_t const* plugin, u32* width, u32* height) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ASSERT(IsMainThread(floe.host));
        auto size = WindowSize(*floe.gui_platform);
        *width = size.width;
        *height = size.height;
        g_log.Debug(k_clap_log_module, "get_size {} {}", *width, *height);
        return true;
    },

    .can_resize = [](clap_plugin_t const*) -> bool { return true; },

    .get_resize_hints = [](clap_plugin_t const*, clap_gui_resize_hints_t* hints) -> bool {
        hints->can_resize_vertically = true;
        hints->can_resize_horizontally = true;
        hints->preserve_aspect_ratio = true;
        auto const ratio = gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui);
        hints->aspect_ratio_width = ratio.width;
        hints->aspect_ratio_height = ratio.height;
        g_log.Debug(k_clap_log_module,
                    "get_resize_hints {}x{}",
                    hints->aspect_ratio_width,
                    hints->aspect_ratio_height);
        return true;
    },

    .adjust_size = [](clap_plugin_t const*, u32* width, u32* height) -> bool {
        *width = Clamp<u32>(*width, 1, k_largest_gui_size);
        *height = Clamp<u32>(*height, 1, k_largest_gui_size);

        auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize(
            {CheckedCast<u16>(*width), CheckedCast<u16>(*height)},
            gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));
        g_log.Debug(k_clap_log_module,
                    "adjust_size in: {}x{}, out: {}x{}",
                    *width,
                    *height,
                    aspect_ratio_conformed_size.width,
                    aspect_ratio_conformed_size.height);
        *width = aspect_ratio_conformed_size.width;
        *height = aspect_ratio_conformed_size.height;
        return true;
    },

    .set_size = [](clap_plugin_t const* plugin, u32 width, u32 height) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui set_size {} {}", width, height);
        ASSERT(IsMainThread(floe.host));

        if (width == 0 || height == 0) return false;
        if (width > k_largest_gui_size || height > k_largest_gui_size) return false;

        auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize(
            {CheckedCast<u16>(width), CheckedCast<u16>(height)},
            gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));
        g_log.Debug(k_clap_log_module,
                    "set_size in: {}x{}, constrained {}x{}, result: {}",
                    width,
                    height,
                    aspect_ratio_conformed_size.width,
                    aspect_ratio_conformed_size.height,
                    aspect_ratio_conformed_size.width == width &&
                        aspect_ratio_conformed_size.height == height);
        if (aspect_ratio_conformed_size.width != width || aspect_ratio_conformed_size.height != height)
            return false;
        return SetSize(*floe.gui_platform, {CheckedCast<u16>(width), CheckedCast<u16>(height)});
    },

    .set_parent = [](clap_plugin_t const* plugin, clap_window_t const* window) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui set_parent");
        ASSERT(IsMainThread(floe.host));
        auto const result = LogIfError(SetParent(*floe.gui_platform, *window), "SetParent");
        // Bitwig never calls show()
        auto _ = SetVisible(*floe.gui_platform, true);
        return result;
    },

    .set_transient = [](clap_plugin_t const* plugin, clap_window_t const* window) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui set_transient");
        ASSERT(IsMainThread(floe.host));
        return LogIfError(SetTransient(*floe.gui_platform, *window), "SetTransient");
    },

    .suggest_title = [](clap_plugin_t const*, char const*) {},

    .show = [](clap_plugin_t const* plugin) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui show");
        ASSERT(IsMainThread(floe.host));
        bool const result = LogIfError(SetVisible(*floe.gui_platform, true), "SetVisible");
        if (result) {
            static bool shown_graphics_info = false;
            if (!shown_graphics_info) {
                shown_graphics_info = true;
                g_log.Info(k_main_log_module,
                           "\n{}",
                           floe.gui_platform->graphics_ctx->graphics_device_info.Items());
            }
        }
        return result;
    },

    .hide = [](clap_plugin_t const* plugin) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "gui hide");
        ASSERT(IsMainThread(floe.host));
        return LogIfError(SetVisible(*floe.gui_platform, false), "SetVisible");
    },
};

clap_plugin_params const floe_params {
    .count = [](clap_plugin_t const*) -> u32 { return (u32)k_num_parameters; },

    .get_info = [](clap_plugin_t const*, u32 param_index, clap_param_info_t* param_info) -> bool {
        auto const& param = k_param_descriptors[param_index];
        param_info->id = ParamIndexToId((ParamIndex)param_index);
        param_info->default_value = (f64)param.default_linear_value;
        param_info->max_value = (f64)param.linear_range.max;
        param_info->min_value = (f64)param.linear_range.min;
        CopyStringIntoBufferWithNullTerm(param_info->name, param.name);
        CopyStringIntoBufferWithNullTerm(param_info->module, param.ModuleString());
        param_info->cookie = nullptr;
        param_info->flags = 0;
        if (!param.flags.not_automatable) param_info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
        if (param.value_type == ParamValueType::Menu || param.value_type == ParamValueType::Bool ||
            param.value_type == ParamValueType::Int)
            param_info->flags |= CLAP_PARAM_IS_STEPPED;

        return true;
    },

    .get_value = [](clap_plugin_t const* plugin, clap_id param_id, f64* out_value) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ASSERT(IsMainThread(floe.host));
        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;
        auto const index = (usize)*opt_index;
        if (floe.engine->pending_state_change)
            *out_value = (f64)floe.engine->last_snapshot.state.param_values[index];
        else
            *out_value = (f64)floe.engine->processor.params[index].value.Load(LoadMemoryOrder::Relaxed);
        return true;
    },

    .value_to_text =
        [](clap_plugin_t const*, clap_id param_id, f64 value, char* out_buffer, u32 out_buffer_capacity)
        -> bool {
        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;
        auto const index = (usize)*opt_index;
        auto const& p = k_param_descriptors[index];
        auto const str = p.LinearValueToString((f32)value);
        if (!str) return false;
        if (out_buffer_capacity < (str->size + 1)) return false;
        CopyMemory(out_buffer, str->data, str->size);
        out_buffer[str->size] = '\0';
        return true;
    },

    .text_to_value =
        [](clap_plugin_t const*, clap_id param_id, char const* param_value_text, f64* out_value) -> bool {
        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;
        auto const index = (usize)*opt_index;
        auto const& p = k_param_descriptors[index];
        if (auto v = p.StringToLinearValue(FromNullTerminated(param_value_text))) {
            *out_value = (f64)*v;
            return true;
        }
        return false;
    },

    // [active ? audio-thread : main-thread]
    .flush =
        [](clap_plugin_t const* plugin, clap_input_events_t const* in, clap_output_events_t const* out) {
            ZoneScopedN("clap_plugin_params flush");
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            if (!floe.active) ASSERT(IsMainThread(floe.host));
            if (!in || !out) return;
            auto& processor = floe.engine->processor;
            processor.processor_callbacks.flush_parameter_events(processor, *in, *out);
        },
};

static constexpr clap_id k_input_port_id = 1;
static constexpr clap_id k_output_port_id = 2;

clap_plugin_audio_ports const floe_audio_ports {
    .count = [](clap_plugin_t const*, bool) -> u32 { return 1; },

    .get = [](clap_plugin_t const*, u32 index, bool is_input, clap_audio_port_info_t* info) -> bool {
        ASSERT(index == 0);
        if (is_input) {
            info->id = k_input_port_id;
            CopyStringIntoBufferWithNullTerm(info->name, "Main In");
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
        } else {
            info->id = k_output_port_id;
            CopyStringIntoBufferWithNullTerm(info->name, "Main Out");
            info->flags = CLAP_AUDIO_PORT_IS_MAIN;
            info->channel_count = 2;
            info->port_type = CLAP_PORT_STEREO;
            info->in_place_pair = CLAP_INVALID_ID;
        }
        return true;
    },
};

static constexpr clap_id k_main_note_port_id = 1; // never change this

// The note ports scan has to be done while the plugin is deactivated.
clap_plugin_note_ports const floe_note_ports {
    .count = [](clap_plugin_t const*, bool is_input) -> u32 { return is_input ? 1 : 0; },

    .get = [](clap_plugin_t const*, u32 index, bool is_input, clap_note_port_info_t* info) -> bool {
        ZoneScopedN("clap_plugin_note_ports get");
        if (index != 0 || !is_input) return false;

        info->id = k_main_note_port_id;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        CopyStringIntoBufferWithNullTerm(info->name, "Notes In"_s);
        return true;
    },
};

clap_plugin_thread_pool const floe_thread_pool {
    .exec =
        [](clap_plugin_t const* plugin, u32 task_index) {
            ZoneScopedN("clap_plugin_thread_pool exec");
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            floe.engine->processor.host_thread_pool->OnThreadPoolExec(task_index);
        },
};

clap_plugin_timer_support const floe_timer {
    .on_timer =
        [](clap_plugin_t const* plugin, clap_id timer_id) {
            ZoneScopedN("clap_plugin_timer_support on_timer");
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ASSERT(IsMainThread(floe.host));
            PollForSettingsFileChanges(g_shared_engine_systems->settings);
            if (floe.gui_platform) OnClapTimer(*floe.gui_platform, timer_id);
        },
};

clap_plugin_posix_fd_support const floe_posix_fd {
    .on_fd =
        [](clap_plugin_t const* plugin, int fd, clap_posix_fd_flags_t) {
            ZoneScopedN("clap_plugin_posix_fd_support on_fd");
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ASSERT(IsMainThread(floe.host));
            if (floe.gui_platform) OnPosixFd(*floe.gui_platform, fd);
        },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init)
clap_plugin const floe_plugin {
    .desc = &k_plugin_info,
    .plugin_data = nullptr,

    .init = [](clap_plugin const* plugin) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ASSERT(!floe.initialised);
        if (floe.initialised) return false;

        ZoneScopedMessage(floe.trace_config, "plugin init");

        if (g_num_init_plugins++ == 0) {
            SetThreadName("main");
            g_shared_engine_systems.Emplace();
        }

        floe.engine.Emplace(floe.host, *g_shared_engine_systems);

        floe.initialised = true;
        return true;
    },

    .destroy =
        [](clap_plugin const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin destroy (init:{})", floe.initialised);

            if (floe.initialised) {
                floe.gui_platform.Clear();

                floe.engine.Clear();

                if (--g_num_init_plugins == 0) g_shared_engine_systems.Clear();
            }

            PageAllocator::Instance().Delete(&floe);
        },

    .activate =
        [](clap_plugin const* plugin, f64 sample_rate, u32 min_frames_count, u32 max_frames_count) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "plugin activate");

        ASSERT(IsMainThread(floe.host));
        ASSERT(!floe.active);
        if (floe.active) return false;
        auto& processor = floe.engine->processor;

        PluginActivateArgs const args {sample_rate, min_frames_count, max_frames_count};
        if (!processor.processor_callbacks.activate(processor, args)) return false;
        floe.active = true;
        return true;
    },

    .deactivate =
        [](clap_plugin const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin activate");

            ASSERT(IsMainThread(floe.host));
            ASSERT(floe.active);
            if (!floe.active) return;
            if (floe.gui_platform) {
                // TODO: I'm not entirely sure if this is ok or not. But I do want to avoid the GUI being
                // active when the audio plugin is deactivate.
                DestroyView(*floe.gui_platform);
            }
            auto& processor = floe.engine->processor;
            processor.processor_callbacks.deactivate(processor);
            floe.active = false;
        },

    .start_processing = [](clap_plugin const* plugin) -> bool {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "plugin start_processing");
        ASSERT(floe.active);
        ASSERT(!floe.processing);
        SetThreadName("audio");
        auto& processor = floe.engine->processor;
        processor.processor_callbacks.start_processing(processor);
        floe.processing = true;
        return true;
    },

    .stop_processing =
        [](clap_plugin const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin stop_processing");
            ASSERT(floe.active);
            ASSERT(floe.processing);
            auto& processor = floe.engine->processor;
            processor.processor_callbacks.stop_processing(processor);
            floe.processing = false;
        },

    .reset =
        [](clap_plugin const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin reset");
            auto& processor = floe.engine->processor;
            processor.processor_callbacks.reset(processor);
        },

    .process = [](clap_plugin const* plugin, clap_process_t const* process) -> clap_process_status {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "plugin process");
        ZoneKeyNum("instance", floe.id);
        ZoneKeyNum("events", process->in_events->size(process->in_events));
        ZoneKeyNum("num_frames", process->frames_count);

        ASSERT_HOT(floe.active);
        ASSERT_HOT(floe.processing);
        if (!floe.active || !floe.processing || !process) return CLAP_PROCESS_ERROR;
        ScopedNoDenormals const no_denormals;
        auto& processor = floe.engine->processor;
        return processor.processor_callbacks.process(floe.engine->processor, *process);
    },

    .get_extension = [](clap_plugin const* plugin, char const* id) -> void const* {
        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        ZoneScopedMessage(floe.trace_config, "plugin get_extension");
        if (NullTermStringsEqual(id, CLAP_EXT_STATE)) return &floe_plugin_state;
        if (NullTermStringsEqual(id, CLAP_EXT_GUI)) return &floe_gui;
        if (NullTermStringsEqual(id, CLAP_EXT_PARAMS)) return &floe_params;
        if (NullTermStringsEqual(id, CLAP_EXT_NOTE_PORTS)) return &floe_note_ports;
        if (NullTermStringsEqual(id, CLAP_EXT_AUDIO_PORTS)) return &floe_audio_ports;
        if (NullTermStringsEqual(id, CLAP_EXT_THREAD_POOL)) return &floe_thread_pool;
        if (NullTermStringsEqual(id, CLAP_EXT_TIMER_SUPPORT)) return &floe_timer;
        if (NullTermStringsEqual(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &floe_posix_fd;
        return nullptr;
    },

    .on_main_thread =
        [](clap_plugin const* plugin) {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin on_main_thread");
            ASSERT(IsMainThread(floe.host));
            if (floe.engine) {
                PollForSettingsFileChanges(g_shared_engine_systems->settings);

                bool update_gui = false;

                auto& processor = floe.engine->processor;
                processor.processor_callbacks.on_main_thread(processor, update_gui);
                EngineCallbacks().on_main_thread(*floe.engine, update_gui);
                if (update_gui && floe.gui_platform)
                    floe.gui_platform->last_result.ElevateUpdateRequest(
                        GuiFrameResult::UpdateRequest::Animate);
            }
        },
};

FloePluginInstance::FloePluginInstance(clap_host const* host) : host(*host) {
    g_log_file.Trace(k_main_log_module);
    clap_plugin = floe_plugin;
    clap_plugin.plugin_data = this;
}

clap_plugin const& CreateFloeInstance(clap_host const* host) {
    auto result = PageAllocator::Instance().New<FloePluginInstance>(host);
    return result->clap_plugin;
}
