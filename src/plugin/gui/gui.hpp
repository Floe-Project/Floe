// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#include "engine/engine.hpp"
#include "gui/gui2_info_panel_state.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui/gui2_settings_panel_state.hpp"
#include "gui/gui_modal_windows.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_envelope.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/fonts.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/layout.hpp"
#include "gui_layer.hpp"
#include "gui_preset_browser.hpp"
#include "settings/settings_file.hpp"

struct GuiFrameInput;

struct LibraryImages {
    sample_lib::LibraryId library_id {};
    Optional<graphics::ImageID> icon {};
    Optional<graphics::ImageID> background {};
    Optional<graphics::ImageID> blurred_background {};
    bool icon_missing {};
    bool background_missing {};
    bool reload {};
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
                                                       ArenaAllocator& scratch_arena,
                                                       WaveformAudioSource source,
                                                       f32 unscaled_width,
                                                       f32 unscaled_height,
                                                       f32 display_ratio) {
        UiSize const size {CheckedCast<u16>(unscaled_width * display_ratio),
                           CheckedCast<u16>(unscaled_height * display_ratio)};

        u64 source_hash = 0;
        switch (source.tag) {
            case WaveformAudioSourceType::AudioData: {
                auto const& audio_data = source.Get<AudioData const*>();
                source_hash = audio_data->hash;
                break;
            }
            case WaveformAudioSourceType::Sine:
            case WaveformAudioSourceType::WhiteNoise: {
                source_hash = (u64)source.tag + 1;
                break;
            }
        }

        for (auto& waveform : m_waveforms) {
            if (waveform.source_hash == source_hash && waveform.image_id.size == size) {
                auto tex = graphics.GetTextureFromImage(waveform.image_id);
                if (tex) {
                    waveform.used = true;
                    return *tex;
                }
            }
        }

        auto const cursor = scratch_arena.TotalUsed();
        DEFER { scratch_arena.TryShrinkTotalUsed(cursor); };

        Waveform waveform {};
        auto pixels = CreateWaveformImage(source, size, scratch_arena, scratch_arena);
        waveform.source_hash = source_hash;
        waveform.image_id = TRY(graphics.CreateImageID(pixels.data, size, 4));
        waveform.used = true;

        dyn::Append(m_waveforms, waveform);
        auto tex = graphics.GetTextureFromImage(waveform.image_id);
        ASSERT(tex);
        return *tex;
    }

    void StartFrame() {
        for (auto& waveform : m_waveforms)
            waveform.used = false;
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
        u64 source_hash {};
        graphics::ImageID image_id = graphics::k_invalid_image_id;
        bool used {};
    };

    DynamicArray<Waveform> m_waveforms {Malloc::Instance()};
};

enum class DialogType {
    AddNewLibraryScanFolder,
    AddNewPresetsScanFolder,
    SavePreset,
    LoadPreset,
};

struct Gui {
    Gui(GuiFrameInput& frame_input, Engine& engine);
    ~Gui();

    void OpenDialog(DialogType type);

    PageAllocator page_allocator;
    ArenaAllocator scratch_arena {page_allocator};

    bool show_purchasable_libraries = false;
    bool show_news = false;

    SettingsPanelState settings_panel_state {};
    InfoPanelState info_panel_state {};
    Notifications notifications {};

    GuiFrameInput& frame_input;
    GuiFrameResult frame_output;
    Engine& engine;
    SharedEngineSystems& shared_engine_systems;
    SettingsFile& settings;

    layout::Context layout = {};
    imgui::Context imgui {frame_input, frame_output};
    EditorGUI editor = {};
    graphics::Font* fira_sans {};
    graphics::Font* roboto_small {};
    graphics::Font* mada_big {};
    graphics::Font* mada {};
    graphics::Font* icons {};
    Fonts fonts {}; // new system
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

    ThreadsafeFunctionQueue main_thread_callbacks {.arena = {PageAllocator::Instance()}};
    sample_lib_server::AsyncCommsChannel& sample_lib_server_async_channel;
};

//
//
//

Optional<LibraryImages> LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id);

void GUIPresetLoaded(Gui* g, Engine* a, bool is_first_preset);
GuiFrameResult GuiUpdate(Gui* g);
void TopPanel(Gui* g);
void MidPanel(Gui* g);
void BotPanel(Gui* g);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size);
