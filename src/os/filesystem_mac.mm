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
#include <pthread.h>
#include <sys/event.h>
#include <sys/stat.h>
#pragma clang diagnostic pop
#undef Rect

#include "foundation/foundation.hpp"
#include "os/misc.hpp"
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

// NOTE: on macOS there are some 'firmlinks' which are like symlinks but are resolved by the kernel.
// These firmlinks are not resolved if we use the NSString resolving functions, so we use realpath.
// e.g. /var -> /private/var only happens with realpath
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

// NOTE: It seems you can receive filesystem events from before you start watching the directory even if you
// use the kFSEventStreamEventIdSinceNow flag. There could be some sort of buffering going on.

// NOTE: FSEvents is a callback based API. You receive events in a separate thread and there doesn't seem to
// be a way around that. FSEventStreamFlushSync might flush the events but you can still receive others at any
// time in the other thread. Therefore we use a queue to pass the buffer the events from the background thread
// to the ReadDirectoryChanges thread.

// References:
// https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/FSEvents_ProgGuide/UsingtheFSEventsFramework/UsingtheFSEventsFramework.html
// CHOC library: https://github.com/Tracktion/choc/blob/main/platform/choc_FileWatcher.h
// EFSW library: https://github.com/SpartanJ/efsw/blob/master/src/efsw/WatcherFSEvents.cpp
// Atom file watcher: https://github.com/atom/watcher/blob/master/docs/macos.md

struct FsWatcher {
    FSEventStreamRef stream {};
    dispatch_queue_t queue {};

    struct Event {
        enum class Existence { Unknown, Exists, DoesNotExist };
        String path;
        FSEventStreamEventFlags flags;
        Existence exists = Existence::Unknown;
        Event* next;
        Event* prev;
    };
    Mutex event_mutex;
    ArenaAllocator event_arena {PageAllocator::Instance()};
    struct Events {
        Event* first;
        Event* last;
    } events;
};

ErrorCodeOr<DirectoryWatcher> CreateDirectoryWatcher(Allocator& a) {
    ZoneScoped;
    DirectoryWatcher result {
        .allocator = a,
        .watched_dirs = {a},
        .native_data = {.pointer = a.New<FsWatcher>()},
    };
    return result;
}

void DestoryDirectoryWatcher(DirectoryWatcher& watcher) {
    ZoneScoped;

    auto fs_w = (FsWatcher*)watcher.native_data.pointer;
    if (fs_w->stream) {
        FSEventStreamStop(fs_w->stream);
        FSEventStreamInvalidate(fs_w->stream);
        FSEventStreamRelease(fs_w->stream);
    }
    // dispatch_release(fs_w->queue);
    watcher.watched_dirs.Clear();
    watcher.allocator.Delete(fs_w);
}

