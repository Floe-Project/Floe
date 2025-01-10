// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui.hpp"

#include <IconsFontAwesome5.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/logger/logger.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui/gui2_attribution_panel.hpp"
#include "gui/gui2_info_panel.hpp"
#include "gui/gui2_notifications.hpp"
#include "gui/gui2_package_install.hpp"
#include "gui/gui2_settings_panel.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_editors.hpp"
#include "gui_framework/draw_list.hpp"
#include "gui_framework/gui_imgui.hpp"
#include "gui_framework/gui_live_edit.hpp"
#include "gui_modal_windows.hpp"
#include "gui_widget_helpers.hpp"
#include "image.hpp"
#include "plugin/plugin.hpp"
#include "sample_lib_server/sample_library_server.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"
#include "state/state_coding.hpp"

static f32 PixelsPerPoint(Gui* g) {
    constexpr auto k_points_in_width = 1000.0f; // 1000 just because it's easy to work with
    return (f32)g->settings.settings.gui.window_width / k_points_in_width;
}

enum class LibraryImageType { Icon, Background };

static String FilenameForLibraryImageType(LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return "icon.png";
        case LibraryImageType::Background: return "background.jpg";
    }
    PanicIfReached();
    return {};
}

static Optional<sample_lib::LibraryPath> PathInLibraryForImageType(sample_lib::Library const& lib,
                                                                   LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return lib.icon_image_path;
        case LibraryImageType::Background: return lib.background_image_path;
    }
    PanicIfReached();
    return {};
}

Optional<ImageBytesManaged>
ImagePixelsFromLibrary(Gui* g, sample_lib::Library const& lib, LibraryImageType type) {
    auto const filename = FilenameForLibraryImageType(type);

    if (lib.file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        // Back in the Mirage days, some libraries didn't embed their own images, but instead got them from a
        // shared pool. We replicate that behaviour here.
        auto mirage_compat_lib =
            sample_lib_server::FindLibraryRetained(g->shared_engine_systems.sample_library_server,
                                                   sample_lib::k_mirage_compat_library_id);
        DEFER { mirage_compat_lib.Release(); };

        if (mirage_compat_lib) {
            if (auto const dir = path::Directory(mirage_compat_lib->path); dir) {
                String const library_subdir = lib.name == "Wraith Demo" ? "Wraith" : lib.name;
                auto const path =
                    path::Join(g->scratch_arena, Array {*dir, "images"_s, library_subdir, filename});
                auto outcome = DecodeImageFromFile(path);
                if (outcome.HasValue()) return outcome.ReleaseValue();
            }
        }
    }

    auto const path_in_lib = PathInLibraryForImageType(lib, type);

    auto err = [&](String middle, LogLevel severity) {
        g_log.Log(k_gui_log_module, severity, "{} {} {}", lib.name, middle, filename);
        return Optional<ImageBytesManaged> {};
    };

    if (!path_in_lib) return err("does not have", LogLevel::Debug);

    auto open_outcome = lib.create_file_reader(lib, *path_in_lib);
    if (open_outcome.HasError()) return err("error opening", LogLevel::Warning);

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const file_outcome = open_outcome.Value().ReadOrFetchAll(arena);
    if (file_outcome.HasError()) return err("error reading", LogLevel::Warning);

    auto image_outcome = DecodeImage(file_outcome.Value());
    if (image_outcome.HasError()) return err("error decoding", LogLevel::Warning);

    return image_outcome.ReleaseValue();
}

static graphics::ImageID CopyPixelsToGpuLoadedImage(Gui* g, ImageBytesManaged const& px) {
    ASSERT(px.rgba);
    auto const outcome = g->frame_input.graphics_ctx->CreateImageID(px.rgba, px.size, 4);
    if (outcome.HasError()) {
        g_log.Error(k_gui_log_module,
                    "Failed to create a texture (size {}x{}): {}",
                    px.size.width,
                    px.size.height,
                    outcome.Error());
        return {};
    }
    return outcome.Value();
}

