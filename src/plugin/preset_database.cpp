// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "preset_database.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/debug/debug.hpp"

#include "sqlite/sqlite3.h"
#include "state/state_coding.hpp"

// NOTES:
// - You don't need an AUTOINCREMENT primary ID - sqlite will populate an INTEGER PRIMARY KEY with unique
//   number automatically and with less CPU & memory overhead. https://www.sqlite.org/autoinc.html
// - You can't use the sqlite3_bind() APIs to parameterise table or column names, only _values_
// - The sqlite_stmt APIs are for a single statement
// - ON DELETE CASCASE means: if an item in the REFERENCES table is deleted, it will also delete any items the
//   table that correspond to it. The child is the table that contains the word REFERENCES, the parent is the
//   one that is referenced. If a parent row is deleted so are all rows in the child table that correspond to
//   it.
//

ErrorCodeCategory const sqlite_error_category {
    .category_id = "SQLITE",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        return writer.WriteChars(FromNullTerminated(sqlite3_errstr((int)code.code)));
    },
};

enum class CallbackResult { Continue, Abort };

using ExecCallback =
    TrivialFunctionRef<CallbackResult(int num_columns, char** column_texts, char** column_names)>;

static ErrorCodeOr<void> Exec(sqlite3* db, char const* sql, ExecCallback callback) {
    char* error_message {};
    auto rc = sqlite3_exec(
        db,
        sql,
        [](void* user_data, int num_columns, char** column_texts, char** column_names) -> int {
            if (!user_data) return 0;
            auto& callback = *(ExecCallback*)user_data;
            const auto r = callback(num_columns, column_texts, column_names);
            return (r == CallbackResult::Continue) ? 0 : 1;
        },
        callback ? &callback : nullptr,
        &error_message);
    DEFER { sqlite3_free(error_message); };

    if (rc != SQLITE_OK) {
        DebugLn("sqlite3_exec() failed: ({}) {}\n{}", rc, FromNullTerminated(error_message), sql);
        return ErrorCode(sqlite_error_category, rc);
    }
    return k_success;
}

static ErrorCodeOr<int> InsertOrGetUniqueId(ArenaAllocator& scratch_arena,
                                            sqlite3* db,
                                            String table,
                                            String value_key,
                                            String value,
                                            String id_key) {
    auto const sql =
        fmt::FormatStringReplace(scratch_arena,
                                 "INSERT OR IGNORE INTO <table> (<value_key>) VALUES ('<value>');\n"
                                 "SELECT <id_key> FROM <table> WHERE <value_key> = '<value>';\0",
                                 ArrayT<fmt::StringReplacement>({
                                     {"<table>", table},
                                     {"<value_key>", value_key},
                                     {"<value>", value},
                                     {"<id_key>", id_key},
                                 }));
    int id {};
    TRY(Exec(db, sql.data, [&](int num_cols, char** col_texts, char** col_names) {
        (void)col_names;
        if (num_cols == 1)
            id = (int)ParseInt(FromNullTerminated(col_texts[0]), ParseIntBase::Decimal).ValueOr(0);
        return CallbackResult::Continue;
    }));

    return id;
}

static void DebugPrintTable(ArenaAllocator& scratch_arena, sqlite3* db, String table) {
    DebugLn("Printing table: {}", table);
    char* error_message {};
    auto const rc = sqlite3_exec(
        db,
        fmt::Format(scratch_arena, "SELECT * FROM {}\0", table).data,
        [](void* user_data, int num_columns, char** column_texts, char** column_names) -> int {
            (void)user_data;
            for (int i = 0; i < num_columns; i++)
                DebugLn("{} = {}", column_names[i], column_texts[i] ? column_texts[i] : "NULL");
            DebugLn("---");
            return 0;
        },
        nullptr,
        &error_message);
    DEFER { sqlite3_free(error_message); };
    if (rc != SQLITE_OK) DebugLn("Failed to print table: {}", error_message);
}

