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

bool detail::NativeFilePickerOnClientMessage(GuiPlatform&, uintptr, uintptr) { return false; }

ErrorCodeOr<void> detail::OpenNativeFilePicker(GuiPlatform& platform,
                                               FilePickerDialogOptions const& options) {
    ASSERT(platform.view);
    if (platform.native_file_picker) return k_success;
    platform.native_file_picker.Emplace();
    @try {
        // auto delegate = [[DIALOG_DELEGATE_CLASS alloc] init];
        // delegate.filters = options.filters;
        switch (options.type) {
            case FilePickerDialogOptions::Type::OpenFile:
            case FilePickerDialogOptions::Type::SelectFolder: {
                ASSERT([NSThread isMainThread]);
                ASSERT([NSApp activationPolicy] != NSApplicationActivationPolicyProhibited);

                NSOpenPanel* open_panel = [NSOpenPanel openPanel];
                platform.native_file_picker->As<void*>() = (__bridge_retained void*)(NSSavePanel*)open_panel;

                // open_panel.delegate = delegate;
                open_panel.parentWindow = ((__bridge NSView*)(void*)puglGetNativeView(platform.view)).window;

                open_panel.title = StringToNSString(options.title);
                [open_panel setLevel:NSModalPanelWindowLevel];
                open_panel.showsResizeIndicator = YES;
                open_panel.showsHiddenFiles = NO;
                open_panel.canChooseDirectories = options.type == FilePickerDialogOptions::Type::SelectFolder;
                open_panel.canChooseFiles = options.type == FilePickerDialogOptions::Type::OpenFile;
                open_panel.canCreateDirectories = YES;
                open_panel.allowsMultipleSelection = options.allow_multiple_selection;
                if (options.default_path)
                    open_panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*options.default_path)];

                [open_panel beginWithCompletionHandler:^(NSInteger response) {
                  ASSERT([NSThread isMainThread]); // TODO: is this assertion guaranteed to be true?
                  platform.frame_state.file_picker_results = {};
                  platform.file_picker_result_arena.ResetCursorAndConsolidateRegions();
                  if (response == NSModalResponseOK) {
                      for (auto const i : Range<NSUInteger>(open_panel.URLs.count)) {
                          NSURL* selection = open_panel.URLs[i];
                          NSString* path = [[selection path] stringByResolvingSymlinksInPath];
                          auto utf8 = FromNullTerminated(path.UTF8String);
                          ASSERT(path::IsAbsolute(utf8));

                          platform.frame_state.file_picker_results.Append(
                              utf8.Clone(platform.file_picker_result_arena),
                              platform.file_picker_result_arena);
                      }
                  } else {
                  }
                  platform.native_file_picker.Clear();
                }];

                break;
            }
            case FilePickerDialogOptions::Type::SaveFile: {
                // NSSavePanel* panel = [NSSavePanel savePanel];
                // panel.title = StringToNSString(options.title);
                // [panel setLevel:NSModalPanelWindowLevel];
                // if (options.default_path) {
                //     if (auto const dir = path::Directory(*options.default_path))
                //         panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*dir)
                //         isDirectory:YES];
                // }
                //
                // auto const response = [panel runModal];
                // if (response == NSModalResponseOK) {
                //     auto const path = NSStringToString([[panel URL] path]);
                //     if (path::IsAbsolute(path)) {
                //         auto result = options.allocator.AllocateExactSizeUninitialised<MutableString>(1);
                //         result[0] = path.Clone(options.allocator);
                //         return result;
                //     }
                // }
                break;
            }
        }

    } @catch (NSException* e) {
        // TODO: report the error somehow
    }
    return k_success;
}

void detail::CloseNativeFilePicker(GuiPlatform& platform) {
    if (!platform.native_file_picker) return;
    auto panel = (__bridge_transfer NSSavePanel*)platform.native_file_picker->As<void*>();

    [panel close];
}
