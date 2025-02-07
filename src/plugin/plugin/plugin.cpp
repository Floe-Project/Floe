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
#include "settings/settings.hpp"

//
#include "os/undef_windows_macros.h"

[[clang::no_destroy]] Optional<SharedEngineSystems> g_shared_engine_systems {};

enum class ClapLoggingLevel {
    Majority, // log only the majority of CLAP calls
    AllNonRealtime, // log all CLAP calls except audio-thread ones
};
constexpr ClapLoggingLevel k_clap_logging_level =
    PRODUCTION_BUILD ? ClapLoggingLevel::Majority : ClapLoggingLevel::AllNonRealtime;

struct FloePluginInstance : PluginInstanceMessages {
    FloePluginInstance(clap_host const* clap_host, FloeInstanceIndex id);
    ~FloePluginInstance() { Trace(k_main_log_module); }

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

// Macro because it declares a constructor/destructor object for timing start/end
#define TRACE_CLAP_CALL(name) ZoneScopedMessage(floe.trace_config, name)

inline void LogClapApi(FloePluginInstance& floe, ClapLoggingLevel level, String name) {
    if (k_clap_logging_level >= level) LogInfo(k_clap_log_module, "{} #{}: {}", name, floe.index);
}

inline void
LogClapFunction(FloePluginInstance& floe, ClapLoggingLevel level, String name, String format, auto... args) {
    if (k_clap_logging_level >= level) {
        ArenaAllocatorWithInlineStorage<400> arena {PageAllocator::Instance()};
        LogInfo(k_clap_log_module, "{} #{}: {}", name, floe.index, fmt::Format(arena, format, args...));
    }
}

inline bool Check(FloePluginInstance& floe, bool condition, String function_name, String message) {
    if (!condition) LogWarning(k_clap_log_module, "{} #{}: {}", function_name, floe.index, message);
    return condition;
}

inline bool Check(bool condition, String function_name, String message) {
    if (!condition) LogWarning(k_clap_log_module, "{}: {}", function_name, message);
    return condition;
}

static FloePluginInstance& GetFloe(clap_plugin const* plugin) {
    ASSERT(plugin);
    return *(FloePluginInstance*)plugin->plugin_data;
}

bool ClapStateSave(clap_plugin const* plugin, clap_ostream const* stream) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "state.save";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, stream, k_func, "stream is null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;

        return EngineCallbacks().save_state(*floe.engine, *stream);
    } catch (PanicException) {
        return false;
    }
}

static bool ClapStateLoad(clap_plugin const* plugin, clap_istream const* stream) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "state.load";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, stream, k_func, "stream is null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;

        return EngineCallbacks().load_state(*floe.engine, *stream);
    } catch (PanicException) {
        return false;
    }
}

clap_plugin_state const floe_plugin_state {
    .save = ClapStateSave,
    .load = ClapStateLoad,
};

static bool LogIfError(ErrorCodeOr<void> const& ec, String name) {
    if (ec.HasError()) {
        ReportError(sentry::Error::Level::Warning, name);
        return false;
    }
    return true;
}

static bool ClapGuiIsApiSupported(clap_plugin_t const* plugin, char const* api, bool is_floating) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.is_api_supported";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        if (!Check(api, k_func, "api is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe,
                        ClapLoggingLevel::AllNonRealtime,
                        k_func,
                        "api: {}, is_floating: {}",
                        api,
                        is_floating);

        if (is_floating) return false;
        return NullTermStringsEqual(k_supported_gui_api, api);
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiGetPrefferedApi(clap_plugin_t const* plugin, char const** api, bool* is_floating) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.get_preferred_api";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::AllNonRealtime, k_func);

        if (is_floating) *is_floating = false;
        if (api) *api = k_supported_gui_api;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiCreate(clap_plugin_t const* plugin, char const* api, bool is_floating) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.create";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        if (!Check(api, k_func, "api is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe,
                        ClapLoggingLevel::Majority,
                        k_func,
                        "api: {}, is_floating: {}",
                        api,
                        is_floating);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;
        if (!Check(floe, NullTermStringsEqual(k_supported_gui_api, api), k_func, "unsupported api"))
            return false;
        if (!Check(floe, !is_floating, k_func, "floating windows are not supported")) return false;
        if (!Check(floe, !floe.gui_platform, k_func, "already created gui")) return false;

        floe.gui_platform.Emplace(floe.host, g_shared_engine_systems->settings);
        return LogIfError(CreateView(*floe.gui_platform), "CreateView");
    } catch (PanicException) {
        return false;
    }
}