static Optional<graphics::ImageID> TryCreateImageOnGpu(graphics::DrawContext& ctx, ImageBytes const image) {
    return ctx.CreateImageID(image.rgba, image.size, k_rgba_channels).OrElse([](ErrorCode error) {
        g_log.Error(k_gui_log_module, "Failed to create image texture: {}", error);
        return graphics::ImageID {};
    });
}

static void CreateLibraryBackgroundImageTextures(Gui* g,
                                                 LibraryImages& imgs,
                                                 ImageBytesManaged const& background_image,
                                                 bool reload_background,
                                                 bool reload_blurred_background) {
    ArenaAllocator arena {PageAllocator::Instance()};

    // If the image is quite a lot larger than we need, resize it down to avoid storing a huge image on the
    // GPU
    auto const scaled_background =
        ShrinkImageIfNeeded(background_image,
                            CheckedCast<u16>(g->frame_input.window_size.width * 1.3f),
                            g->frame_input.window_size.width,
                            arena,
                            false);
    if (reload_background)
        imgs.background = TryCreateImageOnGpu(*g->frame_input.graphics_ctx, scaled_background);

    if (reload_blurred_background) {
        imgs.blurred_background = TryCreateImageOnGpu(
            *g->frame_input.graphics_ctx,
            CreateBlurredLibraryBackground(
                scaled_background,
                arena,
                {
                    .downscale_factor =
                        Clamp01(LiveSize(g->imgui, UiSizeId::BackgroundBlurringDownscaleFactor) / 100.0f),
                    .brightness_scaling_exponent =
                        LiveSize(g->imgui, UiSizeId::BackgroundBlurringBrightnessExponent) / 100.0f,
                    .overlay_value =
                        Clamp01(LiveSize(g->imgui, UiSizeId::BackgroundBlurringOverlayColour) / 100.0f),
                    .overlay_alpha =
                        Clamp01(LiveSize(g->imgui, UiSizeId::BackgroundBlurringOverlayIntensity) / 100.0f),
                    .blur1_radius_percent = LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlur1Radius) / 100,
                    .blur2_radius_percent = LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlur2Radius) / 100,
                    .blur2_alpha =
                        Clamp01(LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlur2Alpha) / 100.0f),
                }));
    }
}

static LibraryImages& FindOrCreateLibraryImages(Gui* g, sample_lib::LibraryIdRef library_id) {
    auto opt_index =
        FindIf(g->library_images, [&](LibraryImages const& l) { return l.library_id == library_id; });
    if (opt_index) return g->library_images[*opt_index];

    dyn::Append(g->library_images, {library_id});
    return g->library_images[g->library_images.size - 1];
}

struct CheckLibraryImagesResult {
    bool reload_icon = false;
    bool reload_background = false;
    bool reload_blurred_background = false;
};

static CheckLibraryImagesResult CheckLibraryImages(Gui* g, LibraryImages& images) {
    auto& ctx = g->frame_input.graphics_ctx;
    CheckLibraryImagesResult result {};

    if (Exchange(images.reload, false)) {
        if (images.icon) ctx->DestroyImageID(*images.icon);
        if (images.background) ctx->DestroyImageID(*images.background);
        if (images.blurred_background) ctx->DestroyImageID(*images.blurred_background);
        result.reload_icon = true;
        result.reload_background = true;
        result.reload_blurred_background = true;
        return result;
    }

    if (!ctx->ImageIdIsValid(images.icon) && !images.icon_missing) result.reload_icon = true;
    if (!ctx->ImageIdIsValid(images.background) && !images.background_missing)
        result.reload_background = true;
    if (!ctx->ImageIdIsValid(images.blurred_background) && !images.background_missing)
        result.reload_blurred_background = true;

    return result;
}

