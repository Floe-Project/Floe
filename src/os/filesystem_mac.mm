// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#define Rect MacRect
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wextra-semi"
#include <AppKit/AppKit.h>
#include <CoreServices/CoreServices.h>
#include <dirent.h>
#include <mach-o/dyld.h>
#include <sys/event.h>
#include <sys/stat.h>
#pragma clang diagnostic pop
#undef Rect

#include "foundation/foundation.hpp"
#include "os/misc_mac.hpp"
#include "os/threading.hpp"
#include "utils/debug/debug.hpp"

#include "filesystem.hpp"

__attribute__((visibility("default")))
@interface MAKE_UNIQUE_OBJC_NAME(ClassForGettingBundle) : NSObject {}
@end
@implementation MAKE_UNIQUE_OBJC_NAME (ClassForGettingBundle)
@end

static ErrorCode FilesystemErrorFromNSError(NSError* error,
                                            char const* extra_debug_info = nullptr,
                                            SourceLocation loc = SourceLocation::Current()) {
    ASSERT(error != nil);
    if ([error.domain isEqualToString:NSCocoaErrorDomain]) {
        if (error.code == NSFileNoSuchFileError || error.code == NSFileReadNoSuchFileError)
            return ErrorCode(FilesystemError::PathDoesNotExist, extra_debug_info, loc);
        else if (error.code == NSURLErrorUserAuthenticationRequired)
            return ErrorCode(FilesystemError::AccessDenied, extra_debug_info, loc);
    } else if ([error.domain isEqualToString:NSURLErrorDomain]) {
        if (error.code == NSURLErrorUserAuthenticationRequired)
            return ErrorCode(FilesystemError::AccessDenied, extra_debug_info, loc);
    }
    return ErrorFromNSError(error, extra_debug_info, loc);
}

ErrorCodeOr<void> MoveFile(String from, String to, ExistingDestinationHandling existing) {
    if (existing == ExistingDestinationHandling::Skip || existing == ExistingDestinationHandling::Overwrite) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:StringToNSString(to)]) {
            if (existing == ExistingDestinationHandling::Skip) return k_success;

            TRY(Delete(to, {}));
        }
    }

    NSError* error = nil;
    if (![[NSFileManager defaultManager] moveItemAtPath:StringToNSString(from)
                                                 toPath:StringToNSString(to)
                                                  error:&error]) {
        return FilesystemErrorFromNSError(error);
    }
    return k_success;
}

ErrorCodeOr<void> CopyFile(String from, String to, ExistingDestinationHandling existing) {
    auto dest = StringToNSString(to);
    if (existing == ExistingDestinationHandling::Fail || existing == ExistingDestinationHandling::Skip) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:dest]) {
            return existing == ExistingDestinationHandling::Fail
                       ? ErrorCodeOr<void> {ErrorCode(FilesystemError::PathAlreadyExists)}
                       : k_success;
        }
    }

    NSError* error = nil;
    if (![[NSFileManager defaultManager] copyItemAtPath:StringToNSString(from) toPath:dest error:&error])
        return FilesystemErrorFromNSError(error);
    return k_success;
}

ErrorCodeOr<MutableString> ConvertToAbsolutePath(Allocator& a, String path) {
    ASSERT(path.size);
    auto nspath = StringToNSString(path);
    if (StartsWith(path, '~')) nspath = nspath.stringByExpandingTildeInPath;
    auto url = [NSURL fileURLWithPath:nspath];
    if (url == nil) return FilesystemErrorFromNSError(nullptr);
    auto abs_string = [url path];
    if (abs_string == nil) return FilesystemErrorFromNSError(nullptr);

    auto result = NSStringToString(abs_string).Clone(a);
    ASSERT(path::IsAbsolute(result));
    return result;
}

