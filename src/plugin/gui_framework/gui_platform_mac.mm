// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect  MacRect
#define Delay MacDelay
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#pragma clang diagnostic pop
#undef Rect
#undef Delay

#include "os/misc_mac.hpp"

#include "gui_platform.hpp"

// TODO: support file filters, maybe with allowedContentTypes?
// #define DIALOG_DELEGATE_CLASS MAKE_UNIQUE_OBJC_NAME(DialogDelegate)
//
// @interface DIALOG_DELEGATE_CLASS : NSObject <NSOpenSavePanelDelegate>
// @property Span<FilePickerDialogOptions::FileFilter const> filters; // NOLINT
// @end
//
// @implementation DIALOG_DELEGATE_CLASS
// - (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
//     NSString* ns_filename = [url lastPathComponent];
//     auto const filename = NSStringToString(ns_filename);
//
//     NSNumber* is_directory = nil;
//
//     BOOL outcome = [url getResourceValue:&is_directory forKey:NSURLIsDirectoryKey error:nil];
//     if (!outcome) return YES;
//     if (is_directory) return YES;
//
//     for (auto filter : self.filters)
//         if (MatchWildcard(filter.wildcard_filter, filename)) return YES;
//
//     return NO;
// }
// @end

struct NativeFilePicker {
    NSSavePanel* panel = nullptr;
};

constexpr uintptr k_file_picker_completed = 0xD1A106;

bool detail::NativeFilePickerOnClientMessage(GuiPlatform& platform, uintptr data1, uintptr data2) {
    if (!platform.native_file_picker) return false;
    if (data1 != k_file_picker_completed) return false;

    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return false;

    ASSERT([NSThread isMainThread]);
    platform.frame_state.file_picker_results = {};
    platform.file_picker_result_arena.ResetCursorAndConsolidateRegions();

    auto const response = (NSModalResponse)data2;
    if (response == NSModalResponseOK) {
        if ([native_file_picker.panel isKindOfClass:[NSOpenPanel class]]) {
            auto open_panel = (NSOpenPanel*)native_file_picker.panel;

            for (auto const i : Range<NSUInteger>(open_panel.URLs.count)) {
                NSURL* selection = open_panel.URLs[i];
                NSString* path = [[selection path] stringByResolvingSymlinksInPath];
                auto utf8 = FromNullTerminated(path.UTF8String);
                ASSERT(path::IsAbsolute(utf8));

                platform.frame_state.file_picker_results.Append(utf8.Clone(platform.file_picker_result_arena),
                                                                platform.file_picker_result_arena);
            }
        } else {
            // NSSavePanel
        }
        return true;
    }

    native_file_picker.panel = nullptr; // release
    platform.native_file_picker.Clear();

    return false;
}

ErrorCodeOr<void> detail::OpenNativeFilePicker(GuiPlatform& platform,
                                               FilePickerDialogOptions const& options) {
    ASSERT(platform.view);
    if (platform.native_file_picker) return k_success;
    platform.native_file_picker.Emplace();
    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();

    @try {
        ASSERT([NSThread isMainThread]);
        ASSERT([NSApp activationPolicy] != NSApplicationActivationPolicyProhibited);

        switch (options.type) {
            case FilePickerDialogOptions::Type::OpenFile:
            case FilePickerDialogOptions::Type::SelectFolder: {
                NSOpenPanel* open_panel = [NSOpenPanel openPanel];
                ASSERT(!native_file_picker.panel);
                native_file_picker.panel = (NSSavePanel*)open_panel; // retain

                open_panel.canChooseDirectories = options.type == FilePickerDialogOptions::Type::SelectFolder;
                open_panel.canChooseFiles = options.type == FilePickerDialogOptions::Type::OpenFile;
                open_panel.canCreateDirectories = YES;
                open_panel.allowsMultipleSelection = options.allow_multiple_selection;
                break;
            }
            case FilePickerDialogOptions::Type::SaveFile: {
                NSSavePanel* save_panel = [NSSavePanel savePanel];
                ASSERT(!native_file_picker.panel);
                native_file_picker.panel = (NSSavePanel*)save_panel; // retain
                break;
            }
        }

        auto panel = native_file_picker.panel;

        // auto delegate = [[DIALOG_DELEGATE_CLASS alloc] init];
        // delegate.filters = options.filters;
        // panel.delegate = delegate;

        panel.parentWindow = ((__bridge NSView*)(void*)puglGetNativeView(platform.view)).window;
        panel.title = StringToNSString(options.title);
        [panel setLevel:NSModalPanelWindowLevel];
        panel.showsResizeIndicator = YES;
        panel.showsHiddenFiles = NO;
        panel.canCreateDirectories = YES;
        if (options.default_path)
            panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*options.default_path)];

        [panel beginWithCompletionHandler:^(NSInteger response) {
          // I don't think we can assert that this is always the main thread, so let's send it via
          // Pugl's event system which should be safe.
          PuglEvent const event {.client = {
                                     .type = PUGL_CLIENT,
                                     .flags = PUGL_IS_SEND_EVENT,
                                     .data1 = k_file_picker_completed,
                                     .data2 = (uintptr)response,
                                 }};
          auto const rc = puglSendEvent(platform.view, &event);
          ASSERT(rc == PUGL_SUCCESS);
        }];

    } @catch (NSException* e) {
        // TODO: report the error somehow
    }
    return k_success;
}

void detail::CloseNativeFilePicker(GuiPlatform& platform) {
    if (!platform.native_file_picker) return;
    auto& native_file_picker = platform.native_file_picker->As<NativeFilePicker>();
    if (!native_file_picker.panel) return;
    [native_file_picker.panel close];
    native_file_picker.panel = nullptr; // release
    platform.native_file_picker.Clear();
}