ErrorCodeOr<sqlite3*> CreatePresetDatabase(ArenaAllocator&) {
    bool fatal_error = false;
    sqlite3* db {};
    int rc {};

    rc = sqlite3_open(":memory:", &db);
    DEFER {
        if (fatal_error) sqlite3_close(db);
    };
    if (rc != SQLITE_OK) {
        fatal_error = true;
        DebugLn("sqlite3_open() failed: {}", FromNullTerminated(sqlite3_errmsg(db)));
        return ErrorCode {sqlite_error_category, rc};
    }

    char const* schema = R"aaa(
PRAGMA foreign_keys = ON;

CREATE TABLE ScanFolders (
    ScanFolderId INTEGER PRIMARY KEY,
    Path TEXT NOT NULL UNIQUE
);
CREATE TABLE SubFolders (
    SubFolderId INTEGER PRIMARY KEY,
    SubPath TEXT NOT NULL UNIQUE
);
CREATE TABLE FileExtensions (
    FileExtensionId INTEGER PRIMARY KEY,
    Extension TEXT NOT NULL UNIQUE
);
CREATE TABLE Presets (
    PresetId INTEGER PRIMARY KEY,
    Name TEXT NOT NULL,
    FileExtensionId INTEGER NOT NULL,
    ScanFolderId INTEGER NOT NULL,
    SubFolderId INTEGER,
    FOREIGN KEY(FileExtensionId) REFERENCES FileExtensions(FileExtensionId),
    FOREIGN KEY(ScanFolderId) REFERENCES ScanFolders(ScanFolderId),
    FOREIGN KEY(SubFolderId) REFERENCES SubFolders(SubFolderId)
);
CREATE TABLE Libraries (
    LibraryId INTEGER PRIMARY KEY,
    Name TEXT NOT NULL UNIQUE
);
CREATE TABLE LibrariesJunction (
    PresetId INTEGER,
    LibraryId INTEGER,
    FOREIGN KEY(PresetId) REFERENCES Presets(PresetId) ON DELETE CASCADE,
    FOREIGN KEY(LibraryId) REFERENCES Libraries(LibraryId)
);

-- NOTE: we might just want to run this manually rather than as a trigger
-- because what about ON UPDATE?
CREATE TRIGGER remove_orphaned
AFTER DELETE ON Presets
BEGIN
    DELETE FROM ScanFolders
    WHERE ScanFolderId NOT IN (SELECT DISTINCT ScanFolderId FROM Presets);

    DELETE FROM SubFolders
    WHERE SubFolderID NOT IN (SELECT DISTINCT SubFolderId FROM Presets);

    DELETE FROM FileExtensions
    WHERE FileExtensionId NOT IN (SELECT DISTINCT FileExtensionId FROM Presets);
END;

CREATE TRIGGER remove_orphaned_libraries
AFTER DELETE ON LibrariesJunction
BEGIN
    DELETE FROM Libraries
    WHERE LibraryId NOT IN (SELECT DISTINCT LibraryId FROM LibrariesJunction);
END;
)aaa";

    char* error_message {};
    rc = sqlite3_exec(db, schema, nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        DEFER { sqlite3_free(error_message); };
        fatal_error = true;
        DebugLn("sqlite3_exec() failed making the schema: {}", FromNullTerminated(error_message));
        return ErrorCode {sqlite_error_category, rc};
    }

    return db;
}

void DestroyPresetDatabase(sqlite3* db) { sqlite3_close(db); }

