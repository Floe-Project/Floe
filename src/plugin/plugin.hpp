// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "os/threading.hpp"
#include "utils/logger/logger.hpp"

#include "clap/ext/gui.h"
#include "clap/ext/thread-check.h"
#include "clap/stream.h"
#include "config.h"

struct PluginActivateArgs {
    f64 sample_rate;
    u32 min_block_size;
    u32 max_block_size;
};

template <typename UserObject>
struct PluginCallbacks {
    // [main-thread & !active_state]
    bool (*activate)(UserObject&, PluginActivateArgs args) = [](UserObject&, PluginActivateArgs) {
        return true;
    };

    // [main-thread & active_state]
    void (*deactivate)(UserObject&) = [](UserObject&) {};

    // Call start processing before processing.
    // [audio-thread & active_state & !processing_state]
    bool (*start_processing)(UserObject&) = [](UserObject&) { return true; };

    // Call stop processing before sending the plugin to sleep.
    // [audio-thread & active_state & processing_state]
    void (*stop_processing)(UserObject&) = [](UserObject&) {};

    // - Clears all buffers, performs a full reset of the processing state (filters, oscillators,
    //   envelopes, lfo, ...) and kills all voices.
    // - The parameter's value remain unchanged.
    // - clap_process.steady_time may jump backward.
    //
    // [audio-thread & active_state]
    void (*reset)(UserObject&) = [](UserObject&) {};

    // process audio, events, ...
    // All the pointers coming from clap_process_t and its nested attributes,
    // are valid until process() returns.
    // [audio-thread & active_state & processing_state]
    clap_process_status (*process)(UserObject&, clap_process const& process) =
        [](UserObject&, clap_process const&) -> clap_process_status { return CLAP_PROCESS_SLEEP; };

    // Flushes a set of parameter changes.
    // This method must not be called concurrently to clap_plugin->process().
    //
    // Note: if the plugin is processing, then the process() call will already achieve the
    // parameter update (bi-directional), so a call to flush isn't required, also be aware
    // that the plugin may use the sample offset in process(), while this information would be
    // lost within flush().
    //
    // [active ? audio-thread : main-thread]
    void (*flush_parameter_events)(UserObject&, clap_input_events const& in, clap_output_events const& out) =
        [](UserObject&, clap_input_events const&, clap_output_events const&) {};

    // Called by the host on the main thread in response to a previous call to:
    //   host->request_callback(host);
    // [main-thread]
    void (*on_main_thread)(UserObject&, bool& update_gui) = [](UserObject&, bool&) {};

    // [main-thread]
    bool (*save_state)(UserObject&, clap_ostream const& stream) = [](UserObject&, clap_ostream const&) {
        return true;
    };

    // [main-thread]
    bool (*load_state)(UserObject&, clap_istream const& stream) = [](UserObject&, clap_istream const&) {
        return true;
    };
};

constexpr char const* k_supported_gui_api =
    IS_WINDOWS ? CLAP_WINDOW_API_WIN32 : (IS_MACOS ? CLAP_WINDOW_API_COCOA : CLAP_WINDOW_API_X11);

// We use the clap extension interface for our plugin and "host" (wrapper) to communicate to each other.
static constexpr char k_floe_clap_extension_id[] = "floe.floe";
static constexpr char k_floe_standalone_host_name[] = "Floe Standalone";

struct FloeClapExtensionHost {
    bool standalone_audio_device_error;
    bool standalone_midi_device_error;
    void* pugl_world;
};

struct GuiFrameInput;

constexpr auto k_clap_log_cat = "👏clap"_cat;

inline bool IsMainThread(clap_host const& host) {
    if constexpr (PRODUCTION_BUILD) return true;
    if (auto const thread_check =
            (clap_host_thread_check const*)host.get_extension(&host, CLAP_EXT_THREAD_CHECK);
        thread_check)
        return thread_check->is_main_thread(&host);
    else
        return IsMainThread();
}

static constexpr char const* k_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                             CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                             CLAP_PLUGIN_FEATURE_STEREO,
                                             nullptr};

static constexpr clap_plugin_descriptor k_plugin_info {
    .clap_version = CLAP_VERSION,
    .id = "com.Floe.Floe",
    .name = "Floe",
    .vendor = FLOE_VENDOR,
    .url = FLOE_HOMEPAGE_URL,
    .manual_url = FLOE_MANUAL_URL,
    .support_url = FLOE_MANUAL_URL,
    .version = FLOE_VERSION_STRING,
    .description = FLOE_DESCRIPTION,
    .features = (char const**)k_features,
};

clap_plugin const& CreatePlugin(clap_host const* clap_host);
