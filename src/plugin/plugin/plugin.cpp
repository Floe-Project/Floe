// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clap/plugin.h"

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "common_infrastructure/error_reporting.hpp"

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

struct FloePluginInstance : PluginInstanceMessages {
    FloePluginInstance(clap_host const* clap_host, FloeInstanceIndex id);
    ~FloePluginInstance() { g_log.Trace(k_main_log_module); }

    clap_host const& host;
    clap_plugin clap_plugin;

    bool initialised {false};
    bool active {false};
    bool processing {false};

    FloeInstanceIndex const index;

    TracyMessageConfig trace_config {
        .category = "clap",
        .colour = 0xa88e39,
        .object_id = index,
    };

    void UpdateGui() override {
        ASSERT(IsMainThread(host));
        if (gui_platform)
            gui_platform->last_result.ElevateUpdateRequest(GuiFrameResult::UpdateRequest::Animate);
    }

    ArenaAllocator arena {PageAllocator::Instance()};

    Optional<Engine> engine {};

    u64 window_size_listener_id {};

    Optional<GuiPlatform> gui_platform {};
};

inline Allocator& FloeInstanceAllocator() { return PageAllocator::Instance(); }

static u16 g_num_initialised_plugins = 0;
static u16 g_num_floe_instances = 0;

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_state const floe_plugin_state {
    .save = [](clap_plugin const* plugin, clap_ostream const* stream) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "state save");

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: save() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            return EngineCallbacks().save_state(*floe.engine, *stream);
        } catch (PanicException) {
            return false;
        }
    },

    .load = [](clap_plugin const* plugin, clap_istream const* stream) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "state load");

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: load() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            return EngineCallbacks().load_state(*floe.engine, *stream);
        } catch (PanicException) {
            return false;
        }
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
// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_gui const floe_gui {
    .is_api_supported = [](clap_plugin_t const*, char const* api, bool is_floating) -> bool {
        if (!api) return false;
        if (is_floating) return false;
        return NullTermStringsEqual(k_supported_gui_api, api);
    },

    .get_preferred_api = [](clap_plugin_t const*, char const** api, bool* is_floating) -> bool {
        if (is_floating) *is_floating = false;
        if (api) *api = k_supported_gui_api;
        return true;
    },

    .create = [](clap_plugin_t const* plugin, char const* api, bool is_floating) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.create() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!NullTermStringsEqual(k_supported_gui_api, api)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.create() with unsupported api");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (is_floating) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.create() with floating window");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (floe.gui_platform) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.create() called twice");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            g_log.Debug(k_clap_log_module, "#{} gui.create()", floe.index);

            ZoneScopedMessage(floe.trace_config, "gui create");
            floe.gui_platform.Emplace(floe.host, g_shared_engine_systems->settings);
            return LogIfError(CreateView(*floe.gui_platform, *floe.engine), "CreateView");
        } catch (PanicException) {
            return false;
        }
    },

    .destroy =
        [](clap_plugin_t const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!IsMainThread(floe.host)) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: gui.destroy() not on main thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.gui_platform) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: gui.destroy() called twice");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                g_log.Debug(k_clap_log_module, "#{} gui.destroy()", floe.index);

                ZoneScopedMessage(floe.trace_config, "gui destroy");
                DestroyView(*floe.gui_platform);
                floe.gui_platform.Clear();
            } catch (PanicException) {
                return;
            }
        },

    .set_scale = [](clap_plugin_t const*, f64) -> bool {
        return false; // we (pugl) negotiate this with the OS ourselves
    },

    .get_size = [](clap_plugin_t const* plugin, u32* width, u32* height) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.get_size() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.gui_platform) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: gui.get_size() called before gui.create()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            auto const size =
                PhysicalPixelsToClapPixels(floe.gui_platform->view, WindowSize(*floe.gui_platform));
            *width = size.width;
            *height = size.height;
            g_log.Debug(k_clap_log_module, "get_size result {}x{}", *width, *height);
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .can_resize = [](clap_plugin_t const*) -> bool { return true; },

    .get_resize_hints = [](clap_plugin_t const* plugin, clap_gui_resize_hints_t* hints) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: gui.get_resize_hints() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            hints->can_resize_vertically = true;
            hints->can_resize_horizontally = true;
            hints->preserve_aspect_ratio = true;
            auto const ratio =
                gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui);
            hints->aspect_ratio_width = ratio.width;
            hints->aspect_ratio_height = ratio.height;
            g_log.Debug(k_clap_log_module,
                        "get_resize_hints {}x{}",
                        hints->aspect_ratio_width,
                        hints->aspect_ratio_height);
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .adjust_size = [](clap_plugin_t const* plugin, u32* clap_width, u32* clap_height) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.adjust_size() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            *clap_width = Clamp<u32>(*clap_width, 1, k_largest_gui_size);
            *clap_height = Clamp<u32>(*clap_height, 1, k_largest_gui_size);
            auto const size = ClapPixelsToPhysicalPixels(floe.gui_platform->view, *clap_width, *clap_height);

            auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize(
                size,
                gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));

            if (aspect_ratio_conformed_size.width < gui_settings::k_min_gui_width) return false;

            auto const clap_size =
                PhysicalPixelsToClapPixels(floe.gui_platform->view, aspect_ratio_conformed_size);

            g_log.Debug(k_clap_log_module,
                        "adjust_size in: {}x{}, out: {}x{}",
                        *clap_width,
                        *clap_height,
                        clap_size.width,
                        clap_size.height);

            *clap_width = clap_size.width;
            *clap_height = clap_size.height;

            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .set_size = [](clap_plugin_t const* plugin, u32 clap_width, u32 clap_height) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.set_size() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.gui_platform) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: gui.set_size() called before gui.create()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            {
                auto w = clap_width;
                auto h = clap_height;
                if (floe_gui.adjust_size(plugin, &w, &h) && (w != clap_width && h != clap_height)) {
                    g_log.Warning(
                        k_clap_log_module,
                        "host misbehaving: gui.set_size() called with unadjusted size, given: {}x{}, adjusted {}x{}",
                        clap_width,
                        clap_height,
                        w,
                        h);
                }
            }

            ZoneScopedMessage(floe.trace_config, "gui set_size {} {}", clap_width, clap_height);

            if (clap_width == 0 || clap_height == 0) return false;
            if (clap_width > k_largest_gui_size || clap_height > k_largest_gui_size) return false;

            auto const size = ClapPixelsToPhysicalPixels(floe.gui_platform->view, clap_width, clap_height);

            auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize(
                size,
                gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));
            g_log.Debug(k_clap_log_module,
                        "#{} set_size in: {}x{}, constrained {}x{}, result: {}",
                        floe.index,
                        size.width,
                        size.height,
                        aspect_ratio_conformed_size.width,
                        aspect_ratio_conformed_size.height,
                        aspect_ratio_conformed_size.width == size.width &&
                            aspect_ratio_conformed_size.height == size.height);
            if (aspect_ratio_conformed_size.width != size.width ||
                aspect_ratio_conformed_size.height != size.height)
                return false;
            return SetSize(*floe.gui_platform, size);
        } catch (PanicException) {
            return false;
        }
    },

    .set_parent = [](clap_plugin_t const* plugin, clap_window_t const* window) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.set_parent() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.gui_platform) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: gui.set_parent() called before gui.create()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!window || !window->ptr) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: gui.set_parent() called with invalid window");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            g_log.Debug(k_clap_log_module, "#{} gui.set_parent()", floe.index);

            ZoneScopedMessage(floe.trace_config, "gui set_parent");
            auto const result = LogIfError(SetParent(*floe.gui_platform, *window), "SetParent");

            // Bitwig never calls show() so we do it here
            auto _ = SetVisible(*floe.gui_platform, true);

            return result;
        } catch (PanicException) {
            return false;
        }
    },

    .set_transient = [](clap_plugin_t const*, clap_window_t const*) -> bool {
        return false; // we don't support floating windows
    },

    .suggest_title =
        [](clap_plugin_t const*, char const*) {
            // we don't support floating windows
        },

    .show = [](clap_plugin_t const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.show() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.gui_platform) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.show() called before gui.create()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            g_log.Debug(k_clap_log_module, "#{} gui.show()", floe.index);
            ZoneScopedMessage(floe.trace_config, "gui show");
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
        } catch (PanicException) {
            return false;
        }
    },

    .hide = [](clap_plugin_t const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.hide() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.gui_platform) {
                g_log.Warning(k_clap_log_module, "host misbehaving: gui.hide() called before gui.create()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            g_log.Debug(k_clap_log_module, "gui.show()");
            ZoneScopedMessage(floe.trace_config, "gui hide");
            return LogIfError(SetVisible(*floe.gui_platform, false), "SetVisible");
        } catch (PanicException) {
            return false;
        }
    },
};

