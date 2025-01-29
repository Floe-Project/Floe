// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logger.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

ErrorCodeOr<void> WriteFormattedLog(Writer writer,
                                    String module_name,
                                    LogLevel level,
                                    MessageWriteFunction write_message,
                                    WriteFormattedLogOptions options) {
    bool needs_space = false;
    bool needs_open_bracket = true;

    auto const begin_prefix_item = [&]() -> ErrorCodeOr<void> {
        char buf[2];
        usize len = 0;
        if (Exchange(needs_open_bracket, false)) buf[len++] = '[';
        if (Exchange(needs_space, true)) buf[len++] = ' ';
        if (len) TRY(writer.WriteChars({buf, len}));
        return k_success;
    };

    if (options.timestamp) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(Timestamp()));
    }

    if (module_name.size) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(module_name));
    }

    if (!(options.no_info_prefix && level == LogLevel::Info)) {
        TRY(begin_prefix_item());
        TRY(writer.WriteChars(({
            String s;
            switch (level) {
                case LogLevel::Debug:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_BLUE("debug")) : "debug";
                    break;
                case LogLevel::Info: s = "info"; break;
                case LogLevel::Warning:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_YELLOW("warning")) : "warning";
                    break;
                case LogLevel::Error:
                    s = options.ansi_colors ? String(ANSI_COLOUR_FOREGROUND_RED("error")) : "error";
            }
            s;
        })));
    }

    if (options.thread) {
        TRY(begin_prefix_item());
        if (auto const thread_name = ThreadName())
            TRY(writer.WriteChars(*thread_name));
        else
            TRY(writer.WriteChars(fmt::IntToString(CurrentThreadId(),
                                                   fmt::IntToStringOptions {
                                                       .base = fmt::IntToStringOptions::Base::Hexadecimal,
                                                   })));
    }

    auto const prefix_was_written = !needs_open_bracket;

    if (prefix_was_written) TRY(writer.WriteChars("] "));
    TRY(write_message(writer));
    TRY(writer.WriteChar('\n'));
    return k_success;
}

void Trace(LogModuleName module_name, String message, SourceLocation loc) {
    Log(module_name, LogLevel::Debug, [&](Writer writer) -> ErrorCodeOr<void> {
        TRY(fmt::FormatToWriter(writer,
                                "trace: {}({}): {}",
                                FromNullTerminated(loc.file),
                                loc.line,
                                loc.function));
        if (message.size) TRY(fmt::FormatToWriter(writer, ": {}", message));
        return k_success;
    });
}

LogConfig g_log_config {};

constexpr auto k_log_extension = ".log"_ca;
constexpr auto k_latest_log_filename = ConcatArrays("latest"_ca, k_log_extension);

ErrorCodeOr<void> CleanupOldLogFilesIfNeeded(ArenaAllocator& scratch_arena) {
    InitLogFolderIfNeeded();

    constexpr usize k_max_log_files = 10;

    auto const entries =
        TRY(FindEntriesInFolder(scratch_arena,
                                *LogFolder(),
                                {
                                    .options =
                                        {
                                            .wildcard = ConcatArrays("*"_ca, k_log_extension),
                                        },
                                    .recursive = false,
                                    .only_file_type = FileType::File,
                                }));
    if (entries.size <= k_max_log_files) return k_success;

    struct Entry {
        dir_iterator::Entry const* entry;
        s128 last_modified;
    };
    DynamicArray<Entry> entries_with_last_modified {scratch_arena};

    for (auto const& entry : entries) {
        if (entry.subpath == k_latest_log_filename) continue;

        auto const full_path = path::Join(scratch_arena, Array {*LogFolder(), entry.subpath});
        DEFER { scratch_arena.Free(full_path.ToByteSpan()); };

        // NOTE: the last modified time won't actually refer to the time that the file was written to, but
        // when it was renamed. But that's still a good enough approximation.
        auto const last_modified = TRY(LastModifiedTimeNsSinceEpoch(full_path));

        dyn::Append(entries_with_last_modified, Entry {&entry, last_modified});
    }

    if (entries_with_last_modified.size <= k_max_log_files) return k_success;

    Sort(entries_with_last_modified,
         [](Entry const& a, Entry const& b) { return a.last_modified < b.last_modified; });

    for (auto i : Range(entries_with_last_modified.size - k_max_log_files)) {
        auto const entry = entries_with_last_modified[i];
        auto const full_path = path::Join(scratch_arena, Array {*LogFolder(), entry.entry->subpath});
        DEFER { scratch_arena.Free(full_path.ToByteSpan()); };
        LogDebug(k_global_log_module, "deleting old log file: {}"_s, full_path);
        auto _ = Delete(full_path, {.type = DeleteOptions::Type::File});
    }

    return k_success;
}