static void ClapGuiDestroy(clap_plugin const* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "gui.destroy";
        if (!Check(plugin, k_func, "plugin is null")) return;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return;

        DestroyView(*floe.gui_platform);
        floe.gui_platform.Clear();
    } catch (PanicException) {
        return;
    }
}

static bool ClapGuiSetScale(clap_plugin_t const* plugin, f64 scale) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.set_scale";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        LogClapFunction(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func, "scale: {}", scale);
    } catch (PanicException) {
    }

    return false; // we (pugl) negotiate this with the OS ourselves
}

static bool ClapGuiGetSize(clap_plugin_t const* plugin, u32* width, u32* height) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.get_size";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, width || height, k_func, "width and height both null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        auto const size = PhysicalPixelsToClapPixels(floe.gui_platform->view, WindowSize(*floe.gui_platform));
        if (width) *width = size.width;
        if (height) *height = size.height;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiCanResize(clap_plugin_t const* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.can_resize";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        LogClapApi(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func);

        // Should be main-thread but we don't care if it's not.

        return true;
    } catch (PanicException) {
    }

    return false;
}

static bool ClapGuiGetResizeHints(clap_plugin_t const* plugin, clap_gui_resize_hints_t* hints) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.get_resize_hints";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::AllNonRealtime, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, hints, k_func, "hints is null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

        hints->can_resize_vertically = true;
        hints->can_resize_horizontally = true;
        hints->preserve_aspect_ratio = true;
        auto const ratio = gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui);
        hints->aspect_ratio_width = ratio.width;
        hints->aspect_ratio_height = ratio.height;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static Optional<UiSize>
GetUsableSizeWithinDimensions(GuiPlatform& gui_platform, u32 clap_width, u32 clap_height) {
    clap_width = Clamp<u32>(clap_width, 1, gui_settings::k_largest_gui_size);
    clap_height = Clamp<u32>(clap_height, 1, gui_settings::k_largest_gui_size);

    auto const size = ClapPixelsToPhysicalPixels(gui_platform.view, clap_width, clap_height);
    if (!size) return k_nullopt;

    auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize(
        *size,
        gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));

    if (!aspect_ratio_conformed_size) return k_nullopt;
    if (aspect_ratio_conformed_size->width < gui_settings::k_min_gui_width) return k_nullopt;

    return PhysicalPixelsToClapPixels(gui_platform.view, *aspect_ratio_conformed_size);
}

static bool ClapGuiAdjustSize(clap_plugin_t const* plugin, u32* clap_width, u32* clap_height) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.adjust_size";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        if (!Check(clap_width && clap_height, k_func, "width or height is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::Majority, k_func, "{} x {}", *clap_width, *clap_height);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

        if (!floe.gui_platform || !floe.gui_platform->view) {
            // We've been called before we have the ability to check our scaling factor, we can still give a
            // reasonable result by getting the nearest aspect ratio size.

            auto const aspect_ratio_conformed_size = gui_settings::GetNearestAspectRatioSizeInsideSize32(
                {*clap_width, *clap_height},
                gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui));

            if (!aspect_ratio_conformed_size) return false;

            *clap_width = aspect_ratio_conformed_size->width;
            *clap_height = aspect_ratio_conformed_size->height;
            return true;
        } else if (auto const size =
                       GetUsableSizeWithinDimensions(*floe.gui_platform, *clap_width, *clap_height)) {
            *clap_width = size->width;
            *clap_height = size->height;
            return true;
        }
    } catch (PanicException) {
    }

    return false;
}