static LibraryImages LoadDefaultLibraryImagesIfNeeded(Gui* g) {
    auto& images = FindOrCreateLibraryImages(g, k_default_background_lib_id);
    auto const reloads = CheckLibraryImages(g, images);

    if (reloads.reload_background || reloads.reload_blurred_background) {
        auto image_data = EmbeddedDefaultBackground();
        auto outcome = DecodeImage({image_data.data, image_data.size});
        ASSERT(!outcome.HasError());
        auto const bg_pixels = outcome.ReleaseValue();
        CreateLibraryBackgroundImageTextures(g,
                                             images,
                                             bg_pixels,
                                             reloads.reload_background,
                                             reloads.reload_blurred_background);
    }

    return images;
}

static LibraryImages LoadLibraryImagesIfNeeded(Gui* g, sample_lib::Library const& lib) {
    auto& images = FindOrCreateLibraryImages(g, lib.Id());
    auto const reloads = CheckLibraryImages(g, images);

    if (reloads.reload_icon) {
        if (auto icon_pixels = ImagePixelsFromLibrary(g, lib, LibraryImageType::Icon))
            images.icon = CopyPixelsToGpuLoadedImage(g, icon_pixels.Value());
        else
            images.icon_missing = true;
    }

    if (reloads.reload_background || reloads.reload_blurred_background) {
        ImageBytesManaged const bg_pixels = ({
            Optional<ImageBytesManaged> opt = ImagePixelsFromLibrary(g, lib, LibraryImageType::Background);
            if (!opt) {
                images.background_missing = true;
                return images;
            }
            opt.ReleaseValue();
        });

        CreateLibraryBackgroundImageTextures(g,
                                             images,
                                             bg_pixels,
                                             reloads.reload_background,
                                             reloads.reload_blurred_background);
    }

    return images;
}

Optional<LibraryImages> LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id) {
    if (library_id == k_default_background_lib_id) return LoadDefaultLibraryImagesIfNeeded(g);

    auto background_lib =
        sample_lib_server::FindLibraryRetained(g->shared_engine_systems.sample_library_server, library_id);
    DEFER { background_lib.Release(); };
    if (!background_lib) return k_nullopt;

    return LoadLibraryImagesIfNeeded(g, *background_lib);
}

static void SampleLibraryChanged(Gui* g, sample_lib::LibraryIdRef library_id) {
    auto opt_index =
        FindIf(g->library_images, [&](LibraryImages const& l) { return l.library_id == library_id; });
    if (opt_index) {
        auto& ctx = g->frame_input.graphics_ctx;
        auto& imgs = g->library_images[*opt_index];
        imgs.icon_missing = false;
        imgs.background_missing = false;
        if (imgs.icon) ctx->DestroyImageID(*imgs.icon);
        if (imgs.background) ctx->DestroyImageID(*imgs.background);
        if (imgs.blurred_background) ctx->DestroyImageID(*imgs.blurred_background);
    }
}

static void CreateFontsIfNeeded(Gui* g) {
    g->imgui.SetPixelsPerPoint(PixelsPerPoint(g));

    //
    // Fonts
    //
    auto& graphics_ctx = g->frame_input.graphics_ctx;

    if (graphics_ctx->fonts.tex_id == nullptr) {
        graphics_ctx->fonts.Clear();

        LoadFonts(*graphics_ctx, g->fonts);

        auto const fira_sans_size = g->imgui.PointsToPixels(16);
        auto const roboto_small_size = g->imgui.PointsToPixels(16);
        auto const mada_big_size = g->imgui.PointsToPixels(23);
        auto const mada_size = g->imgui.PointsToPixels(18);

        auto const def_ranges = graphics_ctx->fonts.GetGlyphRangesDefaultAudioPlugin();

        auto const load_font = [&](Span<u8 const> data, f32 size, Span<graphics::GlyphRange const> ranges) {
            graphics::FontConfig config {};
            config.font_data_reference_only = true;
            auto font = graphics_ctx->fonts.AddFontFromMemoryTTF((void*)data.data,
                                                                 (int)data.size,
                                                                 size * g->frame_input.draw_scale_factor,
                                                                 &config,
                                                                 ranges);
            ASSERT(font != nullptr);
            font->font_size_no_scale = size;

            return font;
        };

        {
            auto const data = EmbeddedFontAwesome();
            // IMPROVE: don't load all icons
            g->icons = load_font({data.data, data.size},
                                 mada_size,
                                 Array {graphics::GlyphRange {ICON_MIN_FA, ICON_MAX_FA}});
            g->icons->font_size_no_scale = mada_size;
            ASSERT(g->icons != nullptr);
        }

        {
            auto const data = EmbeddedFiraSans();
            g->fira_sans = load_font({data.data, data.size}, fira_sans_size, def_ranges);
        }

        {
            auto const data = EmbeddedRoboto();
            g->roboto_small = load_font({data.data, data.size}, roboto_small_size, def_ranges);
        }

        {
            auto const data = EmbeddedMada();
            g->mada = load_font({data.data, data.size}, mada_size, def_ranges);
            g->mada_big = load_font({data.data, data.size}, mada_big_size, def_ranges);
        }

        auto const outcome = graphics_ctx->CreateFontTexture();
        if (outcome.HasError())
            g_log.Error(k_gui_log_module, "Failed to create font texture: {}", outcome.Error());
    }
}