static void CheckInputEvents(clap_input_events const* in) {
    if constexpr (!RUNTIME_SAFETY_CHECKS_ON) return;

    for (auto const event_index : Range(in->size(in))) {
        auto e = in->get(in, event_index);
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (e->type == CLAP_EVENT_PARAM_VALUE) {
            auto& value = *CheckedPointerCast<clap_event_param_value const*>(e);
            auto const opt_index = ParamIdToIndex(value.param_id);
            ASSERT(opt_index);
            auto const param_desc = k_param_descriptors[(usize)*opt_index];
            ASSERT(value.value >= (f64)param_desc.linear_range.min);
            ASSERT(value.value <= (f64)param_desc.linear_range.max);
        }
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_params const floe_params {
    .count = [](clap_plugin_t const*) -> u32 { return (u32)k_num_parameters; },

    .get_info = [](clap_plugin_t const*, u32 param_index, clap_param_info_t* param_info) -> bool {
        // This callback should be main-thread only, but we don't care if that's not true since we don't use
        // any shared state.
        auto const& desc = k_param_descriptors[param_index];
        param_info->id = ParamIndexToId((ParamIndex)param_index);
        param_info->default_value = (f64)desc.default_linear_value;
        param_info->max_value = (f64)desc.linear_range.max;
        param_info->min_value = (f64)desc.linear_range.min;
        CopyStringIntoBufferWithNullTerm(param_info->name, desc.name);
        CopyStringIntoBufferWithNullTerm(param_info->module, desc.ModuleString());
        param_info->cookie = nullptr;
        param_info->flags = 0;
        if (!desc.flags.not_automatable) param_info->flags |= CLAP_PARAM_IS_AUTOMATABLE;
        if (desc.value_type == ParamValueType::Menu || desc.value_type == ParamValueType::Bool ||
            desc.value_type == ParamValueType::Int)
            param_info->flags |= CLAP_PARAM_IS_STEPPED;

        return true;
    },

    .get_value = [](clap_plugin_t const* plugin, clap_id param_id, f64* out_value) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: params.get_value() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.engine) {
                g_log.Warning(k_clap_log_module, "host misbehaving: params.get_value() called before init()");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            auto const opt_index = ParamIdToIndex(param_id);
            if (!opt_index) return false;

            auto const index = (usize)*opt_index;

            // IMPROVE: handle params without atomics (part of larger refactor)
            if (floe.engine->pending_state_change)
                *out_value = (f64)floe.engine->last_snapshot.state.param_values[index];
            else
                *out_value = (f64)floe.engine->processor.params[index].value.Load(LoadMemoryOrder::Relaxed);

            ASSERT(*out_value >= (f64)k_param_descriptors[index].linear_range.min);
            ASSERT(*out_value <= (f64)k_param_descriptors[index].linear_range.max);

            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .value_to_text =
        [](clap_plugin_t const*, clap_id param_id, f64 value, char* out_buffer, u32 out_buffer_capacity)
        -> bool {
        if (PanicOccurred()) return false;

        try {
            auto const opt_index = ParamIdToIndex(param_id);
            if (!opt_index) return false;
            auto const index = (usize)*opt_index;
            auto const str = k_param_descriptors[index].LinearValueToString((f32)value);
            if (!str) return false;
            if (out_buffer_capacity < (str->size + 1)) return false;
            CopyMemory(out_buffer, str->data, str->size);
            out_buffer[str->size] = '\0';
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .text_to_value =
        [](clap_plugin_t const*, clap_id param_id, char const* param_value_text, f64* out_value) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto const opt_index = ParamIdToIndex(param_id);
            if (!opt_index) return false;
            auto const index = (usize)*opt_index;
            if (auto v =
                    k_param_descriptors[index].StringToLinearValue(FromNullTerminated(param_value_text))) {
                *out_value = (f64)*v;
                ASSERT(*out_value >= (f64)k_param_descriptors[index].linear_range.min);
                ASSERT(*out_value <= (f64)k_param_descriptors[index].linear_range.max);
                return true;
            }
            return false;
        } catch (PanicException) {
            return false;
        }
    },

    // [active ? audio-thread : main-thread]
    .flush =
        [](clap_plugin_t const* plugin, clap_input_events_t const* in, clap_output_events_t const* out) {
            if (PanicOccurred()) return;

            try {
                ZoneScopedN("clap_plugin_params flush");
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!floe.engine) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: params.flush() called before init()");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.active) {
                    if (!IsMainThread(floe.host)) {
                        g_log.Warning(k_clap_log_module,
                                      "host misbehaving: params.flush() not on main thread when inactive");
                        ASSERT(!RunningInStandalone(floe.host));
                        return;
                    }
                } else if (IsAudioThread(floe.host) == IsAudioThreadResult::No) {
                    g_log.Warning(k_clap_log_module,
                                  "host misbehaving: params.flush() not on audio thread when active");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!in || !out) return;

                CheckInputEvents(in);

                auto& processor = floe.engine->processor;
                processor.processor_callbacks.flush_parameter_events(processor, *in, *out);
            } catch (PanicException) {
            }
        },
};