// NOTE: on macos there are some 'firmlinks' which are like symlinks but are resolved by the kernel
ErrorCodeOr<MutableString> ResolveSymlinks(Allocator& a, String path) {
    ASSERT(path.size);
    auto nspath = StringToNSString(path);
    char resolved_path[PATH_MAX * 2];
    if (realpath([nspath fileSystemRepresentation], resolved_path) == nullptr)
        return FilesystemErrnoErrorCode(errno);
    return a.Clone(FromNullTerminated(resolved_path));
}

ErrorCodeOr<void> Delete(String path, DeleteOptions options) {
    PathArena path_arena;
    switch (options.type) {
        case DeleteOptions::Type::Any:
        case DeleteOptions::Type::File:
        case DeleteOptions::Type::DirectoryRecursively: {
            NSError* error = nil;
            if (![[NSFileManager defaultManager] removeItemAtPath:StringToNSString(path) error:&error]) {
                auto const err = FilesystemErrorFromNSError(error);
                if (err == FilesystemError::PathDoesNotExist && !options.fail_if_not_exists) return k_success;
                return err;
            }
            return k_success;
        }
        case DeleteOptions::Type::DirectoryOnlyIfEmpty: {
            if (rmdir(NullTerminated(path, path_arena)) == 0)
                return k_success;
            else {
                if (errno == ENOENT && !options.fail_if_not_exists) return k_success;
                return FilesystemErrnoErrorCode(errno);
            }
            break;
        }
    }
    return k_success;
}

ErrorCodeOr<bool> DeleteDirectoryIfMacBundle(String dir) {
    auto bundle = [NSBundle bundleWithPath:StringToNSString(dir)];
    if (bundle != nil) {
        DebugLn("Deleting mac bundle");
        TRY(Delete(dir, {}));
        return true;
    }
    return false;
}

ErrorCodeOr<void> CreateDirectory(String path, CreateDirectoryOptions options) {
    if (options.create_intermediate_directories && options.fail_if_exists) {
        auto o = GetFileType(path);
        if (o.HasValue() && o.Value() == FileType::Directory)
            return ErrorCode {FilesystemError::PathAlreadyExists};
    }

    // returns YES if createIntermediates is set and the directory already exists
    NSError* error = nil;
    if (![[NSFileManager defaultManager]
                  createDirectoryAtPath:StringToNSString(path)
            withIntermediateDirectories:options.create_intermediate_directories ? YES : NO
                             attributes:nil
                                  error:&error]) {
        auto const ec = FilesystemErrorFromNSError(error);
        if (ec == FilesystemError::PathAlreadyExists && !options.fail_if_exists) return k_success;
    }
    return k_success;
}

static ErrorCodeOr<MutableString>
OSXGetSystemFilepath(Allocator& a, NSSearchPathDirectory type, NSSearchPathDomainMask domain) {
    NSError* error = nil;
    auto url = [[NSFileManager defaultManager] URLForDirectory:type
                                                      inDomain:domain
                                             appropriateForURL:nullptr
                                                        create:YES
                                                         error:&error];

    if (url == nil) return FilesystemErrorFromNSError(error);
    return a.Clone(NSStringToString(url.path));
}