void Log(LogModuleName module_name, LogLevel level, FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
    if (level < g_log_config.min_level_allowed.Load(LoadMemoryOrder::Relaxed)) return;

    static auto log_to_stderr = [](LogModuleName module_name,
                                   LogLevel level,
                                   FunctionRef<ErrorCodeOr<void>(Writer)> write_message) {
        constexpr WriteFormattedLogOptions k_config {
            .ansi_colors = true,
            .no_info_prefix = false,
            .timestamp = true,
            .thread = true,
        };
        auto& mutex = StdStreamMutex(StdStream::Err);
        mutex.Lock();
        DEFER { mutex.Unlock(); };

        BufferedWriter<Kb(4)> buffered_writer {StdWriter(StdStream::Err)};
        DEFER { auto _ = buffered_writer.Flush(); };

        auto _ = WriteFormattedLog(buffered_writer.Writer(), module_name.str, level, write_message, k_config);
    };

    switch (g_log_config.destination.Load(LoadMemoryOrder::Relaxed)) {
        case LogConfig::Destination::Stderr: {
            log_to_stderr(module_name, level, write_message);
            break;
        }
        case LogConfig::Destination::File: {
            static thread_local bool initialising = false;

            if (initialising) Panic("tried to log while initialising the log file");

            // We never close the log file, but that's ok, we need it for the entire lifetime of the program.
            // The OS will close it for us.
            [[clang::no_destroy]] static Optional<File> file = []() -> Optional<File> {
                initialising = true;
                DEFER { initialising = false; };

                InitLogFolderIfNeeded();

                auto seed = RandomSeed();
                ArenaAllocatorWithInlineStorage<500> arena {PageAllocator::Instance()};

                auto const log_folder = *LogFolder();

                auto const standard_path = path::Join(arena, Array {log_folder, k_latest_log_filename});
                auto const unique_path =
                    path::Join(arena, Array {log_folder, UniqueFilename("", k_log_extension, seed)});

                // We have a few requirements here:
                // - If possible, we want to use a standard path that doesn't change because it makes tailing
                //   the log easier.
                // - We don't want to overwrite any log files.
                // - We need to correctly handle the case where other processes are running this same code.
                for (auto _ : Range(50)) {
                    // We try to oust the standard log file by renaming it to a unique name. Rename is atomic.
                    // If another process is already using the log file, they will continue to do so safely,
                    // but it will be under the new name.
                    auto const o = Rename(standard_path, unique_path);

                    // If that succeeded, we can attempt to gain exclusive access to the file.
                    if (o.Succeeded() || (o.HasError() && o.Error() == FilesystemError::PathDoesNotExist)) {
                        auto file_outcome = OpenFile(
                            standard_path,
                            {
                                .capability = FileMode::Capability::Write | FileMode::Capability::Append,
                                .share = FileMode::Share::DeleteRename | FileMode::Share::ReadWrite,
                                .creation = FileMode::Creation::CreateNew, // Exclusive access
                            });

                        if (file_outcome.HasError()) {
                            if (file_outcome.Error() == FilesystemError::PathAlreadyExists) {
                                // We tried creating a new file but it already exists, another process must
                                // have created it between our Rename and OpenFile calls. Let's try again.
                                continue;
                            }
                            log_to_stderr(k_global_log_module,
                                          LogLevel::Error,
                                          [&file_outcome](Writer writer) -> ErrorCodeOr<void> {
                                              return fmt::FormatToWriter(writer,
                                                                         "failed to open log file: {}"_s,
                                                                         file_outcome.Error());
                                          });
                            return k_nullopt;
                        }

                        return file_outcome.ReleaseValue();
                    } else {
                        log_to_stderr(k_global_log_module,
                                      LogLevel::Error,
                                      [&o](Writer writer) -> ErrorCodeOr<void> {
                                          return fmt::FormatToWriter(writer,
                                                                     "failed to rename log file: {}"_s,
                                                                     o.Error());
                                      });
                        return k_nullopt;
                    }
                }

                log_to_stderr(k_global_log_module, LogLevel::Error, [](Writer writer) {
                    return writer.WriteChars("failed to open log file: too many attempts"_s);
                });
                return k_nullopt;
            }();

            if (!file) {
                log_to_stderr(k_global_log_module, level, write_message);
                return;
            }

            BufferedWriter<Kb(4)> buffered_writer {file->Writer()};
            DEFER { auto _ = buffered_writer.Flush(); };

            auto o = WriteFormattedLog(buffered_writer.Writer(),
                                       module_name.str,
                                       level,
                                       write_message,
                                       {
                                           .ansi_colors = false,
                                           .no_info_prefix = false,
                                           .timestamp = true,
                                       });
            if (o.HasError()) {
                log_to_stderr(k_global_log_module, LogLevel::Error, [o](Writer writer) {
                    return fmt::FormatToWriter(writer, "failed to write log file: {}"_s, o.Error());
                });
            }

            break;
        }
    }
}
