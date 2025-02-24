// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clap/plugin.h"

#include "foundation/foundation.hpp"
#include "utils/debug/debug.hpp"
#include "utils/debug/tracy_wrapped.hpp"

#include "common_infrastructure/descriptors/param_descriptors.hpp"
#include "common_infrastructure/error_reporting.hpp"
#include "common_infrastructure/preferences.hpp"

#include "clap/ext/audio-ports.h"
#include "clap/ext/note-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/posix-fd-support.h"
#include "clap/ext/state.h"
#include "clap/ext/timer-support.h"
#include "clap/host.h"
#include "clap/id.h"
#include "clap/process.h"
#include "engine/engine.hpp"
#include "engine/shared_engine_systems.hpp"
#include "gui/gui_prefs.hpp"
#include "gui_framework/gui_platform.hpp"
#include "plugin.hpp"
#include "processing_utils/scoped_denormals.hpp"
#include "processor/processor.hpp"

//
#include "os/undef_windows_macros.h"

[[clang::no_destroy]] Optional<SharedEngineSystems> g_shared_engine_systems {};

// Logging is non-realtime only. We don't log in the audio thread.
// Some main-thread CLAP functions are called very frequently, so we only log them at a certain level.
enum class ClapFunctionType {
    NonRecurring,
    Any,
};
constexpr ClapFunctionType k_clap_logging_level = ClapFunctionType::NonRecurring;

// To make our CLAP interface bulletproof, we store a known index (based on a magic number) in the plugin_data
// and only access our corresponding object if it's valid. This is safer than the alternative of directly
// storing a pointer and dereferencing it without knowing for sure it's ours.
constexpr uintptr k_clap_plugin_data_magic = 0xF10E;

inline Optional<FloeInstanceIndex> IndexFromPluginData(uintptr plugin_data) {
    auto v = plugin_data;
    if (v < k_clap_plugin_data_magic) [[unlikely]]
        return k_nullopt;
    auto index = v - k_clap_plugin_data_magic;
    if (index >= k_max_num_floe_instances) [[unlikely]]
        return k_nullopt;
    return (FloeInstanceIndex)index;
}

inline void* PluginDataFromIndex(FloeInstanceIndex index) {
    return (void*)(k_clap_plugin_data_magic + index);
}

struct FloePluginInstance : PluginInstanceMessages {
    FloePluginInstance(clap_host const& host,
                       FloeInstanceIndex index,
                       clap_plugin const& plugin_interface_template)
        : host(host)
        , index(index) {
        Trace(ModuleName::Main);
        clap_plugin = plugin_interface_template;
        clap_plugin.plugin_data = PluginDataFromIndex(index);
    }
    ~FloePluginInstance() { Trace(ModuleName::Gui); }

    clap_host const& host;
    FloeInstanceIndex const index;

    clap_plugin clap_plugin;