static ErrorCodeOr<void> OpenDialog(Gui* g, DialogType type) {
    switch (type) {
        case DialogType::AddNewLibraryScanFolder: {
            Optional<String> default_folder {};
            if (auto extra_paths =
                    g->settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Libraries)];
                extra_paths.size)
                default_folder = extra_paths[0];

            auto const paths = TRY(FilesystemDialog({
                .type = DialogArguments::Type::SelectFolder,
                .allocator = g->scratch_arena,
                .title = "Select Floe Library Folder",
                .default_path = default_folder,
                .filters = {},
                .parent_window = g->frame_input.native_window,
            }));
            if (paths.size) {
                auto const path = paths[0];
                filesystem_settings::AddScanFolder(g->settings, ScanFolderType::Libraries, path);
            }
            break;
        }
        case DialogType::AddNewPresetsScanFolder: {
            Optional<String> default_folder {};
            if (auto extra_paths =
                    g->settings.settings.filesystem.extra_scan_folders[ToInt(ScanFolderType::Presets)];
                extra_paths.size)
                default_folder = extra_paths[0];

            auto const paths = TRY(FilesystemDialog({
                .type = DialogArguments::Type::SelectFolder,
                .allocator = g->scratch_arena,
                .title = "Select Floe Presets Folder",
                .default_path = default_folder,
                .filters = {},
                .parent_window = g->frame_input.native_window,
            }));
            if (paths.size) {
                auto const path = paths[0];
                filesystem_settings::AddScanFolder(g->settings, ScanFolderType::Presets, path);
            }
            break;
        }
        case DialogType::LoadPreset:
        case DialogType::SavePreset: {
            Optional<String> default_path {};
            auto& preset_scan_folders = g->shared_engine_systems.settings.settings.filesystem
                                            .extra_scan_folders[ToInt(ScanFolderType::Presets)];
            if (preset_scan_folders.size) {
                default_path =
                    path::Join(g->scratch_arena,
                               Array {preset_scan_folders[0], "untitled" FLOE_PRESET_FILE_EXTENSION});
            }

            constexpr auto k_filters = ArrayT<DialogArguments::FileFilter>({{
                .description = "Floe Preset"_s,
                .wildcard_filter = "*.floe-*"_s,
            }});

            if (type == DialogType::LoadPreset) {
                auto const paths = TRY(FilesystemDialog({
                    .type = DialogArguments::Type::OpenFile,
                    .allocator = g->scratch_arena,
                    .title = "Load Floe Preset",
                    .default_path = default_path,
                    .filters = k_filters.Items(),
                    .parent_window = g->frame_input.native_window,
                }));
                if (paths.size) LoadPresetFromFile(g->engine, paths[0]);
            } else if (type == DialogType::SavePreset) {
                auto const paths = TRY(FilesystemDialog({
                    .type = DialogArguments::Type::SaveFile,
                    .allocator = g->scratch_arena,
                    .title = "Save Floe Preset",
                    .default_path = default_path,
                    .filters = k_filters.Items(),
                    .parent_window = g->frame_input.native_window,
                }));
                if (paths.size) SaveCurrentStateToFile(g->engine, paths[0]);
            } else {
                PanicIfReached();
            }
            break;
        }
    }

    return k_success;
}

