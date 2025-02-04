// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_file.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "os/misc.hpp"

bool IsOnlineReportingDisabled() {
    ArenaAllocatorWithInlineStorage<Kb(4)> arena {PageAllocator::Instance()};
    auto try_read = [&]() -> ErrorCodeOr<bool> {
        String file_data {};
        {
            auto file = TRY(OpenFile(SettingsFilepath(),
                                     {
                                         .capability = FileMode::Capability::Read,
                                         .share = FileMode::Share::ReadWrite,
                                         .creation = FileMode::Creation::OpenExisting,
                                     }));
            TRY(file.Lock({.type = FileLockOptions::Type::Shared}));
            DEFER { auto _ = file.Unlock(); };

            file_data = TRY(file.ReadWholeFile(arena));
        }

        bool online_reporting_disabled = false;

        for (auto const line : SplitIterator {.whole = file_data, .token = '\n', .skip_consecutive = true}) {
            if (StartsWith(line, ';')) continue;

            if (ini::SetIfMatching(line,
                                   ini::Key(ini::KeyType::OnlineReportingDisabled),
                                   online_reporting_disabled))
                break;
        }

        return online_reporting_disabled;
    };

    auto const outcome = try_read();
    if (outcome.HasError()) {
        if (outcome.Error() == FilesystemError::PathDoesNotExist) return false;

        // We couldn't read the file, so we can't know either way. It could just be an temporary filesystem
        // error, so we can't assume the user's preference so we'll say reporting is disabled.
        return true;
    }

    return outcome.Value();
}