    bool initialised {false};
    bool active {false};
    bool processing {false};

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

static u16 g_floe_instances_initialised {};
static Array<FloePluginInstance*, k_max_num_floe_instances> g_floe_instances {};

// Macro because it declares a constructor/destructor object for timing start/end
#define TRACE_CLAP_CALL(name) ZoneScopedMessage(floe.trace_config, name)

inline void LogClapFunction(FloePluginInstance& floe, ClapFunctionType level, String name) {
    if (k_clap_logging_level >= level) LogInfo(ModuleName::Clap, "{} #{}", name, floe.index);
}

inline void
LogClapFunction(FloePluginInstance& floe, ClapFunctionType level, String name, String format, auto... args) {
    if (k_clap_logging_level >= level) {
        ArenaAllocatorWithInlineStorage<400> arena {PageAllocator::Instance()};
        LogInfo(ModuleName::Clap, "{} #{}: {}", name, floe.index, fmt::Format(arena, format, args...));
    }
}

inline bool Check(FloePluginInstance& floe, bool condition, String function_name, String message) {
    if (!condition) [[unlikely]] {
        ReportError(sentry::Error::Level::Error,
                    HashMultiple(Array {function_name, message}),
                    "{} #{}: {}",
                    function_name,
                    floe.index,
                    message);
    }
    return condition;
}

inline bool Check(bool condition, String function_name, String message) {
    if (!condition) [[unlikely]] {
        ReportError(sentry::Error::Level::Error,
                    HashMultiple(Array {function_name, message}),
                    "{}: {}",
                    function_name,
                    message);
    }
    return condition;
}

static FloePluginInstance* ExtractFloe(clap_plugin const* plugin) {
    if (!plugin) [[unlikely]]
        return nullptr;
    auto index = IndexFromPluginData((uintptr)plugin->plugin_data);
    if (!index) [[unlikely]]
        return nullptr;
    return g_floe_instances[*index];
}

bool ClapStateSave(clap_plugin const* plugin, clap_ostream const* stream) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "state.save";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
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
        ReportError(sentry::Error::Level::Warning, Hash(name), name);
        return false;
    }
    return true;
}

static bool ClapGuiIsApiSupported(clap_plugin_t const* plugin, char const* api, bool is_floating) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.is_api_supported";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });

        if (!Check(api, k_func, "api is null")) return false;
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "api: {}, is_floating: {}", api, is_floating);

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);

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
        if (!Check(api, k_func, "api is null")) return false;

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe,
                        ClapFunctionType::NonRecurring,
                        k_func,
                        "api: {}, is_floating: {}",
                        api,
                        is_floating);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe,
                   NullTermStringsEqual(k_supported_gui_api, api) && !is_floating,
                   k_func,
                   "unsupported api"))
            return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;
        if (!Check(floe, !floe.gui_platform, k_func, "already created gui")) return false;

        floe.gui_platform.Emplace(floe.host, g_shared_engine_systems->prefs);
        return LogIfError(CreateView(*floe.gui_platform), "CreateView");
    } catch (PanicException) {
        return false;
    }
}

static void ClapGuiDestroy(clap_plugin const* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "gui.destroy";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func, "scale: {}", scale);
    } catch (PanicException) {
    }

    return false; // we (pugl) negotiate this with the OS ourselves
}

static bool ClapGuiGetSize(clap_plugin_t const* plugin, u32* width, u32* height) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.get_size";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, width || height, k_func, "width and height both null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        auto const size = GetSize(*floe.gui_platform);

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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, hints, k_func, "hints is null")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

        hints->can_resize_vertically = true;
        hints->can_resize_horizontally = true;
        hints->preserve_aspect_ratio = true;
        auto const ratio = DesiredAspectRatio(g_shared_engine_systems->prefs);
        hints->aspect_ratio_width = ratio.width;
        hints->aspect_ratio_height = ratio.height;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static Optional<UiSize>
GetUsableSizeWithinDimensions(GuiPlatform& gui_platform, u32 clap_width, u32 clap_height) {
    clap_width = Clamp<u32>(clap_width, 1, k_largest_gui_size);
    clap_height = Clamp<u32>(clap_height, 1, k_largest_gui_size);

    auto const size = ClapPixelsToPhysicalPixels(gui_platform.view, clap_width, clap_height);
    if (!size) return k_nullopt;

    auto const aspect_ratio_conformed_size =
        NearestAspectRatioSizeInsideSize(*size, DesiredAspectRatio(g_shared_engine_systems->prefs));

    if (!aspect_ratio_conformed_size) return k_nullopt;
    if (aspect_ratio_conformed_size->width < k_min_gui_width) return k_nullopt;

    return PhysicalPixelsToClapPixels(gui_platform.view, *aspect_ratio_conformed_size);
}