static constexpr clap_id k_input_port_id = 1;
static constexpr clap_id k_output_port_id = 2;

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_audio_ports const floe_audio_ports {
    .count = [](clap_plugin_t const*, bool) -> u32 { return 1; },

    .get = [](clap_plugin_t const*, u32 index, bool is_input, clap_audio_port_info_t* info) -> bool {
        if (index != 0) {
            g_log.Warning(k_clap_log_module, "host misbehaving: audio_ports.get() called with invalid index");
            return false;
        }
        if (!info) return false;

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
// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_note_ports const floe_note_ports {
    .count = [](clap_plugin_t const*, bool is_input) -> u32 { return is_input ? 1 : 0; },

    .get = [](clap_plugin_t const*, u32 index, bool is_input, clap_note_port_info_t* info) -> bool {
        if (!is_input) {
            g_log.Warning(k_clap_log_module,
                          "host misbehaving: note_ports.get() thinks we have output ports");
            return false;
        }

        if (index != 0) {
            g_log.Warning(k_clap_log_module, "host misbehaving: note_ports.get() called with invalid index");
            return false;
        }

        if (!info) return false;

        ZoneScopedN("clap_plugin_note_ports get");
        info->id = k_main_note_port_id;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        CopyStringIntoBufferWithNullTerm(info->name, "Notes In"_s);
        return true;
    },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_panicking is initialised
clap_plugin_thread_pool const floe_thread_pool {
    .exec =
        [](clap_plugin_t const* plugin, u32 task_index) {
            if (PanicOccurred()) return;

            try {
                ZoneScopedN("clap_plugin_thread_pool exec");
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                floe.engine->processor.processor_callbacks.on_thread_pool_exec(floe.engine->processor,
                                                                               task_index);
            } catch (PanicException) {
            }
        },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_timer_support const floe_timer {
    .on_timer =
        [](clap_plugin_t const* plugin, clap_id timer_id) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!IsMainThread(floe.host)) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: on_timer() not on main thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.engine) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: on_timer() called before init()");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                ZoneScopedN("clap_plugin_timer_support on_timer");

                // We don't care about the timer_id, we just want to poll.
                PollForSettingsFileChanges(g_shared_engine_systems->settings);

                if (floe.gui_platform) OnClapTimer(*floe.gui_platform, timer_id);
                if (floe.engine) EngineCallbacks().on_timer(*floe.engine, timer_id);
            } catch (PanicException) {
            }
        },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_posix_fd_support const floe_posix_fd {
    .on_fd =
        [](clap_plugin_t const* plugin, int fd, clap_posix_fd_flags_t) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!IsMainThread(floe.host)) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: on_fd() not on main thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.engine) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: on_fd() called before init()");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                ZoneScopedN("clap_plugin_posix_fd_support on_fd");
                if (floe.gui_platform) OnPosixFd(*floe.gui_platform, fd);
            } catch (PanicException) {
            }
        },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_panicking is initialised