void Gui::OpenDialog(DialogType type) {
    auto const outcome = ::OpenDialog(this, type);
    if (outcome.HasError()) g_log.Error(k_gui_log_module, "Failed to create dialog: {}", outcome.Error());
}

Gui::Gui(GuiFrameInput& frame_input, Engine& engine)
    : frame_input(frame_input)
    , engine(engine)
    , shared_engine_systems(engine.shared_engine_systems)
    , settings(engine.shared_engine_systems.settings)
    , imgui(frame_input, frame_output)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          engine.shared_engine_systems.sample_library_server,
          {
              .error_notifications = engine.error_notifications,
              .result_added_callback = []() {},
              .library_changed_callback =
                  [gui = this](sample_lib::LibraryIdRef library_id_ref) {
                      sample_lib::LibraryId lib_id {library_id_ref};
                      gui->main_thread_callbacks.Push([gui, lib_id]() { SampleLibraryChanged(gui, lib_id); });
                      gui->frame_input.request_update.Store(true, StoreMemoryOrder::Relaxed);
                  },
          })) {
    g_log_file.Trace(k_gui_log_module);

    editor.imgui = &imgui;
    imgui.user_callback_data = this;

    layout::ReserveItemsCapacity(layout, 2048);
}

Gui::~Gui() {
    layout::DestroyContext(layout);
    sample_lib_server::CloseAsyncCommsChannel(engine.shared_engine_systems.sample_library_server,
                                              sample_lib_server_async_channel);
    g_log_file.Trace(k_gui_log_module);
    if (midi_keyboard_note_held_with_mouse) {
        engine.processor.events_for_audio_thread.Push(
            GuiNoteClickReleased {.key = midi_keyboard_note_held_with_mouse.Value()});
        engine.host.request_process(&engine.host);
    }
}

bool Tooltip(Gui* g, imgui::Id id, Rect r, char const* fmt, ...);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size) {
    auto const img_w = (f32)img.size.width;
    auto const img_h = (f32)img.size.height;
    auto const window_ratio = container_size.x / container_size.y;
    auto const image_ratio = img_w / img_h;

    f32x2 uv {1, 1};
    if (image_ratio > window_ratio)
        uv.x = window_ratio / image_ratio;
    else
        uv.y = image_ratio / window_ratio;
    return uv;
}

static void DoStandaloneErrorGUI(Gui* g) {
    ASSERT(!PRODUCTION_BUILD);

    auto& engine = g->engine;

    auto const host = engine.host;
    auto const floe_ext = (FloeClapExtensionHost const*)host.get_extension(&host, k_floe_clap_extension_id);
    if (!floe_ext) return;

    g->frame_input.graphics_ctx->PushFont(g->roboto_small);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };
    auto& imgui = g->imgui;
    auto platform = &g->frame_input;
    static bool error_window_open = true;

    bool const there_is_an_error =
        floe_ext->standalone_midi_device_error || floe_ext->standalone_audio_device_error;
    if (error_window_open && there_is_an_error) {
        auto settings = imgui::DefWindow();
        settings.flags |= imgui::WindowFlags_AutoHeight | imgui::WindowFlags_AutoWidth;
        imgui.BeginWindow(settings, {.xywh {0, 0, 200, 0}}, "StandaloneErrors");
        DEFER { imgui.EndWindow(); };
        f32 y_pos = 0;
        if (floe_ext->standalone_midi_device_error) {
            imgui.Text(imgui::DefText(), {.xywh {0, y_pos, 100, 20}}, "No MIDI input");
            y_pos += 20;
        }
        if (floe_ext->standalone_audio_device_error) {
            imgui.Text(imgui::DefText(), {.xywh {0, y_pos, 100, 20}}, "No audio devices");
            y_pos += 20;
        }
        if (imgui.Button(imgui::DefButton(), {.xywh {0, y_pos, 100, 20}}, imgui.GetID("closeErr"), "Close"))
            error_window_open = false;
    }
    if (floe_ext->standalone_midi_device_error) {
        imgui.frame_output.wants_keyboard_input = true;
        if (platform->Key(ModifierKey::Shift).is_down) {
            auto gen_midi_message = [&](bool on, u7 key) {
                if (on)
                    engine.processor.events_for_audio_thread.Push(
                        GuiNoteClicked({.key = key, .velocity = 0.7f}));
                else
                    engine.processor.events_for_audio_thread.Push(GuiNoteClickReleased({.key = key}));
            };

            struct Key {
                KeyCode key;
                u7 midi_key;
            };
            static Key const keys[] = {
                {KeyCode::LeftArrow, 60},
                {KeyCode::RightArrow, 63},
                {KeyCode::UpArrow, 80},
                {KeyCode::DownArrow, 45},
            };

            for (auto& i : keys) {
                if (platform->Key(i.key).presses.size) gen_midi_message(true, i.midi_key);
                if (platform->Key(i.key).releases.size) gen_midi_message(false, i.midi_key);
            }
        }
    }
}