ErrorCodeOr<MutableString> KnownDirectory(Allocator& a, KnownDirectories type) {
    NSSearchPathDirectory osx_type {};
    NSSearchPathDomainMask domain {};
    Optional<String> extra_subdirs {};
    Optional<String> fallback {};
    switch (type) {
        case KnownDirectories::Temporary: return a.Clone(NSStringToString(NSTemporaryDirectory()));
        case KnownDirectories::Logs:
            osx_type = NSLibraryDirectory;
            domain = NSUserDomainMask;
            extra_subdirs = "Logs";
            break;
        case KnownDirectories::PluginSettings:
            osx_type = NSMusicDirectory;
            domain = NSUserDomainMask;
            extra_subdirs = "Audio Music Apps/Plug-In Settings";
            break;
        case KnownDirectories::Documents:
            osx_type = NSDocumentDirectory;
            domain = NSUserDomainMask;
            break;
        case KnownDirectories::Downloads:
            osx_type = NSDownloadsDirectory;
            domain = NSUserDomainMask;
            break;
        case KnownDirectories::Prefs:
            osx_type = NSApplicationSupportDirectory;
            domain = NSUserDomainMask;
            break;
        case KnownDirectories::Data:
            osx_type = NSApplicationSupportDirectory;
            domain = NSUserDomainMask;
            break;
        case KnownDirectories::AllUsersData:
            osx_type = NSApplicationSupportDirectory;
            domain = NSLocalDomainMask;
            fallback = "/Library/Application Support";
            break;
        case KnownDirectories::AllUsersSettings:
            osx_type = NSApplicationSupportDirectory;
            domain = NSLocalDomainMask;
            fallback = "/Library/Application Support";
            break;
        case KnownDirectories::ClapPlugin:
            osx_type = NSLibraryDirectory;
            domain = NSLocalDomainMask;
            extra_subdirs = "Audio/Plug-Ins/CLAP";
            fallback = "/Library";
            break;
        case KnownDirectories::Vst3Plugin:
            osx_type = NSLibraryDirectory;
            domain = NSLocalDomainMask;
            extra_subdirs = "Audio/Plug-Ins/VST3";
            fallback = "/Library";
            break;
        case KnownDirectories::Count: PanicIfReached();
    }

    auto path = ({
        MutableString p {};
        if (auto outcome = OSXGetSystemFilepath(a, osx_type, domain); outcome.HasError())
            if (!fallback)
                return outcome.Error();
            else
                p = a.Clone(*fallback);
        else
            p = outcome.Value();
        p;
    });

    if (extra_subdirs) {
        auto p = DynamicArray<char>::FromOwnedSpan(path, a);
        path::JoinAppend(p, *extra_subdirs);
        path = p.ToOwnedSpan();
    }

    ASSERT(path::IsAbsolute(path));
    return path;
}

ErrorCodeOr<MutableString> CurrentExecutablePath(Allocator& a) {
    u32 size = 0;
    constexpr int k_buffer_not_large_enough = -1;
    char unused_buffer[1];
    errno = 0;
    if (_NSGetExecutablePath(unused_buffer, &size) == k_buffer_not_large_enough) {
        auto result = a.AllocateExactSizeUninitialised<char>(size);
        errno = 0;
        if (_NSGetExecutablePath(result.data, &size) == 0)
            return result;
        else
            a.Free(result.ToByteSpan());
    }
    if (errno != 0) return FilesystemErrnoErrorCode(errno);
    PanicIfReached();
    return MutableString {};
}

ErrorCodeOr<DynamicArrayInline<char, 200>> NameOfRunningExecutableOrLibrary() {
    if (NSBundle* bundle = [NSBundle bundleForClass:[MAKE_UNIQUE_OBJC_NAME(ClassForGettingBundle) class]])
        return path::Filename(NSStringToString(bundle.bundlePath));

    u32 size = 0;
    constexpr int k_buffer_not_large_enough = -1;
    char unused_buffer[1];
    errno = 0;
    if (_NSGetExecutablePath(unused_buffer, &size) == k_buffer_not_large_enough) {
        DynamicArrayInline<char, 200> result;
        dyn::Resize(result, size);
        errno = 0;
        if (_NSGetExecutablePath(result.data, &size) == 0) return result;
    }
    if (errno != 0) return FilesystemErrnoErrorCode(errno);
    PanicIfReached();
    return ""_s;
}

Optional<Version> MacosBundleVersion(String path) {
    auto bundle = [NSBundle bundleWithPath:StringToNSString(path)];
    if (bundle == nil) return {};
    NSString* version_string = bundle.infoDictionary[@"CFBundleVersion"];
    NSString* beta_version = bundle.infoDictionary[@"Beta_Version"];
    FixedSizeAllocator<64> temp_mem;
    DynamicArray<char> str {FromNullTerminated(version_string.UTF8String), temp_mem};
    if (beta_version != nil && beta_version.length) fmt::Append(str, "-Beta{}", beta_version.UTF8String);
    return ParseVersionString(str);
}