void EventCallback([[maybe_unused]] ConstFSEventStreamRef stream_ref,
                   [[maybe_unused]] void* user_data,
                   size_t num_events,
                   void* event_paths,
                   FSEventStreamEventFlags const event_flags[],
                   [[maybe_unused]] FSEventStreamEventId const event_ids[]) {
    auto& watcher = *(FsWatcher*)user_data;
    auto** paths = (char**)event_paths;

    if constexpr (!PRODUCTION_BUILD) {
        DynamicArrayInline<char, 4000> info;
        struct Flag {
            FSEventStreamEventFlags flag;
            String name;
        };
        constexpr Flag k_flags[] = {
            {kFSEventStreamEventFlagMustScanSubDirs, "MustScanSubDirs"},
            {kFSEventStreamEventFlagUserDropped, "UserDropped"},
            {kFSEventStreamEventFlagKernelDropped, "KernelDropped"},
            {kFSEventStreamEventFlagEventIdsWrapped, "EventIdsWrapped"},
            {kFSEventStreamEventFlagHistoryDone, "HistoryDone"},
            {kFSEventStreamEventFlagRootChanged, "RootChanged"},
            {kFSEventStreamEventFlagMount, "Mount"},
            {kFSEventStreamEventFlagUnmount, "Unmount"},
            {kFSEventStreamEventFlagItemChangeOwner, "ItemChangeOwner"},
            {kFSEventStreamEventFlagItemCreated, "ItemCreated"},
            {kFSEventStreamEventFlagItemFinderInfoMod, "ItemFinderInfoMod"},
            {kFSEventStreamEventFlagItemInodeMetaMod, "ItemInodeMetaMod"},
            {kFSEventStreamEventFlagItemIsDir, "ItemIsDir"},
            {kFSEventStreamEventFlagItemIsFile, "ItemIsFile"},
            {kFSEventStreamEventFlagItemIsHardlink, "ItemIsHardlink"},
            {kFSEventStreamEventFlagItemIsLastHardlink, "ItemIsLastHardlink"},
            {kFSEventStreamEventFlagItemIsSymlink, "ItemIsSymlink"},
            {kFSEventStreamEventFlagItemModified, "ItemModified"},
            {kFSEventStreamEventFlagItemRemoved, "ItemRemoved"},
            {kFSEventStreamEventFlagItemRenamed, "ItemRenamed"},
            {kFSEventStreamEventFlagItemXattrMod, "ItemXattrMod"},
            {kFSEventStreamEventFlagOwnEvent, "OwnEvent"},
            {kFSEventStreamEventFlagItemCloned, "ItemCloned"},
        };
        auto writer = dyn::WriterFor(info);

        auto _ = fmt::AppendLine(writer, "FSEvent received {} events:", num_events);

        for (size_t i = 0; i < num_events; i++) {
            MAYBE_UNUSED auto u1 = fmt::AppendLine(writer, "  {{");
            MAYBE_UNUSED auto u2 = fmt::AppendLine(writer, "    path: {}", paths[i]);

            DynamicArrayInline<char, 1000> flags_str;
            for (auto const& flag : k_flags)
                if (event_flags[i] & flag.flag) {
                    dyn::AppendSpan(flags_str, flag.name);
                    dyn::AppendSpan(flags_str, ", ");
                }
            if (flags_str.size) flags_str.size -= 2;
            MAYBE_UNUSED auto u3 = fmt::AppendLine(writer, "    flags: {}", flags_str);
            MAYBE_UNUSED auto u4 = fmt::AppendLine(writer, "    id: {}", event_ids[i]);

            MAYBE_UNUSED auto u5 = fmt::AppendLine(writer, "  }}");
        }

        StdPrint(StdStream::Err, info);
    }

#if 0
    ArenaAllocatorWithInlineStorage<1024> scratch_arena;
    auto coalesced = scratch_arena.NewMultiple<bool>(num_events);
#endif

    for (size_t i = 0; i < num_events; i++) {
        FSEventStreamEventFlags flags = event_flags[i];
#if 0
        if (coalesced[i]) continue;

        for (size_t j = i + 1; j < num_events; j++) {
            if (coalesced[j]) continue;
            if (NullTermStringsEqual(paths[i], paths[j])) {
                flags |= event_flags[j];
                coalesced[j] = true;
            }
        }
#endif

        auto existence = FsWatcher::Event::Existence::Unknown;
        if (flags & (kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagItemRemoved |
                     kFSEventStreamEventFlagItemCreated)) {
            struct stat info;
            if (lstat(paths[i], &info) == 0)
                existence = FsWatcher::Event::Existence::Exists;
            else if (errno == ENOENT)
                existence = FsWatcher::Event::Existence::DoesNotExist;
        }

        watcher.event_mutex.Lock();
        DEFER { watcher.event_mutex.Unlock(); };

        auto event = watcher.event_arena.NewUninitialised<FsWatcher::Event>();
        PLACEMENT_NEW(event)
        FsWatcher::Event {
            .path = watcher.event_arena.Clone(FromNullTerminated(paths[i])),
            .flags = flags,
            .exists = existence,
        };

        DoublyLinkedListAppend(watcher.events, event);
    }
}