static bool HasAnyErrorNotifications(Gui* g) {
    for (auto& err_notifications :
         Array {&g->engine.error_notifications, &g->shared_engine_systems.error_notifications}) {
        for (auto& error : err_notifications->items)
            if (error.TryScoped()) return true;
    }
    return false;
}

GuiFrameResult GuiUpdate(Gui* g) {
    ZoneScoped;
    ASSERT(IsMainThread(g->engine.host));

    g->frame_output = {};

    live_edit::g_high_contrast_gui = g->settings.settings.gui.high_contrast_gui; // IMRPOVE: hacky
    g->scratch_arena.ResetCursorAndConsolidateRegions();

    while (auto function = g->main_thread_callbacks.TryPop(g->scratch_arena))
        (*function)();

    CreateFontsIfNeeded(g);
    auto& imgui = g->imgui;
    imgui.SetPixelsPerPoint(PixelsPerPoint(g));

    if (!g->engine.attribution_text.size) g->attribution_panel_open = false;

    g->waveforms.StartFrame();
    DEFER { g->waveforms.EndFrame(*g->frame_input.graphics_ctx); };

    auto whole_window_sets = imgui::DefMainWindow();
    whole_window_sets.draw_routine_window_background = [&](IMGUI_DRAW_WINDOW_BG_ARGS_TYPES) {};
    imgui.Begin(whole_window_sets);

    g->frame_input.graphics_ctx->PushFont(g->fira_sans);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };

    auto& settings = g->settings.settings.gui;
    auto const top_h = LiveSize(imgui, UiSizeId::Top2Height);
    auto const bot_h = settings.show_keyboard ? gui_settings::KeyboardHeight(g->settings.settings.gui) : 0;
    auto const mid_h = (f32)g->frame_input.window_size.height - (top_h + bot_h);

    auto draw_top_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        auto const top = LiveCol(imgui, UiColMap::TopPanelBackTop);
        auto const bot = LiveCol(imgui, UiColMap::TopPanelBackBot);
        imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), top, top, bot, bot);
    };
    auto draw_mid_window = [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;

        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::MidPanelBack));

        if (!settings.high_contrast_gui) {
            auto overall_library = LibraryForOverallBackground(g->engine);
            if (overall_library) {
                auto imgs = LibraryImagesFromLibraryId(g, *overall_library);
                if (imgs->background) {
                    auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(*imgs->background);
                    if (tex) {
                        imgui.graphics->AddImage(*tex,
                                                 r.Min(),
                                                 r.Max(),
                                                 {0, 0},
                                                 GetMaxUVToMaintainAspectRatio(*imgs->background, r.size));
                    }
                }
            }
        }

        imgui.graphics->AddLine(r.TopLeft(), r.TopRight(), LiveCol(imgui, UiColMap::MidPanelTopLine));
    };
    auto draw_bot_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::BotPanelBack));
    };

    {
        auto mid_settings = imgui::DefWindow();
        mid_settings.pad_top_left = {};
        mid_settings.pad_bottom_right = {};
        mid_settings.draw_routine_window_background = draw_mid_window;
        mid_settings.flags = 0;

        auto mid_panel_r = Rect {.x = 0, .y = top_h, .w = imgui.Width(), .h = mid_h};
        imgui.BeginWindow(mid_settings, mid_panel_r, "MidPanel");
        MidPanel(g);
        imgui.EndWindow();

        PresetBrowser preset_browser {g, g->preset_browser_data, false};
        preset_browser.DoPresetBrowserPanel(mid_panel_r);
    }

    {
        auto sets = imgui::DefWindow();
        sets.draw_routine_window_background = draw_top_window;
        sets.pad_top_left = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadT)};
        sets.pad_bottom_right = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadB)};
        imgui.BeginWindow(sets, {.xywh {0, 0, imgui.Width(), top_h}}, "TopPanel");
        TopPanel(g);
        imgui.EndWindow();
    }

    if (settings.show_keyboard) {
        auto bot_settings = imgui::DefWindow();
        bot_settings.pad_top_left = {8, 8};
        bot_settings.pad_bottom_right = {8, 8};
        bot_settings.draw_routine_window_background = draw_bot_window;
        imgui.BeginWindow(bot_settings, {.xywh {0, top_h + mid_h, imgui.Width(), bot_h}}, "BotPanel");
        BotPanel(g);
        imgui.EndWindow();
    }

    if (!PRODUCTION_BUILD && NullTermStringsEqual(g->engine.host.name, k_floe_standalone_host_name))
        DoStandaloneErrorGUI(g);

    if (HasAnyErrorNotifications(g)) OpenModalIfNotAlready(imgui, ModalWindowType::LoadError);

    DoModalWindows(g);

    // GUI2 panels. This is the future.
    {
        GuiBoxSystem box_system {
            .arena = g->scratch_arena,
            .imgui = g->imgui,
            .fonts = g->fonts,
            .layout = g->layout,
        };
        box_system.show_tooltips = g->settings.settings.gui.show_tooltips;

        {
            SettingsPanelContext context {
                .settings = g->settings,
                .sample_lib_server = g->shared_engine_systems.sample_library_server,
                .package_install_jobs = g->engine.package_install_jobs,
                .thread_pool = g->shared_engine_systems.thread_pool,
            };

            DoSettingsPanel(box_system, context, g->settings_panel_state);
        }

        {
            InfoPanelContext context {
                .server = g->shared_engine_systems.sample_library_server,
                .voice_pool = g->engine.processor.voice_pool,
                .scratch_arena = g->scratch_arena,
                .libraries =
                    sample_lib_server::AllLibrariesRetained(g->shared_engine_systems.sample_library_server,
                                                            g->scratch_arena),
            };
            DEFER { sample_lib_server::ReleaseAll(context.libraries); };

            DoInfoPanel(box_system, context, g->info_panel_state);
        }

        {
            AttributionPanelContext context {
                .engine = g->engine,
            };

            DoAttributionPanel(box_system, context, g->attribution_panel_open);
        }

        DoNotifications(box_system, g->notifications);

        DoPackageInstallNotifications(box_system,
                                      g->engine.package_install_jobs,
                                      g->notifications,
                                      g->engine.error_notifications,
                                      g->shared_engine_systems.thread_pool);
    }

    DoWholeEditor(g);
    imgui.End(g->scratch_arena);

    auto outcome = WriteSettingsFileIfChanged(g->settings);
    if (outcome.HasError()) {
        auto item = g->engine.error_notifications.NewError();
        item->value = {
            .title = "Failed to save settings file"_s,
            .message = g->settings.paths.settings_write_path,
            .error_code = outcome.Error(),
            .id = U64FromChars("savesets"),
        };
        g->engine.error_notifications.AddOrUpdateError(item);
    }

    return g->frame_output;
}