static bool ClapGuiAdjustSize(clap_plugin_t const* plugin, u32* clap_width, u32* clap_height) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.adjust_size";
        if (!Check(clap_width && clap_height, k_func, "width or height is null")) return false;

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func, "{} x {}", *clap_width, *clap_height);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

        if (!floe.gui_platform || !floe.gui_platform->view) {
            // We've been called before we have the ability to check our scaling factor, we can still give a
            // reasonable result by getting the nearest aspect ratio size.

            auto const aspect_ratio_conformed_size =
                NearestAspectRatioSizeInsideSize32({*clap_width, *clap_height},
                                                   DesiredAspectRatio(g_shared_engine_systems->prefs));

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func, "{} x {}", clap_width, clap_height);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        auto const size = ClapPixelsToPhysicalPixels(floe.gui_platform->view, clap_width, clap_height);

        if (!Check(floe, size && size->width >= k_min_gui_width, k_func, "invalid size")) return false;
        if (!Check(floe,
                   IsAspectRatio(*size, DesiredAspectRatio(g_shared_engine_systems->prefs)),
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);

        return false; // we don't support floating windows
    } catch (PanicException) {
    }

    return false;
}

static void ClapGuiSuggestTitle(clap_plugin_t const* plugin, char const*) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "gui.set_transient";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);

        // we don't support floating windows

    } catch (PanicException) {
    }
}

static bool ClapGuiShow(clap_plugin_t const* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "gui.show";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;
        if (!Check(floe, floe.gui_platform.HasValue(), k_func, "no gui created")) return false;

        bool const result = LogIfError(SetVisible(*floe.gui_platform, true, *floe.engine), "SetVisible");
        if (result) {
            static bool shown_graphics_info = false;
            if (!shown_graphics_info) {
                shown_graphics_info = true;
                LogInfo(ModuleName::Gui,
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "index: {}", param_index);

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "id: {}", param_id);

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "id: {}, value: {}", param_id, value);

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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "id: {}", param_id);
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

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        if (!floe.active) LogClapFunction(floe, ClapFunctionType::Any, k_func, "num in: {}", in->size(in));
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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return 0;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "is_input: {}", is_input);

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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe,
                        ClapFunctionType::Any,
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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "is_input: {}", is_input);

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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);

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
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, "thread_pool.exec", "plugin ptr is invalid")) return;
            f;
        });
        TRACE_CLAP_CALL("thread_pool.exec");

        floe.engine->processor.processor_callbacks.on_thread_pool_exec(floe.engine->processor, task_index);
    } catch (PanicException) {
    }
}

clap_plugin_thread_pool const floe_thread_pool {
    .exec = ClapThreadPoolExec,
};

static void ClapTimerSupportOnTimer(clap_plugin_t const* plugin, clap_id timer_id) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "timer_support.on_timer";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return;

        // We don't care about the timer_id, we just want to poll.
        prefs::PollForExternalChanges(g_shared_engine_systems->prefs);

        if (floe.gui_platform) OnClapTimer(*floe.gui_platform, timer_id);
        if (floe.engine) EngineCallbacks().on_timer(*floe.engine, timer_id);
    } catch (PanicException) {
    }
}

clap_plugin_timer_support const floe_timer {
    .on_timer = ClapTimerSupportOnTimer,
};

static void ClapFdSupportOnFd(clap_plugin_t const* plugin, int fd, clap_posix_fd_flags_t) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "posix_fd_support.on_fd";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;
        if (!Check(floe, floe.initialised, k_func, "not initialised")) return;

        if (floe.gui_platform) OnPosixFd(*floe.gui_platform, fd);
    } catch (PanicException) {
    }
}

clap_plugin_posix_fd_support const floe_posix_fd {
    .on_fd = ClapFdSupportOnFd,
};

FloeClapTestingExtension const floe_custom_ext {
    .state_change_is_pending = [](clap_plugin_t const* plugin) -> bool {
        if (PanicOccurred()) return false;

        try {
            auto& floe = *({
                auto f = ExtractFloe(plugin);
                if (!Check(f, "state_change_is_pending", "plugin ptr is invalid")) return false;
                f;
            });
            return floe.engine->pending_state_change.HasValue();
        } catch (PanicException) {
            return false;
        }
    },
};