ErrorCodeOr<void> ReadDirectoryChanges(DirectoryWatcher& watcher,
                                       Span<DirectoryToWatch const> dirs_to_watch,
                                       [[maybe_unused]] ArenaAllocator& scratch_arena,
                                       DirectoryWatcher::Callback callback) {
    auto const any_states_changed = watcher.HandleWatchedDirChanges(dirs_to_watch, scratch_arena);

    auto& fs_watcher = *(FsWatcher*)watcher.native_data.pointer;
    if (any_states_changed) {
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

        FSEventStreamContext context {};
        context.info = &fs_watcher;

        constexpr double k_latency_seconds = 0.0; // TODO: allow setting this as an option?

        fs_watcher.stream =
            FSEventStreamCreate(nullptr,
                                &EventCallback,
                                &context,
                                paths,
                                kFSEventStreamEventIdSinceNow,
                                k_latency_seconds,
                                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagWatchRoot |
                                    kFSEventStreamCreateFlagNoDefer);
        if (!fs_watcher.stream)
            return ErrorCode {FilesystemError::PathDoesNotExist}; // TODO(1.0): not the right error

        // FSEvents is a callback based API where you always receive events in a separate thread.
        if (!fs_watcher.queue) {
            auto attr =
                dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, -10);
            fs_watcher.queue = dispatch_queue_create("com.floe.fseventsqueue", attr);
        }
        FSEventStreamSetDispatchQueue(fs_watcher.stream, fs_watcher.queue);

        DebugLn("Starting FSEventStream");
        if (!FSEventStreamStart(fs_watcher.stream)) {
            FSEventStreamInvalidate(fs_watcher.stream);
            FSEventStreamRelease(fs_watcher.stream);
            return ErrorCode {FilesystemError::PathDoesNotExist}; // TODO(1.0): not the right error
        }
    }

    if (fs_watcher.stream) FSEventStreamFlushAsync(fs_watcher.stream);

    {
        fs_watcher.event_mutex.Lock();
        DEFER { fs_watcher.event_mutex.Unlock(); };

        for (auto event = fs_watcher.events.first; event; event = event->next) {
            for (auto const& dir : watcher.watched_dirs) {
                if (StartsWithSpan(event->path, dir.resolved_path)) {
                    if (event->flags & kFSEventStreamEventFlagMustScanSubDirs) {
                        callback(
                            event->path,
                            DirectoryWatcher::FileChange {
                                .changes =
                                    Array {DirectoryWatcher::FileChange::Change::UnknownManualRescanNeeded},
                                .subpath = {},
                            });
                    } else {
                        String subpath {};
                        if (event->path.size == dir.resolved_path.size)
                            continue; // ignore
                        else
                            subpath = event->path.SubSpan(dir.resolved_path.size);
                        if (subpath[0] == '/') subpath = subpath.SubSpan(1);

                        // FSEvents ONLY supports recursive watching so we just have to ignore subdirectory
                        // events
                        if (!dir.recursive && Contains(subpath, '/')) {
                            DebugLn("Ignoring subdirectory event: {} because {} is watched non-recursively",
                                    subpath,
                                    dir.path);
                            continue;
                        }

                        auto const created = event->flags & kFSEventStreamEventFlagItemCreated;
                        auto const removed = event->flags & kFSEventStreamEventFlagItemRemoved;
                        auto const renamed = event->flags & kFSEventStreamEventFlagItemRenamed;
                        auto const modified = event->flags & kFSEventStreamEventFlagItemModified;

                        DirectoryWatcher::FileChange change {
                            .changes = {},
                            .subpath = subpath,
                            .file_type = (event->flags & kFSEventStreamEventFlagItemIsDir)
                                             ? FileType::Directory
                                             : FileType::RegularFile,
                        };

                        if (renamed) {
                            if (event->exists == FsWatcher::Event::Existence::Exists)
                                dyn::Append(change.changes,
                                            DirectoryWatcher::FileChange::Change::RenamedNewName);
                            else if (event->exists == FsWatcher::Event::Existence::DoesNotExist)
                                dyn::Append(change.changes,
                                            DirectoryWatcher::FileChange::Change::RenamedOldName);
                        } else {
                            if (created && removed)
                                if (event->exists == FsWatcher::Event::Existence::DoesNotExist)
                                    dyn::Append(change.changes, DirectoryWatcher::FileChange::Change::Added);
                                else
                                    dyn::Append(change.changes,
                                                DirectoryWatcher::FileChange::Change::Deleted);
                            else if (created)
                                dyn::Append(change.changes, DirectoryWatcher::FileChange::Change::Added);
                        }

                        // TODO: this logic isn't quite right, we can get 'modified' AFTER 'removed'
                        if (modified)
                            dyn::Append(change.changes, DirectoryWatcher::FileChange::Change::Modified);

                        if (!renamed) {
                            if (removed && created)
                                if (event->exists == FsWatcher::Event::Existence::DoesNotExist)
                                    dyn::Append(change.changes,
                                                DirectoryWatcher::FileChange::Change::Deleted);
                                else
                                    dyn::Append(change.changes, DirectoryWatcher::FileChange::Change::Added);
                            else if (removed)
                                dyn::Append(change.changes, DirectoryWatcher::FileChange::Change::Deleted);
                        }

                        callback(dir.path, change);
                    }
                }
            }
        }

        fs_watcher.events.first = nullptr;
        fs_watcher.events.last = nullptr;
        fs_watcher.event_arena.ResetCursorAndConsolidateRegions();
    }

    watcher.RemoveAllNotWatching();

    return k_success;
}