static bool ClapGuiSetSize(clap_plugin_t const* plugin, u32 clap_width, u32 clap_height) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.set_size";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::Majority, k_func, "{} x {}", clap_width, clap_height);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        auto const size = ClapPixelsToPhysicalPixels(floe.gui_platform->view, clap_width, clap_height);

        if (!Check(floe, size && size->width >= gui_settings::k_min_gui_width, k_func, "invalid size"))
            return false;
        if (!Check(floe,
                   gui_settings::IsAspectRatio(
                       *size,
                       gui_settings::CurrentAspectRatio(g_shared_engine_systems->settings.settings.gui)),
                   k_func,
                   "invalid aspect ratio")) {
            return false;
        }

        return SetSize(*floe.gui_platform, *size);
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiSetParent(clap_plugin_t const* plugin, clap_window_t const* window) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.set_parent";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, window, k_func, "window is null")) return false;
        if (!Check(floe, window->ptr, k_func, "window ptr is null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        auto const result = LogIfError(SetParent(*floe.gui_platform, *window), "SetParent");

        // Bitwig never calls show() so we do it here
        auto _ = SetVisible(*floe.gui_platform, true, *floe.engine);

        return result;
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiSetTransient(clap_plugin_t const* plugin, clap_window_t const*) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.set_transient";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        LogClapApi(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func);

        return false; // we don't support floating windows
    } catch (PanicException) {
    }

    return false;
}

static void ClapGuiSuggestTitle(clap_plugin_t const* plugin, char const*) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "gui.set_transient";
        if (!Check(plugin, k_func, "plugin is null")) return;

        LogClapApi(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func);

        // we don't support floating windows

    } catch (PanicException) {
    }
}

static bool ClapGuiShow(clap_plugin_t const* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.show";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        bool const result = LogIfError(SetVisible(*floe.gui_platform, true, *floe.engine), "SetVisible");
        if (result) {
            static bool shown_graphics_info = false;
            if (!shown_graphics_info) {
                shown_graphics_info = true;
                LogInfo(k_main_log_module,
                        "\n{}",
                        floe.gui_platform->graphics_ctx->graphics_device_info.Items());
            }
        }
        return result;
    } catch (PanicException) {
        return false;
    }
}

static bool ClapGuiHide(clap_plugin_t const* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.hide";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::Majority, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        return LogIfError(SetVisible(*floe.gui_platform, false, *floe.engine), "SetVisible");
    } catch (PanicException) {
        return false;
    }
}

// Size (width, height) is in pixels; the corresponding windowing system extension is
// responsible for defining if it is physical pixels or logical pixels.
clap_plugin_gui const floe_gui {
    .is_api_supported = ClapGuiIsApiSupported,
    .get_preferred_api = ClapGuiGetPrefferedApi,
    .create = ClapGuiCreate,
    .destroy = ClapGuiDestroy,
    .set_scale = ClapGuiSetScale,
    .get_size = ClapGuiGetSize,
    .can_resize = ClapGuiCanResize,
    .get_resize_hints = ClapGuiGetResizeHints,
    .adjust_size = ClapGuiAdjustSize,
    .set_size = ClapGuiSetSize,
    .set_parent = ClapGuiSetParent,
    .set_transient = ClapGuiSetTransient,
    .suggest_title = ClapGuiSuggestTitle,
    .show = ClapGuiShow,
    .hide = ClapGuiHide,
};

[[nodiscard]] static bool CheckInputEvents(clap_input_events const* in) {
    if constexpr (!RUNTIME_SAFETY_CHECKS_ON) return true;

    for (auto const event_index : Range(in->size(in))) {
        auto e = in->get(in, event_index);
        if (!e) return false;
        if (e->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (e->type == CLAP_EVENT_PARAM_VALUE) {
            auto& value = *CheckedPointerCast<clap_event_param_value const*>(e);
            auto const opt_index = ParamIdToIndex(value.param_id);
            if (!opt_index) return false;
            auto const param_desc = k_param_descriptors[(usize)*opt_index];
            if (value.value < (f64)param_desc.linear_range.min ||
                value.value > (f64)param_desc.linear_range.max)
                return false;
        }
    }
    return true;
}

static u32 ClapParamsCount(clap_plugin_t const*) { return (u32)k_num_parameters; }

static bool ClapParamsGetInfo(clap_plugin_t const* plugin, u32 param_index, clap_param_info_t* param_info) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "params.get_info";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::AllNonRealtime, k_func, "index: {}", param_index);

        if (!Check(floe, param_info, k_func, "param_info is null")) return false;
        if (!Check(floe, param_index < k_num_parameters, k_func, "param_index out of range")) return false;

        // This callback should be main-thread only, but we don't care since we don't use any shared state.

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
    } catch (PanicException) {
        return false;
    }
}