static bool ClapInit(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "init";

        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        if (!Check(floe, floe.host.name && floe.host.name[0], k_func, "host name is null")) return false;
        if (!Check(floe, floe.host.version && floe.host.version[0], k_func, "host version is null"))
            return false;
        LogClapFunction(floe,
                        ClapFunctionType::NonRecurring,
                        k_func,
                        "{} {}",
                        floe.host.name,
                        floe.host.version);
        TRACE_CLAP_CALL(k_func);

        if (floe.initialised) return true;

        if (auto const thread_check =
                (clap_host_thread_check const*)floe.host.get_extension(&floe.host, CLAP_EXT_THREAD_CHECK);
            thread_check)
            if (!Check(floe, thread_check->is_main_thread(&floe.host), k_func, "not main thread"))
                return false;

        if (g_floe_instances_initialised++ == 0) {
            SetThreadName("main");

            DynamicArrayBounded<sentry::Tag, 4> tags {};
            {
                dyn::Append(tags, {"host_name"_s, FromNullTerminated(floe.host.name)});
                dyn::Append(tags, {"host_version"_s, FromNullTerminated(floe.host.version)});
                if (floe.host.vendor && floe.host.vendor[0])
                    dyn::Append(tags, {"host_vendor"_s, FromNullTerminated(floe.host.vendor)});
            }

            g_shared_engine_systems.Emplace(tags);

            LogInfo(ModuleName::Clap, "host: {} {} {}", floe.host.vendor, floe.host.name, floe.host.version);

            // TODO: remove this before release
            if constexpr (!PRODUCTION_BUILD)
                ReportError(sentry::Error::Level::Info, k_nullopt, "Floe plugin loaded"_s);
        } else {
            if (!Check(ThreadName() == "main", k_func, "multiple main threads")) return false;
        }

        floe.engine.Emplace(floe.host, *g_shared_engine_systems, floe);

        // IMPORTANT: engine is initialised first
        g_shared_engine_systems->RegisterFloeInstance(floe.index);

        floe.initialised = true;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static bool ClapActivate(const struct clap_plugin* plugin,
                         double sample_rate,
                         uint32_t min_frames_count,
                         uint32_t max_frames_count) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "activate";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, floe.initialised, k_func, "not initialised")) return false;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return false;

        if (floe.active) return true;

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
}

static void ClapDeactivate(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "deactivate";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, floe.initialised, k_func, "not initialised")) return;
        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;

        if (!floe.active) return;

        auto& processor = floe.engine->processor;
        processor.processor_callbacks.deactivate(processor);
        floe.active = false;
    } catch (PanicException) {
    }
}

static void ClapDestroy(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "destroy";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::NonRecurring, k_func);
        TRACE_CLAP_CALL(k_func);

        if (floe.initialised) {
            if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;

            // These shouldn't be necessary, but we can easily handle them so we do.
            if (floe.active) ClapDeactivate(plugin);
            if (floe.gui_platform) ClapGuiDestroy(plugin);

            // IMPORTANT: engine is cleared after unregistration.
            g_shared_engine_systems->UnregisterFloeInstance(floe.index);

            floe.engine.Clear();

            ASSERT(g_floe_instances_initialised != 0);
            if (--g_floe_instances_initialised == 0) g_shared_engine_systems.Clear();
        }

        auto const index = floe.index;
        FloeInstanceAllocator().Delete(&floe);
        g_floe_instances[index] = nullptr;
    } catch (PanicException) {
    }
}