@interface DialogDelegate : NSObject <NSOpenSavePanelDelegate>
@property Span<DialogOptions::FileFilter const> filters; // NOLINT
@end

@implementation DialogDelegate
- (BOOL)panel:(id)sender shouldEnableURL:(NSURL*)url {
    NSString* ns_filename = [url lastPathComponent];
    auto const filename = NSStringToString(ns_filename);

    NSNumber* is_directory = nil;

    BOOL outcome = [url getResourceValue:&is_directory forKey:NSURLIsDirectoryKey error:nil];
    if (!outcome) return YES;
    if (is_directory) return YES;

    for (auto filter : self.filters)
        if (MatchWildcard(filter.wildcard_filter, filename)) return YES;

    return NO;
}
@end

ErrorCodeOr<Optional<MutableString>> FilesystemDialog(DialogOptions options) {
    auto delegate = [[DialogDelegate alloc] init];
    delegate.filters = options.filters;
    switch (options.type) {
        case DialogOptions::Type::SelectFolder: {
            NSOpenPanel* open_panel = [NSOpenPanel openPanel];
            open_panel.delegate = delegate;

            open_panel.title = StringToNSString(options.title);
            [open_panel setLevel:NSModalPanelWindowLevel];
            open_panel.showsResizeIndicator = YES;
            open_panel.showsHiddenFiles = NO;
            open_panel.canChooseDirectories = YES;
            open_panel.canChooseFiles = NO;
            open_panel.canCreateDirectories = YES;
            open_panel.allowsMultipleSelection = NO;
            if (options.default_path)
                open_panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*options.default_path)];

            auto const response = [open_panel runModal];
            if (response == NSModalResponseOK) {
                NSURL* selection = open_panel.URLs[0];
                NSString* path = [[selection path] stringByResolvingSymlinksInPath];
                auto utf8 = FromNullTerminated(path.UTF8String);
                if (path::IsAbsolute(utf8)) return utf8.Clone(options.allocator);
            }
            return nullopt;
        }
        case DialogOptions::Type::OpenFile: {
#if 0
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = StringViewToNSString(title);
    panel.directoryURL =
        [NSURL fileURLWithPath:StringViewToNSString(Legacy::Directory(default_path_and_file))
                   isDirectory:YES];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;

    [panel setLevel:NSModalPanelWindowLevel];
    [panel beginWithCompletionHandler:^(NSModalResponse result) {
      if (result == NSModalResponseOK) {
          callback_function(std::string([[[[panel URLs] objectAtIndex:0] path] UTF8String]));
      }
    }];
#endif
            TODO(); // TODO(1.0)
            return nullopt;
        }
        case DialogOptions::Type::SaveFile: {
            NSSavePanel* panel = [NSSavePanel savePanel];
            panel.title = StringToNSString(options.title);
            [panel setLevel:NSModalPanelWindowLevel];
            if (options.default_path) {
                if (auto const dir = path::Directory(*options.default_path))
                    panel.directoryURL = [NSURL fileURLWithPath:StringToNSString(*dir) isDirectory:YES];
            }

            auto const response = [panel runModal];
            if (response == NSModalResponseOK) {
                auto const path = NSStringToString([[panel URL] path]);
                if (path::IsAbsolute(path)) return path.Clone(options.allocator);
            }
            return nullopt;
        }
    }

    return nullopt;
}