static bool ClapParamsGetValue(clap_plugin_t const* plugin, clap_id param_id, f64* out_value) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "params.get_value";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::AllNonRealtime, k_func, "id: {}", param_id);

        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;

        if (!Check(floe, out_value, k_func, "out_value is null")) return false;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

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
}

static bool ClapParamsValueToText(clap_plugin_t const* plugin,
                                  clap_id param_id,
                                  f64 value,
                                  char* out_buffer,
                                  u32 out_buffer_capacity) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "params.value_to_text";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::AllNonRealtime, k_func, "id: {}, value: {}", param_id, value);

        if (out_buffer_capacity == 0) return false;
        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;
        if (!Check(floe, out_buffer, k_func, "out_buffer is null")) return false;
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
}

static bool ClapParamsTextToValue(clap_plugin_t const* plugin,
                                  clap_id param_id,
                                  char const* param_value_text,
                                  f64* out_value) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "params.text_to_value";
        if (!Check(plugin, k_func, "plugin is null")) return false;

        auto& floe = GetFloe(plugin);
        LogClapFunction(floe, ClapLoggingLevel::AllNonRealtime, k_func, "id: {}", param_id);
        TRACE_CLAP_CALL(k_func);

        auto const opt_index = ParamIdToIndex(param_id);
        if (!opt_index) return false;
        auto const index = (usize)*opt_index;

        if (!Check(floe, param_value_text, k_func, "param_value_text is null")) return false;
        if (auto v = k_param_descriptors[index].StringToLinearValue(FromNullTerminated(param_value_text))) {
            if (!Check(floe, out_value, k_func, "out_value is null")) return false;
            *out_value = (f64)*v;
            ASSERT(*out_value >= (f64)k_param_descriptors[index].linear_range.min);
            ASSERT(*out_value <= (f64)k_param_descriptors[index].linear_range.max);
            return true;
        }
        return false;
    } catch (PanicException) {
        return false;
    }
}

// [active ? audio-thread : main-thread]
static void
ClapParamsFlush(clap_plugin_t const* plugin, clap_input_events_t const* in, clap_output_events_t const* out) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "params.flush";
        if (!plugin) return;

        auto& floe = GetFloe(plugin);
        if (!floe.active)
            LogClapFunction(floe, ClapLoggingLevel::AllNonRealtime, k_func, "num in: {}", in->size(in));
        TRACE_CLAP_CALL(k_func);

        if (!in) return;
        if (!out) return;
        if (!floe.initialised) return;

        if (floe.active && IsAudioThread(floe.host) == IsAudioThreadResult::No)
            return;
        else if (!floe.active && !IsMainThread(floe.host))
            return;

        if (!CheckInputEvents(in)) return;

        auto& processor = floe.engine->processor;
        processor.processor_callbacks.flush_parameter_events(processor, *in, *out);
    } catch (PanicException) {
    }
}

clap_plugin_params const floe_params {
    .count = ClapParamsCount,
    .get_info = ClapParamsGetInfo,
    .get_value = ClapParamsGetValue,
    .value_to_text = ClapParamsValueToText,
    .text_to_value = ClapParamsTextToValue,
    .flush = ClapParamsFlush,
};

static constexpr clap_id k_input_port_id = 1;
static constexpr clap_id k_output_port_id = 2;

static u32 ClapAudioPortsCount(clap_plugin_t const* plugin, [[maybe_unused]] bool is_input) {
    if (PanicOccurred()) return 0;

    try {
        constexpr String k_func = "audio_ports.count";
        if (!Check(plugin, k_func, "plugin is null")) return 0;
        LogClapFunction(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func, "is_input: {}", is_input);

        return 1;
    } catch (PanicException) {
        return 0;
    }

    return 1;
}