static bool ClapStartProcessing(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return false;

    try {
        constexpr String k_func = "start_processing";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return false;
            f;
        });
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsAudioThread(floe.host) != IsAudioThreadResult::No, k_func, "not audio thread"))
            return false;

        if (!Check(floe, floe.active, k_func, "not active")) return false;

        if (floe.processing) return true;

        auto& processor = floe.engine->processor;
        processor.processor_callbacks.start_processing(processor);
        floe.processing = true;
        return true;
    } catch (PanicException) {
        return false;
    }
}

static void ClapStopProcessing(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "stop_processing";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsAudioThread(floe.host) != IsAudioThreadResult::No, k_func, "not audio thread"))
            return;
        if (!Check(floe, floe.active, k_func, "not active")) return;

        if (!floe.processing) return;

        auto& processor = floe.engine->processor;
        processor.processor_callbacks.stop_processing(processor);
        floe.processing = false;
    } catch (PanicException) {
    }
}

static void ClapReset(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "reset";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsAudioThread(floe.host) != IsAudioThreadResult::No, k_func, "not audio thread"))
            return;
        if (!Check(floe, floe.active, k_func, "not active")) return;

        auto& processor = floe.engine->processor;
        processor.processor_callbacks.reset(processor);
    } catch (PanicException) {
    }
}

static clap_process_status ClapProcess(const struct clap_plugin* plugin, clap_process_t const* process) {
    if (PanicOccurred()) return CLAP_PROCESS_ERROR;

    try {
        constexpr String k_func = "process";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return CLAP_PROCESS_ERROR;
            f;
        });
        TRACE_CLAP_CALL(k_func);

        ZoneKeyNum("instance", floe.index);
        ZoneKeyNum("events", process->in_events->size(process->in_events));
        ZoneKeyNum("num_frames", process->frames_count);

        if (!Check(floe, IsAudioThread(floe.host) != IsAudioThreadResult::No, k_func, "not audio thread"))
            return CLAP_PROCESS_ERROR;
        if (!Check(floe, floe.active, k_func, "not active")) return CLAP_PROCESS_ERROR;
        if (!Check(floe, floe.processing, k_func, "not processing")) return CLAP_PROCESS_ERROR;
        if (!Check(floe, process, k_func, "process is null")) return CLAP_PROCESS_ERROR;
        if (!Check(floe, CheckInputEvents(process->in_events), k_func, "invalid events"))
            return CLAP_PROCESS_ERROR;

        ScopedNoDenormals const no_denormals;
        auto& processor = floe.engine->processor;
        return processor.processor_callbacks.process(floe.engine->processor, *process);
    } catch (PanicException) {
        return CLAP_PROCESS_ERROR;
    }
}