static ErrorCodeOr<void> AddScanFolder(sqlite3* db,
                                       ArenaAllocator& scratch_arena,
                                       String folder,
                                       bool is_always_scanned_folder,
                                       ThreadsafeErrorNotifications& error_notifications) {
    auto const folder_error_id = ThreadsafeErrorNotifications::Id("prfo", folder);
    auto it = ({
        auto o = RecursiveDirectoryIterator::Create(scratch_arena, folder, "*.floe*");
        if (o.HasError()) {
            if (o.Error() == FilesystemError::PathDoesNotExist && is_always_scanned_folder) return k_success;
            auto item = error_notifications.NewError();
            item->value = {
                .title = "Failed to scan preset folder"_s,
                .message = folder,
                .error_code = o.Error(),
                .id = folder_error_id,
            };
            error_notifications.AddOrUpdateError(item);
            return o.Error();
        }
        error_notifications.RemoveError(folder_error_id);
        o.ReleaseValue();
    });

    int const scan_folder_id =
        TRY(InsertOrGetUniqueId(scratch_arena, db, "ScanFolders", "Path", folder, "ScanFolderId"));

    String const insert_preset_sql = "INSERT INTO Presets ("
                                     "Name,"
                                     "FileExtensionId,"
                                     "ScanFolderId,"
                                     "SubFolderId"
                                     ") VALUES (?, ?, ?, ?) RETURNING PresetId;";

    sqlite3_stmt* stmt;
    if (auto const rc =
            sqlite3_prepare_v2(db, insert_preset_sql.data, (int)insert_preset_sql.size, &stmt, nullptr);
        rc != SQLITE_OK) {
        DebugLn("sqlite3_prepare_v2() failed: {}", FromNullTerminated(sqlite3_errmsg(db)));
        return ErrorCode {sqlite_error_category, rc};
    }
    DEFER { sqlite3_finalize(stmt); };

    for (; it.HasMoreFiles(); ({
             auto o = it.Increment();
             if (o.HasError()) {
                 auto item = error_notifications.NewError();
                 item->value = {
                     .title = "Failed to scan preset folder"_s,
                     .message = folder,
                     .error_code = o.Error(),
                     .id = folder_error_id,
                 };
                 error_notifications.AddOrUpdateError(item);
                 return o.Error();
             }
         })) {
        auto const& entry = it.Get();
        if (entry.type != FileType::RegularFile) continue;

        auto const name = path::FilenameWithoutExtension(entry.path);
        auto const ext = path::Extension(entry.path);

        auto const file_error_id = ThreadsafeErrorNotifications::Id("prfi", entry.path);

        DynamicArrayInline<String, k_num_layers> libraries {};
        if (ext == FLOE_PRESET_FILE_EXTENSION) {
            auto file = ({
                auto o = OpenFile(entry.path, FileMode::Read);
                if (o.HasError()) {
                    auto item = error_notifications.NewError();
                    item->value = {
                        .title = "Failed to scan preset file"_s,
                        .message = entry.path.Items(),
                        .error_code = o.Error(),
                        .id = file_error_id,
                    };
                    error_notifications.AddOrUpdateError(item);
                    continue;
                }
                o.ReleaseValue();
            });

            StateSnapshot state {};
            if (auto o = CodeState(
                    state,
                    CodeStateOptions {
                        .mode = CodeStateOptions::Mode::Decode,
                        .read_or_write_data = [&file](void* data, usize bytes) -> ErrorCodeOr<void> {
                            TRY(file.Read(data, bytes));
                            return k_success;
                        },
                        .source = StateSource::PresetFile,
                        .abbreviated_read = true,
                    });
                o.HasError()) {
                auto item = error_notifications.NewError();
                item->value = {
                    .title = "Preset is invalid"_s,
                    .message = entry.path.Items(),
                    .error_code = o.Error(),
                    .id = file_error_id,
                };
                error_notifications.AddOrUpdateError(item);
                continue;
            }

            for (auto [index, inst] : Enumerate(state.insts))
                if (auto s = inst.TryGet<sample_lib::InstrumentId>())
                    dyn::AppendIfNotAlreadyThere(libraries, scratch_arena.Clone(s->library_name));
        } else {
            struct Version1Library {
                String name;
                String file_extension;
            };
            static constexpr auto k_v1_libs = ArrayT<Version1Library>({
                {"Abstract Energy", "abstract"},
                {"Arctic Strings", "strings"},
                {"Deep Conjuring", "dcii"},
                {"Dreamstates", "dreams"},
                {"Feedback Loops", "feedback"},
                {"Isolated Signals", "isosig"},
                {"Lost Reveries", "lostrev"},
                {"Music Box Suite Free", "music-box-free"},
                {"Music Box Suite", "music-box"},
                {"Paranormal", "paranormal"},
                {"Phoenix", "phoenix"},
                {"Scare Tactics", "scare"},
                {"Scenic Vibrations", "scenic-vibrations"},
                {"Signal Interference", "signal"},
                {"Slow", "slow"},
                {"Squeaky Gate", "gate"},
                {"Terracotta", "terracotta"},
                {"Wraith Demo", "wraith-demo"},
                {"Wraith", "wraith"},
            });
            auto const ext_suffix = ext.SubSpan(".floe-"_s.size);
            for (auto const& l : k_v1_libs)
                if (ext_suffix == l.file_extension) {
                    dyn::Append(libraries, l.name);
                    break;
                }
        }

        DynamicArrayInline<int, k_num_layers> library_ids {};
        for (auto lib : libraries) {
            TRY(Exec(db,
                     fmt::FormatStringReplace(scratch_arena,
                                              "INSERT OR IGNORE INTO Libraries (Name) VALUES ('<name>');\n"
                                              "SELECT LibraryId FROM Libraries WHERE Name = '<name>';\0",
                                              ArrayT<fmt::StringReplacement>({
                                                  {"<name>", lib},
                                              }))
                         .data,
                     [&](int num_cols, char** col_texts, char** col_names) {
                         (void)col_names;
                         if (num_cols == 1)
                             dyn::AppendIfNotAlreadyThere(
                                 library_ids,
                                 (int)ParseInt(FromNullTerminated(col_texts[0]), ParseIntBase::Decimal)
                                     .ValueOr(0));
                         return CallbackResult::Continue;
                     }));
        }

        int const ext_id = TRY(
            InsertOrGetUniqueId(scratch_arena, db, "FileExtensions", "Extension", ext, "FileExtensionId"));

        Optional<int> const sub_folder_id = ({
            const auto subdirs = path::Directory(String(entry.path).SubSpan(folder.size + 1));
            Optional<int> id {};
            if (subdirs)
                id = TRY(
                    InsertOrGetUniqueId(scratch_arena, db, "SubFolders", "SubPath", *subdirs, "SubFolderId"));
            id;
        });

        sqlite3_bind_text(stmt, 1, name.data, (int)name.size, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, ext_id);
        sqlite3_bind_int(stmt, 3, scan_folder_id);
        if (sub_folder_id)
            sqlite3_bind_int(stmt, 4, *sub_folder_id);
        else
            sqlite3_bind_null(stmt, 4);

        if (auto const rc = sqlite3_step(stmt); rc != SQLITE_ROW) {
            DebugLn("sqlite3_step() failed: {}", FromNullTerminated(sqlite3_errmsg(db)));
            return ErrorCode {sqlite_error_category, rc};
        }

        if (library_ids.size) {
            auto const preset_id = sqlite3_column_int(stmt, 0);
            DynamicArray<char> sql {scratch_arena};
            dyn::AppendSpan(sql, "INSERT INTO LibrariesJunction (PresetId, LibraryId) VALUES");
            for (auto lib_id : library_ids)
                fmt::Append(sql, " ({}, {}),", preset_id, lib_id);
            Last(sql) = ';';
            TRY(Exec(db, dyn::NullTerminated(sql), nullptr));
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        error_notifications.RemoveError(file_error_id);
    }

    return k_success;
}

ErrorCodeOr<void> RescanPresetDatabase(sqlite3* db,
                                       ArenaAllocator& scratch_arena,
                                       Span<String> always_scanned_folders,
                                       Span<String> extra_scan_folders,
                                       ThreadsafeErrorNotifications& error_notifications) {
    for (auto scan_folders : Array {always_scanned_folders, extra_scan_folders})
        for (auto folder : scan_folders)
            TRY(AddScanFolder(db,
                              scratch_arena,
                              folder,
                              scan_folders.data == always_scanned_folders.data,
                              error_notifications));

    DebugPrintTable(scratch_arena, db, "Presets");
    DebugPrintTable(scratch_arena, db, "FileExtensions");
    DebugPrintTable(scratch_arena, db, "ScanFolders");
    DebugPrintTable(scratch_arena, db, "SubFolders");
    DebugPrintTable(scratch_arena, db, "Libraries");
    DebugPrintTable(scratch_arena, db, "LibrariesJunction");

    return k_success;
}

//=================================================
//  _______        _
// |__   __|      | |
//    | | ___  ___| |_ ___
//    | |/ _ \/ __| __/ __|
//    | |  __/\__ \ |_\__ \
//    |_|\___||___/\__|___/
//
//=================================================

TEST_CASE(TestPresetDatabase) {
    auto& scratch_arena = tester.scratch_arena;

    auto db = TRY(CreatePresetDatabase(scratch_arena));
    DEFER { DestroyPresetDatabase(db); };

    ThreadsafeErrorNotifications errors;
    TRY(RescanPresetDatabase(db,
                             scratch_arena,
                             {},
                             Array {
                                 (String)path::Join(scratch_arena,
                                                    ConcatArrays(Array {TestFilesFolder(tester)},
                                                                 k_repo_subdirs_floe_test_presets)),
                             },
                             errors));

    return k_success;
}

TEST_REGISTRATION(FloePresetDatabaseTests) { REGISTER_TEST(TestPresetDatabase); }