static bool
ClapAudioPortsGet(clap_plugin_t const* plugin, u32 index, bool is_input, clap_audio_port_info_t* info) {
    if (PanicOccurred()) return 0;

    try {
        constexpr String k_func = "audio_ports.get";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        auto& floe = GetFloe(plugin);
        LogClapFunction(floe,
                        ClapLoggingLevel::AllNonRealtime,
                        "audio_ports.get",
                        "index: {}, is_input: {}",
                        index,
                        is_input);

        if (!Check(floe, index == 0, k_func, "index out of range")) return false;
        if (!Check(floe, info, k_func, "info is null")) return false;

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
    } catch (PanicException) {
        return false;
    }
}

clap_plugin_audio_ports const floe_audio_ports {
    .count = ClapAudioPortsCount,
    .get = ClapAudioPortsGet,
};

static constexpr clap_id k_main_note_port_id = 1; // never change this

static u32 ClapNotePortsCount(clap_plugin_t const* plugin, bool is_input) {
    if (PanicOccurred()) return 0;

    try {
        constexpr String k_func = "note_ports.count";
        if (!Check(plugin, k_func, "plugin is null")) return 0;
        LogClapFunction(GetFloe(plugin), ClapLoggingLevel::AllNonRealtime, k_func, "is_input: {}", is_input);

        return is_input ? 1 : 0;
    } catch (PanicException) {
        return 0;
    }

    return 0;
}

static bool
ClapNotePortsGet(clap_plugin_t const* plugin, u32 index, bool is_input, clap_note_port_info_t* info) {
    if (PanicOccurred()) return 0;

    try {
        constexpr String k_func = "note_ports.get";
        if (!Check(plugin, k_func, "plugin is null")) return false;
        auto& floe = GetFloe(plugin);
        LogClapApi(floe, ClapLoggingLevel::AllNonRealtime, k_func);

        if (!Check(floe, index == 0, k_func, "index out of range")) return false;
        if (!Check(floe, info, k_func, "info is null")) return false;
        if (!Check(floe, is_input, k_func, "output ports not supported")) return false;

        info->id = k_main_note_port_id;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
        CopyStringIntoBufferWithNullTerm(info->name, "Notes In");
        return true;
    } catch (PanicException) {
        return false;
    }
}

// The note ports scan has to be done while the plugin is deactivated.
clap_plugin_note_ports const floe_note_ports {
    .count = ClapNotePortsCount,
    .get = ClapNotePortsGet,
};

static void ClapThreadPoolExec(clap_plugin_t const* plugin, u32 task_index) {
    if (PanicOccurred()) return;

    try {
        if (!plugin) return;

        auto& floe = *(FloePluginInstance*)plugin->plugin_data;
        TRACE_CLAP_CALL("thread_pool.exec");

        floe.engine->processor.processor_callbacks.on_thread_pool_exec(floe.engine->processor, task_index);
    } catch (PanicException) {
    }
}

clap_plugin_thread_pool const floe_thread_pool {
    .exec = ClapThreadPoolExec,
};