static void const* ClapGetExtension(const struct clap_plugin* plugin, char const* id) {
    if (PanicOccurred()) return nullptr;

    try {
        constexpr String k_func = "get_extension";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return nullptr;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func, "id: {}", id);
        TRACE_CLAP_CALL(k_func);
        if (!Check(id, k_func, "id is null")) return nullptr;

        if (NullTermStringsEqual(id, CLAP_EXT_STATE)) return &floe_plugin_state;
        if (NullTermStringsEqual(id, CLAP_EXT_GUI)) return &floe_gui;
        if (NullTermStringsEqual(id, CLAP_EXT_PARAMS)) return &floe_params;
        if (NullTermStringsEqual(id, CLAP_EXT_NOTE_PORTS)) return &floe_note_ports;
        if (NullTermStringsEqual(id, CLAP_EXT_AUDIO_PORTS)) return &floe_audio_ports;
        if (NullTermStringsEqual(id, CLAP_EXT_THREAD_POOL)) return &floe_thread_pool;
        if (NullTermStringsEqual(id, CLAP_EXT_TIMER_SUPPORT)) return &floe_timer;
        if (NullTermStringsEqual(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &floe_posix_fd;
        if (NullTermStringsEqual(id, k_floe_clap_extension_id)) return &floe_custom_ext;
    } catch (PanicException) {
    }

    return nullptr;
}

static void ClapOnMainThread(const struct clap_plugin* plugin) {
    if (PanicOccurred()) return;

    try {
        constexpr String k_func = "on_main_thread";
        auto& floe = *({
            auto f = ExtractFloe(plugin);
            if (!Check(f, k_func, "plugin ptr is invalid")) return;
            f;
        });
        LogClapFunction(floe, ClapFunctionType::Any, k_func);
        TRACE_CLAP_CALL(k_func);

        if (!Check(floe, IsMainThread(floe.host), k_func, "not main thread")) return;

        if (floe.engine) {
            prefs::PollForExternalChanges(g_shared_engine_systems->prefs);

            auto& processor = floe.engine->processor;
            processor.processor_callbacks.on_main_thread(processor);
            EngineCallbacks().on_main_thread(*floe.engine);
        }
    } catch (PanicException) {
    }
}

clap_plugin const floe_plugin {
    .desc = &k_plugin_info,
    .plugin_data = nullptr,
    .init = ClapInit,
    .destroy = ClapDestroy,
    .activate = ClapActivate,
    .deactivate = ClapDeactivate,
    .start_processing = ClapStartProcessing,
    .stop_processing = ClapStopProcessing,
    .reset = ClapReset,
    .process = ClapProcess,
    .get_extension = ClapGetExtension,
    .on_main_thread = ClapOnMainThread,
};

clap_plugin const* CreateFloeInstance(clap_host const* host) {
    if (!Check(host, "create_plugin", "host is null")) return nullptr;
    Optional<FloeInstanceIndex> index {};
    for (auto [i, instance] : Enumerate<FloeInstanceIndex>(g_floe_instances)) {
        if (instance == nullptr) {
            index = i;
            break;
        }
    }
    if (!index) return nullptr;
    auto result = FloeInstanceAllocator().New<FloePluginInstance>(*host, *index, floe_plugin);
    if (!result) return nullptr;
    g_floe_instances[*index] = result;
    return &result->clap_plugin;
}

void OnPollThread(FloeInstanceIndex index) {
    if (PanicOccurred()) return;
    // We're on the polling thread, but we can be sure that the engine is active because our
    // Register/Unregister calls are correctly before/after.
    auto& floe = *g_floe_instances[index];
    ASSERT(floe.engine);
    EngineCallbacks().on_poll_thread(*floe.engine);
}

void OnPreferenceChanged(FloeInstanceIndex index, prefs::Key const& key, prefs::Value const* value) {
    if (PanicOccurred()) return;
    auto& floe = *g_floe_instances[index];
    ASSERT(IsMainThread(floe.host));
    ASSERT(floe.engine);

    if (floe.gui_platform) {
        if (auto const width = prefs::MatchInt(key, value, SettingDescriptor(GuiSetting::WindowWidth))) {
            auto const current_size = GetSize(*floe.gui_platform);
            if (current_size.width != *width) {
                if (auto const host_gui =
                        (clap_host_gui const*)floe.host.get_extension(&floe.host, CLAP_EXT_GUI)) {
                    auto const size =
                        PhysicalPixelsToClapPixels(floe.gui_platform->view,
                                                   DesiredWindowSize(g_shared_engine_systems->prefs));
                    host_gui->request_resize(&floe.host, size.width, size.height);
                }
            }
        } else if (auto const show_keyboard =
                       prefs::MatchBool(key, value, SettingDescriptor(GuiSetting::ShowKeyboard))) {
            if (auto const host_gui =
                    (clap_host_gui const*)floe.host.get_extension(&floe.host, CLAP_EXT_GUI)) {
                auto const size =
                    PhysicalPixelsToClapPixels(floe.gui_platform->view,
                                               DesiredWindowSize(g_shared_engine_systems->prefs));
                host_gui->resize_hints_changed(&floe.host);
                host_gui->request_resize(&floe.host, size.width, size.height);
            }
        }
    }

    EngineCallbacks().on_preference_changed(*floe.engine, key, value);
}
