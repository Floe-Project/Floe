// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "framework/draw_list.hpp"
#include "framework/gui_imgui.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_envelope.hpp"
#include "gui_layer.hpp"
#include "gui_preset_browser.hpp"
#include "layout.hpp"
#include "plugin_instance.hpp"
#include "settings/settings_file.hpp"

struct PluginInstance;
struct FloeInstance;
struct GuiFrameInput;

struct ImagePixelsRgba {
    NON_COPYABLE(ImagePixelsRgba);
    ~ImagePixelsRgba() {
        if (free_data && data) free_data(data);
    }
    ImagePixelsRgba() {}
    ImagePixelsRgba(ImagePixelsRgba&& other)
        : free_data(Move(other.free_data))
        , data(other.data)
        , size(other.size) {
        other.data = nullptr;
    }

    TrivialFixedSizeFunction<24, void(u8*)> free_data {};
    u8* data {};
    UiSize size {};
};

struct LibraryImages {
    sample_lib::LibraryId library_id {};
    Optional<graphics::ImageID> icon {};
    Optional<graphics::ImageID> background {};
    Optional<graphics::ImageID> blurred_background {};
    bool icon_missing {};
    bool background_missing {};
};

struct DraggingFX {
    imgui::Id id {};
    Effect* fx {};
    usize drop_slot {};
    f32x2 relative_grab_point {};
};

class FloeWaveformImages {
  public:
    ErrorCodeOr<graphics::TextureHandle> FetchOrCreate(graphics::DrawContext& graphics,
                                                       AudioData const& audio_file,
                                                       f32 unscaled_width,
                                                       f32 unscaled_height,
                                                       f32 display_ratio) {
        UiSize const size {CheckedCast<u16>(unscaled_width * display_ratio),
                           CheckedCast<u16>(unscaled_height * display_ratio)};

        for (auto& w : m_waveforms) {
            if (w.audio_data_hash == audio_file.hash && w.image_id.size == size) {
                auto tex = graphics.GetTextureFromImage(w.image_id);
                if (tex) {
                    w.used = true;
                    return *tex;
                }
            }
        }

        Waveform w {};
        auto pixels = GetWaveformImageFromSample(audio_file, size);
        w.audio_data_hash = audio_file.hash;
        w.image_id = TRY(graphics.CreateImageID(pixels.data, size, 4));
        w.used = true;

        dyn::Append(m_waveforms, w);
        auto tex = graphics.GetTextureFromImage(w.image_id);
        ASSERT(tex);
        return *tex;
    }

    void StartFrame() {
        for (auto& w : m_waveforms)
            w.used = false;
    }

    void EndFrame(graphics::DrawContext& graphics) {
        dyn::RemoveValueIf(m_waveforms, [&graphics](Waveform& w) {
            if (!w.used) {
                graphics.DestroyImageID(w.image_id);
                return true;
            }
            return false;
        });
    }

    void Clear() { dyn::Clear(m_waveforms); }

  private:
    struct Waveform {
        u64 audio_data_hash {};
        graphics::ImageID image_id = graphics::k_invalid_image_id;
        bool used {};
    };

    DynamicArray<Waveform> m_waveforms {Malloc::Instance()};
};

enum class DialogType { AddNewLibraryScanFolder, AddNewPresetsScanFolder, SavePreset, LoadPreset };

struct InstInfo {
    String title;
    String info;
};

struct Gui {
    Gui(GuiFrameInput& frame_input, PluginInstance& plugin);
    ~Gui();

    void OpenDialog(DialogType type);

    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};

    bool show_purchasable_libraries = false;
    bool show_news = false;
    u64 m_window_size_listener_id {};

    GuiFrameInput& frame_input;
    GuiFrameResult frame_output;
    PluginInstance& plugin;
    Logger& logger;
    SettingsFile& settings;

    Layout layout = {};
    imgui::Context imgui {frame_input, frame_output};
    EditorGUI editor = {};
    // TODO: really weird crash where this becomes null mid-frame
    // probably some undefined behaviour somewhere or a threading issue, I can't think what else would cause
    // it even weirder: if these font fields are moved the top of this struct the crash doesn't happen. Could
    // be UB in imgui::Context.
    graphics::Font* fira_sans {};
    graphics::Font* roboto_small {};
    graphics::Font* mada_big {};
    graphics::Font* mada {};
    graphics::Font* icons {};
    PresetBrowserPersistentData preset_browser_data {};

    layer_gui::LayerLayout layer_gui[k_num_layers] = {};

    FloeWaveformImages waveforms {};

    DynamicArray<LibraryImages> library_images {Malloc::Instance()};

    Optional<DraggingFX> dragging_fx_unit {};
    Optional<DraggingFX> dragging_fx_switch {};

    GuiEnvelopeCursor envelope_voice_cursors[ToInt(GuiEnvelopeType::Count)][k_num_voices] {};

    Optional<ParamIndex> param_text_editor_to_open {};

    Optional<u7> midi_keyboard_note_held_with_mouse = {};

    TimePoint redraw_counter = {};

    bool dynamics_slider_is_held {};

    ArenaAllocator inst_info_arena {page_allocator};
    String inst_info_title {};
    DynamicArray<InstInfo> inst_info {inst_info_arena};

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
};

//
//
//

graphics::ImageID CopyPixelsToGpuLoadedImage(Gui* g, ImagePixelsRgba const& px);
LibraryImages LoadDefaultBackgroundIfNeeded(Gui* g);
LibraryImages LoadLibraryBackgroundAndIconIfNeeded(Gui* g, sample_lib::Library const& lib);
Optional<LibraryImages> LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id);

void GUIPresetLoaded(Gui* g, PluginInstance* a, bool is_first_preset);
GuiFrameResult GuiUpdate(Gui* g);
void TopPanel(Gui* g);
void MidPanel(Gui* g);
void BotPanel(Gui* g);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size);