FloeClapExtensionPlugin const floe_custom_ext {
    .state_change_is_pending = [](clap_plugin_t const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            return floe.engine->pending_state_change.HasValue();
        } catch (PanicException) {
            return false;
        }
    },
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin const floe_plugin {
    .desc = &k_plugin_info,
    .plugin_data = nullptr,

    .init = [](clap_plugin const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {

            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (floe.initialised) {
                g_log.Warning(k_clap_log_module, "host misbehaving: init() called twice");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (auto const thread_check =
                    (clap_host_thread_check const*)floe.host.get_extension(&floe.host, CLAP_EXT_THREAD_CHECK);
                thread_check && !thread_check->is_main_thread(&floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: init() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            ASSERT(g_num_initialised_plugins !=
                   LargestRepresentableValue<decltype(g_num_initialised_plugins)>());

            ZoneScopedMessage(floe.trace_config, "plugin init");

            if (g_num_initialised_plugins++ == 0) {
                SetThreadName("main");

                DynamicArrayBounded<sentry::Tag, 4> tags {};
                {
                    // the clap spec says these should be valid
                    ASSERT(floe.host.name && floe.host.name[0]);
                    ASSERT(floe.host.version && floe.host.version[0]);

                    auto const host_name = FromNullTerminated(floe.host.name);
                    dyn::Append(tags, {"host_name"_s, host_name});
                    dyn::Append(tags, {"host_version"_s, FromNullTerminated(floe.host.version)});
                    if (floe.host.vendor && floe.host.vendor[0])
                        dyn::Append(tags, {"host_vendor"_s, FromNullTerminated(floe.host.vendor)});
                }

                g_shared_engine_systems.Emplace(tags);

                g_log.Info(k_main_log_module,
                           "host: {} {} {}",
                           floe.host.vendor,
                           floe.host.name,
                           floe.host.version);

                sentry::Error message {{
                    .level = sentry::ErrorEvent::Level::Info,
                    .message = "Host start 2"_s,
                }};
                ReportError(Move(message));
            }

            g_log.Debug(k_clap_log_module, "#{} init", floe.index);

            floe.engine.Emplace(floe.host, *g_shared_engine_systems, floe);

            // IMPORTANT: engine is initialised first
            g_shared_engine_systems->RegisterFloeInstance(&floe.clap_plugin, floe.index);

            floe.initialised = true;
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .destroy =
        [](clap_plugin const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin destroy (init:{})", floe.initialised);

                if (floe.initialised) {
                    if (!IsMainThread(floe.host)) {
                        g_log.Warning(k_clap_log_module, "host misbehaving: destroy() not on main thread");
                        ASSERT(!RunningInStandalone(floe.host));
                        return;
                    }

                    if (floe.gui_platform) {
                        g_log.Warning(k_clap_log_module,
                                      "host misbehaving: destroy() called while gui is active");
                        ASSERT(!RunningInStandalone(floe.host));
                        floe_gui.destroy(plugin);
                    }

                    if (floe.active) {
                        g_log.Warning(k_clap_log_module, "host misbehaving: destroy() called while active");
                        ASSERT(!RunningInStandalone(floe.host));
                        floe_plugin.deactivate(plugin);
                    }

                    // IMPORTANT: engine is cleared after
                    g_shared_engine_systems->UnregisterFloeInstance(floe.index);

                    floe.engine.Clear();

                    ASSERT(g_num_initialised_plugins);
                    if (--g_num_initialised_plugins == 0) g_shared_engine_systems.Clear();
                }

                g_log.Debug(k_clap_log_module, "#{} destroy", floe.index);

                --g_num_floe_instances;
                FloeInstanceAllocator().Delete(&floe);
            } catch (PanicException) {
            }
        },

    .activate =
        [](clap_plugin const* plugin, f64 sample_rate, u32 min_frames_count, u32 max_frames_count) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin activate");

            if (!IsMainThread(floe.host)) {
                g_log.Warning(k_clap_log_module, "host misbehaving: activate() not on main thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (floe.active) {
                g_log.Warning(k_clap_log_module, "host misbehaving: activate() when already active");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            auto& processor = floe.engine->processor;
            if (!processor.processor_callbacks.activate(processor,
                                                        {
                                                            .sample_rate = sample_rate,
                                                            .min_block_size = min_frames_count,
                                                            .max_block_size = max_frames_count,
                                                        }))
                return false;
            floe.active = true;
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .deactivate =
        [](clap_plugin const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin activate");

                if (!IsMainThread(floe.host)) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: deactivate() not on main thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.active) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: deactivate() when not active");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                auto& processor = floe.engine->processor;
                processor.processor_callbacks.deactivate(processor);
                floe.active = false;
            } catch (PanicException) {
            }
        },

    .start_processing = [](clap_plugin const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin start_processing");

            if (IsAudioThread(floe.host) == IsAudioThreadResult::No) {
                g_log.Warning(k_clap_log_module, "host misbehaving: start_processing() not on audio thread");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (!floe.active) {
                g_log.Warning(k_clap_log_module, "host misbehaving: start_processing() when not active");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            if (floe.processing) {
                g_log.Warning(k_clap_log_module,
                              "host misbehaving: start_processing() when already processing");
                ASSERT(!RunningInStandalone(floe.host));
                return false;
            }

            auto& processor = floe.engine->processor;
            processor.processor_callbacks.start_processing(processor);
            floe.processing = true;
            return true;
        } catch (PanicException) {
            return false;
        }
    },

    .stop_processing =
        [](clap_plugin const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin stop_processing");

                if (IsAudioThread(floe.host) == IsAudioThreadResult::No) {
                    g_log.Warning(k_clap_log_module,
                                  "host misbehaving: stop_processing() not on audio thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.active) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: stop_processing() when not active");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.processing) {
                    g_log.Warning(k_clap_log_module,
                                  "host misbehaving: stop_processing() when not processing");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                auto& processor = floe.engine->processor;
                processor.processor_callbacks.stop_processing(processor);
                floe.processing = false;
            } catch (PanicException) {
            }
        },

    .reset =
        [](clap_plugin const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin reset");

                if (IsAudioThread(floe.host) == IsAudioThreadResult::No) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: reset() not on audio thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (!floe.active) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: reset() when not active");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                auto& processor = floe.engine->processor;
                processor.processor_callbacks.reset(processor);
            } catch (PanicException) {
            }
        },

    .process = [](clap_plugin const* plugin, clap_process_t const* process) -> clap_process_status {
        if (PanicOccurred()) return CLAP_PROCESS_ERROR;

        try {
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin process");
            ZoneKeyNum("instance", floe.index);
            ZoneKeyNum("events", process->in_events->size(process->in_events));
            ZoneKeyNum("num_frames", process->frames_count);

            if (!floe.active || !floe.processing || !process ||
                IsAudioThread(floe.host) == IsAudioThreadResult::No)
                return CLAP_PROCESS_ERROR;

            CheckInputEvents(process->in_events);

            ScopedNoDenormals const no_denormals;
            auto& processor = floe.engine->processor;
            return processor.processor_callbacks.process(floe.engine->processor, *process);
        } catch (PanicException) {
            return CLAP_PROCESS_ERROR;
        }
    },

    .get_extension = [](clap_plugin const* plugin, char const* id) -> void const* {
        if (PanicOccurred()) return nullptr;

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
        if (NullTermStringsEqual(id, k_floe_clap_extension_id)) return &floe_custom_ext;
        return nullptr;
    },

    .on_main_thread =
        [](clap_plugin const* plugin) {
            if (PanicOccurred()) return;

            try {
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin on_main_thread");

                if (!IsMainThread(floe.host)) {
                    g_log.Warning(k_clap_log_module, "host misbehaving: on_main_thread() not on main thread");
                    ASSERT(!RunningInStandalone(floe.host));
                    return;
                }

                if (floe.engine) {
                    PollForSettingsFileChanges(g_shared_engine_systems->settings);

                    auto& processor = floe.engine->processor;
                    processor.processor_callbacks.on_main_thread(processor);
                    EngineCallbacks().on_main_thread(*floe.engine);
                }
            } catch (PanicException) {
            }
        },
};

FloePluginInstance::FloePluginInstance(clap_host const* host, FloeInstanceIndex index)
    : host(*host)
    , index(index) {
    g_log.Trace(k_main_log_module);
    clap_plugin = floe_plugin;
    clap_plugin.plugin_data = this;
}

clap_plugin const* CreateFloeInstance(clap_host const* host) {
    // TODO: fix g_num_floe_instances; instances can be added and removed - we can't just count it
    if (g_num_floe_instances == k_max_num_floe_instances) return nullptr;
    auto result = FloeInstanceAllocator().New<FloePluginInstance>(host, g_num_floe_instances++);
    return &result->clap_plugin;
}

void RequestGuiResize(clap_plugin const& plugin) {
    auto const& floe = *(FloePluginInstance*)plugin.plugin_data;
    ASSERT(IsMainThread(floe.host));
    if (!floe.gui_platform) return;
    auto const host_gui = (clap_host_gui const*)floe.host.get_extension(&floe.host, CLAP_EXT_GUI);
    if (host_gui) {
        auto const size = PhysicalPixelsToClapPixels(
            floe.gui_platform->view,
            gui_settings::WindowSize(g_shared_engine_systems->settings.settings.gui));
        host_gui->resize_hints_changed(&floe.host);
        host_gui->request_resize(&floe.host, size.width, size.height);
    }
}

void OnPollThread(clap_plugin const& plugin) {
    // We're on the polling thread, but we can be sure that the engine is active because our
    // Register/Unregister calls are correctly before/after.
    auto& floe = *(FloePluginInstance*)plugin.plugin_data;
    ASSERT(floe.engine);
    EngineCallbacks().on_poll_thread(*floe.engine);
}
