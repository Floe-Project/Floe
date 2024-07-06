// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "os/filesystem.hpp"

#include "framework/gui_live_edit.hpp"
#include "gui.hpp"
#include "gui_editor_widgets.hpp"
#include "stb/stb_image_write.h"

static void DoProfileGUI(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto& plugin = g->plugin;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "AudioDebug");

    EditorReset(&g->editor);
    // NOTE: not really thread-safe to access plugin.processor
    auto const max_ms = (f32)plugin.processor.audio_processing_context.process_block_size_max /
                        plugin.processor.audio_processing_context.sample_rate * 1000.0f;
    EditorText(&g->editor,
               fmt::Format(g->scratch_arena,
                           "FS: {} Block: {} Max MS Allowed: {.3}",
                           plugin.processor.audio_processing_context.sample_rate,
                           plugin.processor.audio_processing_context.process_block_size_max,
                           max_ms));

    imgui.EndWindow();
}

static void DoAudioDebugPanel(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto& plugin = g->plugin;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "AudioDebug");

    EditorReset(&g->editor);

    EditorText(
        &g->editor,
        fmt::Format(g->scratch_arena, "Voices: {}", plugin.processor.voice_pool.num_active_voices.Load()));
    EditorText(&g->editor,
               fmt::Format(g->scratch_arena,
                           "Master Audio Processing: {}",
                           plugin.processor.fx_need_another_frame_of_processing));

    imgui.EndWindow();
}

static void DoGUIColourEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Colours");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    ColoursGUISliders(&g->editor, search);

    imgui.EndWindow();
}

static void DoGUIColourMapEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Colours");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    static EditorTextInputBuffer colour_search;
    EditorTextInput(&g->editor, "Colour Search:", colour_search);

    static bool show_high_contrast = false;
    if (EditorButton(&g->editor, "On", "Show High Contrast:")) show_high_contrast = !show_high_contrast;

    ColourMapGUIMenus(&g->editor, search, colour_search, show_high_contrast);

    imgui.EndWindow();
}

static void DoGUISizeEditor(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "GUI Sizes");
    EditorReset(&g->editor);

    static EditorTextInputBuffer search;
    EditorTextInput(&g->editor, "Search:", search);
    SizesGUISliders(&g->editor, search);

    imgui.EndWindow();
}

static bool g_show_editor = false;
static bool g_show_editor_on_left = true;
static char g_background_filepath[260];

static void TakeScreenshot(Gui* g) {
    for (auto wnd : g->imgui.windows) {
        if (wnd->name == "DebugR") {
            wnd->skip_drawing_this_frame = true;
            break;
        }
    }

    g->gui_platform.graphics_ctx->RequestScreenshotImage([g](u8 const* data, int width, int height) {
        auto path = DynamicArray<char>::FromOwnedSpan(
            KnownDirectoryWithSubdirectories(g->scratch_arena, KnownDirectories::Data, Array {"Floe"_s})
                .Value(),
            g->scratch_arena);
        dyn::AppendSpan(path, "/screenshot-");
        dyn::AppendSpan(path, "floe"_s);

        int num = 1;
        while (true) {
            auto const initial_size = path.size;
            fmt::Append(path, "-{}.jpg", num);
            ++num;

            if (auto o = GetFileType(path); o.HasValue() && o.Value() == FileType::RegularFile)
                dyn::Resize(path, initial_size);
            else
                break;
        }

        if (!stbi_write_jpg(dyn::NullTerminated(path), width, height, 3, data, 95)) PanicIfReached();
    });
}