namespace native {

struct FsWatcher {
    FSEventStreamRef stream {};
    dispatch_queue_t queue {};
};

ErrorCodeOr<void> Initialise(DirectoryWatcher& w) {
    w.native_data.pointer = w.allocator.New<FsWatcher>();
    return k_success;
}

void Deinitialise(DirectoryWatcher& w) {
    auto watcher = (FsWatcher*)w.native_data.pointer;
    if (watcher->stream) {
        FSEventStreamStop(watcher->stream);
        FSEventStreamInvalidate(watcher->stream);
        FSEventStreamRelease(watcher->stream);
    }
    // dispatch_release(watcher->queue);
    w.allocator.Delete(watcher);
}

// IMPROVE: FSEvent do actually support recursive watching I think, but I believe it will require setting up
// multiple streams
bool supports_recursive_watch = false;

void DirectoryChanged([[maybe_unused]] ConstFSEventStreamRef streamRef,
                      [[maybe_unused]] void* clientCallBackInfo,
                      size_t numEvents,
                      void* eventPaths,
                      FSEventStreamEventFlags const eventFlags[],
                      FSEventStreamEventId const eventIds[]) {
    auto** paths = (char**)eventPaths;
    for (size_t i = 0; i < numEvents; i++) {
        // TODO(1.0): handle and dispatch events
        printf("Event %llu in path %s\n", eventIds[i], paths[i]);

        if (eventFlags[i] & kFSEventStreamEventFlagItemCreated) printf("    File or directory created\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved) printf("    File or directory removed\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed) printf("    File or directory renamed\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemModified) printf("    File or directory modified\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemInodeMetaMod)
            printf("    File or directory inode metadata modified\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemFinderInfoMod)
            printf("    File or directory Finder Info modified\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemChangeOwner)
            printf("    File or directory ownership changed\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemXattrMod)
            printf("    File or directory extended attribute modified\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemIsFile) printf("    Event refers to a file\n");
        if (eventFlags[i] & kFSEventStreamEventFlagItemIsDir) printf("    Event refers to a directory\n");
    }
}

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& watcher,
                                       bool watched_directories_changed,
                                       [[maybe_unused]] ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback) {
    // TODO(1.0): watch/unwatch based on watched_directories_changed and reviewing the state of all the dirs
    (void)callback;
    auto& fs_watcher = *(FsWatcher*)watcher.native_data.pointer;
    if (watched_directories_changed) {
        if (fs_watcher.stream) {
            FSEventStreamStop(fs_watcher.stream);
            FSEventStreamInvalidate(fs_watcher.stream);
            FSEventStreamRelease(fs_watcher.stream);
        }
        CFMutableArrayRef paths = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
        DEFER { CFRelease(paths); };
        for (auto const& dir : watcher.watched_dirs) {
            if (dir.state != DirectoryWatcher::WatchedDirectory::State::Watching &&
                dir.state != DirectoryWatcher::WatchedDirectory::State::NeedsWatching)
                continue;
            CFStringRef cf_path = CFStringCreateWithBytes(nullptr,
                                                          (u8 const*)dir.path.data,
                                                          (CFIndex)dir.path.size,
                                                          kCFStringEncodingUTF8,
                                                          false);
            DEFER { CFRelease(cf_path); };
            CFArrayAppendValue(paths, cf_path);
        }
        fs_watcher.stream =
            FSEventStreamCreate(nullptr,
                                &DirectoryChanged,
                                nullptr,
                                paths,
                                kFSEventStreamEventIdSinceNow,
                                0.3,
                                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot |
                                    kFSEventStreamCreateFlagNoDefer);
        if (!fs_watcher.stream)
            return ErrorCode {FilesystemError::PathDoesNotExist}; // TODO(1.0): not the right error

        if (!fs_watcher.queue)
            fs_watcher.queue = dispatch_queue_create("com.example.fseventsqueue", DISPATCH_QUEUE_SERIAL);
        FSEventStreamSetDispatchQueue(fs_watcher.stream, fs_watcher.queue);

        if (!FSEventStreamStart(fs_watcher.stream)) {
            FSEventStreamInvalidate(fs_watcher.stream);
            FSEventStreamRelease(fs_watcher.stream);
            return ErrorCode {FilesystemError::PathDoesNotExist}; // TODO(1.0): not the right error
        }
    }

    if (fs_watcher.stream) FSEventStreamFlushSync(fs_watcher.stream);

    return k_success;
}

} // namespace native
