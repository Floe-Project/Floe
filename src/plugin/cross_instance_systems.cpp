// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cross_instance_systems.hpp"

#include <time.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"
#include "utils/debug/debug.hpp"

#include "settings/settings_file.hpp"

void FloeLogger::LogFunction(String str, LogLevel level, bool add_newline) {
    DebugAssertMainThread();

    TracyMessageEx(
        {
            .category = "floelogger",
            .colour = 0xffffff,
            .object_id = nullopt,
        },
        "{}: {}",
        ({
            String c;
            switch (level) {
                case LogLevel::Debug: c = "Debug: "_s; break;
                case LogLevel::Info: c = ""_s; break;
                case LogLevel::Warning: c = "Warning: "_s; break;
                case LogLevel::Error: c = "Error: "_s; break;
            }
            c;
        }),
        str);

    bool first_message = false;

    if (!m_path) {
        auto outcome = KnownDirectoryWithSubdirectories(arena, KnownDirectories::Logs, Array {"Floe"_s});
        if (outcome.HasValue()) {
            auto path = DynamicArray<char>::FromOwnedSpan(outcome.Value(), arena);
            path::JoinAppend(path, "floe.log");
            m_path = path.ToOwnedSpan();
            first_message = true;
        } else {
            DebugLn("failed to get logs known dir: {}", outcome.Error());
        }
    }

    auto file = ({
        auto outcome = OpenFile(*m_path, FileMode::Append);
        if (outcome.HasError()) {
            DebugLn("failed to open log file: {}", outcome.Error());
            return;
        }
        outcome.ReleaseValue();
    });

    auto try_writing = [&](Writer writer) -> ErrorCodeOr<void> {
        if (first_message) {
            TRY(writer.WriteChars("=======================\n"_s));

            char time_buffer[128];
            auto t = time(nullptr);
            auto const time_str_len = strftime(time_buffer, sizeof(time_buffer), "%c", localtime(&t));
            TRY(writer.WriteChars(String {time_buffer, time_str_len}));
            TRY(writer.WriteChar('\n'));

            TRY(fmt::FormatToWriter(writer,
                                    "Floe v{}.{}.{} {}\n",
                                    FLOE_MAJOR_VERSION,
                                    FLOE_MINOR_VERSION,
                                    FLOE_PATCH_VERSION));

            TRY(writer.WriteChars("Git commit: " GIT_HEAD_SHA1 "\n"_s));

            TRY(fmt::FormatToWriter(writer, "OS: {}\n", OperatingSystemName()));
        }

        TRY(writer.WriteChars(LogPrefix(level, {.ansi_colors = false, .no_info_prefix = false})));
        TRY(writer.WriteChars(str));
        if (add_newline) TRY(writer.WriteChar('\n'));
        return k_success;
    };

    auto writer = file.Writer();
    auto o = try_writing(writer);
    if (o.HasError()) DebugLn("failed to write log file: {}, {}", *m_path, o.Error());
}

CrossInstanceSystems::CrossInstanceSystems()
    : arena(PageAllocator::Instance(), Kb(16))
    , logger(arena)
    , paths(CreateFloePaths(arena))
    , settings(paths)
    , available_libraries(paths.always_scanned_folders[ToInt(ScanFolderType::Libraries)], error_notifications)
    , sample_library_loader(thread_pool, available_libraries) {
    folder_settings_listener_id =
        settings.tracking.filesystem_change_listeners.Add([this](ScanFolderType type) {
            switch (type) {
                case ScanFolderType::Presets: preset_listing.scanned_folder.needs_rescan.Store(true); break;
                case ScanFolderType::Libraries: {
                    available_libraries.SetExtraScanFolders(
                        settings.settings.filesystem.extra_libraries_scan_folders);
                    break;
                }
                case ScanFolderType::Count: PanicIfReached();
            }
        });
    thread_pool.Init("Global", {});

    // =========
    bool file_is_new = false;
    auto opt_data = FindAndReadSettingsFile(settings.arena, paths);
    if (!opt_data)
        file_is_new = true;
    else
        settings.settings = *opt_data;
    if (InitialiseSettingsFileData(settings.settings, settings.arena, file_is_new))
        settings.tracking.changed = true;

    ASSERT(settings.settings.gui.window_width != 0);

    available_libraries.SetExtraScanFolders(settings.settings.filesystem.extra_libraries_scan_folders);
}

CrossInstanceSystems::~CrossInstanceSystems() {
    {
        auto outcome = WriteSettingsFileIfChanged(settings);
        if (outcome.HasError()) logger.ErrorLn("Failed to write settings file: {}", outcome.Error());
    }

    settings.tracking.filesystem_change_listeners.Remove(folder_settings_listener_id);
}