static void DoCommandPanel(Gui* g, Rect r) {
    auto& imgui = g->imgui;
    auto sets = imgui::DefWindow();
    sets.flags = 0;
    imgui.BeginWindow(sets, r, "Commands");
    EditorReset(&g->editor);

    if (EditorButton(&g->editor, "Show Editor", "Show editor: F1")) g_show_editor = !g_show_editor;
    if (EditorButton(&g->editor, "Editor Left", "Editor position left: F2"))
        g_show_editor_on_left = !g_show_editor_on_left;

    if (EditorButton(&g->editor, "Take Screenshot", "Save Screenshot: F3")) TakeScreenshot(g);

    auto const filters = Array<DialogOptions::FileFilter, 2> {{{
                                                                   .description = "JPEG image"_s,
                                                                   .wildcard_filter = "*.ppg"_s,
                                                               },
                                                               {
                                                                   .description = "WEBP image",
                                                                   .wildcard_filter = "*.webp"_s,
                                                               }

    }};

    if (EditorButton(&g->editor, "Set Background Filepath", "Set the filepath for the background image")) {
        auto outcome = FilesystemDialog(DialogOptions {
            .type = DialogOptions::Type::OpenFile,
            .allocator = g->scratch_arena,
            .title = "Select background JPG",
            .default_path = nullopt,
            .filters = filters.Items(),
            .parent_window = g->gui_platform.GetWindow(),
        });
        if (outcome.HasValue()) {
            auto const opt_path = outcome.Value();
            if (opt_path) {
                CopyStringIntoBufferWithNullTerm(g_background_filepath, *opt_path);

                g->gui_platform.SetGUIDirty();
            }
        } else {
            PanicIfReached();
        }
    }
    if (EditorButton(&g->editor, "Remove Background Filepath", "Use the default background image"))
        g_background_filepath[0] = 0;

    imgui.EndWindow();
}

static void DoWholeEditor(Gui* g) {
    if constexpr (!FLOE_EDITOR_ENABLED) return;
    auto& imgui = g->imgui;

    g->gui_platform.gui_update_requirements.wants_keyboard_input = true; // for debug panel open/close

    if (g->gui_platform.KeyJustWentDown(KeyCodeF1)) g_show_editor = !g_show_editor;

    if (g_show_editor) {
        if (g->gui_platform.KeyJustWentDown(KeyCodeF2)) g_show_editor_on_left = !g_show_editor_on_left;
        g->gui_platform.graphics_ctx->PushDefaultFont();
        auto const half_w = (f32)(int)(imgui.Width() / 2);
        Rect debug_r;
        if (g_show_editor_on_left)
            debug_r = {half_w + 1, 0, half_w - 1, imgui.Height()};
        else
            debug_r = {0, 0, half_w - 1, imgui.Height()};
        imgui.BeginWindow(imgui::DefWindow(), debug_r, "DebugR");

        static String const tab_text[] = {
            "Commands",
            "Audio",
            "Colours",
            "ColMap",
            "Sizes",
            "GUI Dbg",
            "Profile",
            "Sampler",
        };
        static auto const num_tabs = (int)ArraySize(tab_text);
        static int selected_tab = 0;
        auto tab_h = imgui.graphics->context->CurrentFontSize() * 2;
        for (auto const i : Range(num_tabs)) {
            auto third = imgui.Width() / (f32)num_tabs;
            bool v = i == selected_tab;
            auto id = imgui.GetID(tab_text[i]);
            if (imgui.ToggleButton(imgui::DefToggleButton(),
                                   {(f32)i * third, 0, third, tab_h},
                                   id,
                                   v,
                                   tab_text[i])) {
                selected_tab = i;
            }
        }
        Rect const selected_r = {0, tab_h, imgui.Width(), imgui.Height() - tab_h};
        switch (selected_tab) {
            case 0: DoCommandPanel(g, selected_r); break;
            case 1: DoAudioDebugPanel(g, selected_r); break;
            case 2: DoGUIColourEditor(g, selected_r); break;
            case 3: DoGUIColourMapEditor(g, selected_r); break;
            case 4: DoGUISizeEditor(g, selected_r); break;
            case 5: imgui.DebugWindow(selected_r); break;
            case 6: DoProfileGUI(g, selected_r); break;
            default: PanicIfReached();
        }

        imgui.EndWindow();
        g->gui_platform.graphics_ctx->PopFont();
    }
}