// NOLINTNEXTLINE(cppcoreguidelines-interfaces-global-init): clang-tidy thinks g_log is initialised
clap_plugin_timer_support const floe_timer {
    .on_timer =
        [](clap_plugin_t const* plugin, clap_id timer_id) {
            if (PanicOccurred()) return;

            try {
                ASSERT(plugin);
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!IsMainThread(floe.host)) {
                    LogWarning(k_clap_log_module, "host misbehaving: on_timer() not on main thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.engine) {
                    LogWarning(k_clap_log_module, "host misbehaving: on_timer() called before init()");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
                ASSERT(plugin);
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;

                if (!IsMainThread(floe.host)) {
                    LogWarning(k_clap_log_module, "host misbehaving: on_fd() not on main thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.engine) {
                    LogWarning(k_clap_log_module, "host misbehaving: on_fd() called before init()");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
            ASSERT(plugin);
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
            LogDebug(k_clap_log_module, "init plugin");
            ASSERT(plugin);

            auto& floe = *(FloePluginInstance*)plugin->plugin_data;

            if (floe.initialised) {
                LogWarning(k_clap_log_module, "host misbehaving: init() called twice");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                return false;
            }

            if (auto const thread_check =
                    (clap_host_thread_check const*)floe.host.get_extension(&floe.host, CLAP_EXT_THREAD_CHECK);
                thread_check && !thread_check->is_main_thread(&floe.host)) {
                LogWarning(k_clap_log_module, "host misbehaving: init() not on main thread");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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

                LogInfo(k_main_log_module,
                        "host: {} {} {}",
                        floe.host.vendor,
                        floe.host.name,
                        floe.host.version);

                ReportError(sentry::Error::Level::Info, "Floe plugin loaded"_s);
            }

            LogDebug(k_clap_log_module, "#{} init", floe.index);

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
                ASSERT(plugin);

                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin destroy (init:{})", floe.initialised);

                if (floe.initialised) {
                    if (!IsMainThread(floe.host)) {
                        LogWarning(k_clap_log_module, "host misbehaving: destroy() not on main thread");
                        ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                        return;
                    }

                    if (floe.gui_platform) {
                        LogWarning(k_clap_log_module,
                                   "host misbehaving: destroy() called while gui is active");
                        ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                        floe_gui.destroy(plugin);
                    }

                    if (floe.active) {
                        LogWarning(k_clap_log_module, "host misbehaving: destroy() called while active");
                        ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                        floe_plugin.deactivate(plugin);
                    }

                    // IMPORTANT: engine is cleared after
                    g_shared_engine_systems->UnregisterFloeInstance(floe.index);

                    floe.engine.Clear();

                    ASSERT(g_num_initialised_plugins);
                    if (--g_num_initialised_plugins == 0) g_shared_engine_systems.Clear();
                }

                LogDebug(k_clap_log_module, "#{} destroy", floe.index);

                --g_num_floe_instances;
                FloeInstanceAllocator().Delete(&floe);
            } catch (PanicException) {
            }
        },

    .activate =
        [](clap_plugin const* plugin, f64 sample_rate, u32 min_frames_count, u32 max_frames_count) -> bool {
        if (PanicOccurred()) return false;

        try {
            ASSERT(plugin);
            auto& floe = *(FloePluginInstance*)plugin->plugin_data;
            ZoneScopedMessage(floe.trace_config, "plugin activate");

            if (!IsMainThread(floe.host)) {
                LogWarning(k_clap_log_module, "host misbehaving: activate() not on main thread");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                return false;
            }

            if (floe.active) {
                LogWarning(k_clap_log_module, "host misbehaving: activate() when already active");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
                ASSERT(plugin);
                auto& floe = *(FloePluginInstance*)plugin->plugin_data;
                ZoneScopedMessage(floe.trace_config, "plugin activate");

                if (!IsMainThread(floe.host)) {
                    LogWarning(k_clap_log_module, "host misbehaving: deactivate() not on main thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.active) {
                    LogWarning(k_clap_log_module, "host misbehaving: deactivate() when not active");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
                LogWarning(k_clap_log_module, "host misbehaving: start_processing() not on audio thread");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                return false;
            }

            if (!floe.active) {
                LogWarning(k_clap_log_module, "host misbehaving: start_processing() when not active");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                return false;
            }

            if (floe.processing) {
                LogWarning(k_clap_log_module, "host misbehaving: start_processing() when already processing");
                ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
                    LogWarning(k_clap_log_module, "host misbehaving: stop_processing() not on audio thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.active) {
                    LogWarning(k_clap_log_module, "host misbehaving: stop_processing() when not active");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.processing) {
                    LogWarning(k_clap_log_module, "host misbehaving: stop_processing() when not processing");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
                    LogWarning(k_clap_log_module, "host misbehaving: reset() not on audio thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
                    return;
                }

                if (!floe.active) {
                    LogWarning(k_clap_log_module, "host misbehaving: reset() when not active");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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

            ASSERT(CheckInputEvents(process->in_events));

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
                    LogWarning(k_clap_log_module, "host misbehaving: on_main_thread() not on main thread");
                    ASSERT(g_final_binary_type != FinalBinaryType::Standalone);
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
    Trace(k_main_log_module);
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
