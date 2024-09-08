// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presets_folder.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "tests/framework.hpp"
#include "utils/directory_listing/directory_listing.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/sample_library/sample_library.hpp"

#include "rescan_mode.hpp"
#include "scanned_folder.hpp"
#include "state/state_coding.hpp"

PresetsListing::PresetsListing(String always_scanned_folder,
                               ThreadsafeErrorNotifications& error_notifications)
    : always_scanned_folder(always_scanned_folder)
    , scanned_folder(true)
    , error_notifications(error_notifications) {}

bool EntryMatchesSearchFilter(DirectoryListing::Entry const& entry,
                              DirectoryListing const& listing,
                              String search_filter,
                              DirectoryListing::Entry const* current_selected_folder) {
    if (current_selected_folder && !entry.IsDecendentOf(current_selected_folder)) return false;
    if (!search_filter.size) return true;

    auto top_level_root = entry.Parent();
    for (; top_level_root->Parent() != listing.MasterRoot(); top_level_root = top_level_root->Parent()) {
    }
    ASSERT(Find(listing.Roots(), top_level_root));
    auto path = entry.Path();
    ASSERT(StartsWithSpan(path, top_level_root->Path()));
    path.RemovePrefix(top_level_root->Path().size);

    return ContainsCaseInsensitiveAscii(path, search_filter);
}

void PresetListingChanged(PresetBrowserFilters& browser_filters, DirectoryListing const* listing) {
    if (listing) {
        if (!browser_filters.selected_folder_hash ||
            !listing->ContainsHash(browser_filters.selected_folder_hash)) {
            browser_filters.selected_folder_hash = listing->MasterRoot()->Hash();
        }
    }
}

DirectoryListing::Entry const* SelectPresetFromListing(DirectoryListing const& listing,
                                                       PresetSelectionCriteria const& selection_criteria,
                                                       Optional<String> current_preset_path,
                                                       u64& random_seed) {
    if (!listing.NumFiles()) return nullptr;

    // IMPROVE: find a better solution than a string comparison to match the preset path
    auto const current_entry = current_preset_path ? listing.Find(*current_preset_path) : nullptr;
    auto const current_hash = current_entry ? current_entry->Hash() : 0;

    switch (selection_criteria.tag) {
        case PresetSelectionMode::Adjacent: {
            auto const& direction = selection_criteria.Get<DirectoryListing::AdjacentDirection>();
            DirectoryListing::Entry const* p = nullptr;
            if (current_entry)
                p = listing.GetNextFileEntryAtInterval(current_entry, direction);
            else
                p = listing.GetFirstFileEntry();
            return p;
        }
        case PresetSelectionMode::Random: {
            auto const& random_criteria = selection_criteria.Get<PresetRandomiseCriteria>();
            switch (random_criteria.tag) {
                case PresetRandomiseMode::All: {
                    return listing.GetRandomFile(random_seed,
                                                 {
                                                     .file_hash_to_skip = current_hash,
                                                 });
                }
                case PresetRandomiseMode::Folder: {
                    if (current_entry && current_entry->Parent()) {
                        auto const current_parent_hash = current_entry->Parent()->Hash();
                        return listing.GetRandomFile(random_seed,
                                                     {
                                                         .file_hash_to_skip = current_hash,
                                                         .required_parent_folder_hash = current_parent_hash,
                                                     });
                    }
                    break;
                }
                case PresetRandomiseMode::Library: {
                    auto const& library_info = random_criteria.Get<PresetLibraryInfo>();
                    return listing.GetRandomFile(
                        random_seed,
                        {
                            .file_hash_to_skip = current_hash,
                            .meets_custom_requirement =
                                [&library_info = library_info](DirectoryListing::Entry const& entry) {
                                    if (entry.Metadata()) {
                                        auto const& meta = *(PresetMetadata const*)entry.Metadata();
                                        for (auto& l : meta.used_libraries)
                                            if (l == library_info.library_id) return true;
                                    }

                                    if (EndsWithSpan(entry.Path(), library_info.file_extension)) return true;

                                    return false;
                                },
                        });

                    break;
                }
                case PresetRandomiseMode::BrowserFilters: {
                    auto const& browser_filters = random_criteria.Get<PresetBrowserFilters>();

                    auto const selected_folder = listing.Find(browser_filters.selected_folder_hash);
                    ASSERT(selected_folder);

                    return listing.GetRandomFile(random_seed,
                                                 {
                                                     .file_hash_to_skip = current_hash,
                                                     .meets_custom_requirement =
                                                         [&](DirectoryListing::Entry const& entry) {
                                                             return EntryMatchesSearchFilter(
                                                                 entry,
                                                                 listing,
                                                                 browser_filters.search_filter,
                                                                 selected_folder);
                                                         },
                                                 });
                }
            }
            break;
        }
    }

    return nullptr;
}

PresetsFolderScanResult FetchOrRescanPresetsFolder(PresetsListing& listing,
                                                   RescanMode mode,
                                                   Span<String const> extra_scan_folders,
                                                   ThreadPool* thread_pool) {
    ArenaAllocatorWithInlineStorage<1000> scratch_arena;
    DynamicArray<String> scan_folders {scratch_arena};
    dyn::Append(scan_folders, listing.always_scanned_folder);
    dyn::AppendSpan(scan_folders, extra_scan_folders);

    auto const is_loading = HandleRescanRequest(
        listing.scanned_folder,
        thread_pool,
        mode,
        scan_folders,
        [&listing](Span<String const> folders_to_scan) {
            BeginScan(listing.scanned_folder);

            DirectoryListing new_listing {PageAllocator::Instance()};
            auto const errors = new_listing.ScanFolders(
                folders_to_scan,
                true,
                Array {"*.mirage*"_s, "*" FLOE_PRESET_FILE_EXTENSION},
                [](String path, ArenaAllocator& arena) -> ErrorCodeOr<void*> {
                    if (path::Extension(path) == FLOE_PRESET_FILE_EXTENSION) {
                        auto file = TRY_I(OpenFile(path, FileMode::Read));

                        StateSnapshot state {};
                        TRY(CodeState(
                            state,
                            CodeStateArguments {
                                .mode = CodeStateArguments::Mode::Decode,
                                .read_or_write_data = [&file](void* data, usize bytes) -> ErrorCodeOr<void> {
                                    TRY(file.Read(data, bytes));
                                    return k_success;
                                },
                                .source = StateSource::PresetFile,
                                .abbreviated_read = true,
                            }));

                        auto result = arena.New<PresetMetadata>();
                        PLACEMENT_NEW(result) PresetMetadata();
                        for (auto& i : state.inst_ids) {
                            if (auto s = i.TryGet<sample_lib::InstrumentId>()) {
                                if (!Find(result->used_libraries, s->library))
                                    dyn::Append(result->used_libraries, s->library.Ref().Clone(arena));
                            }
                        }
                        return (void*)result;
                    }
                    return nullptr;
                });

            auto const error_id = [](String path) {
                return ((u64)U32FromChars("pres") << 32) | Hash32(path);
            };
            for (auto& err : errors.folder_errors) {
                auto item = listing.error_notifications.NewError();
                item->value = {
                    .title = "Failed to scan preset folder"_s,
                    .message = err.path,
                    .error_code = err.error,
                    .id = error_id(err.path),
                };
                listing.error_notifications.AddOrUpdateError(item);
            }
            for (auto& err : errors.metadata_errors) {
                auto item = listing.error_notifications.NewError();
                item->value = {
                    .title = "Failed to read preset file"_s,
                    .message = err.path,
                    .error_code = err.error,
                    .id = error_id(err.path),
                };
                listing.error_notifications.AddOrUpdateError(item);
            }
            for (auto& entry : new_listing.Roots())
                listing.error_notifications.RemoveError(error_id(entry->Path()));

            listing.listing_back.Use(
                [&new_listing](Optional<DirectoryListing>& l) { l = Move(new_listing); });

            EndScan(listing.scanned_folder);
        });

    listing.listing_back.Use([&listing](Optional<DirectoryListing>& l) {
        if (l) {
            listing.listing = l.ReleaseValue();
            l.Clear();
        }
    });

    return {
        .is_loading = is_loading,
        .listing = listing.listing ? &*listing.listing : nullptr,
    };
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

static ErrorCodeOr<DirectoryListing> TestListing(tests::Tester& tester) {
    DirectoryListing listing(tester.scratch_arena);

    auto result = listing.ScanFolders(
        Array {(String)path::Join(tester.scratch_arena,
                                  Array {TestFilesFolder(tester), k_repo_subdirs_floe_test_presets})},
        false,
        Array {"*.mirage-*"_s, "*" FLOE_PRESET_FILE_EXTENSION},
        {});
    if (result.folder_errors.size) return result.folder_errors[0].error;
    return listing;
}

TEST_CASE(TestPresetBrowserFilters) {
    auto listing = TRY(TestListing(tester));

    // Always contains a valid selected folder
    {
        PresetBrowserFilters filters {};
        PresetListingChanged(filters, nullptr);
        CHECK(filters.selected_folder_hash == 0);

        PresetListingChanged(filters, &listing);
        CHECK(filters.selected_folder_hash != 0);

        PresetListingChanged(filters, nullptr);
        CHECK(filters.selected_folder_hash != 0);

        u64 const made_up_hash = 903242;
        filters.selected_folder_hash = made_up_hash;
        PresetListingChanged(filters, &listing);
        CHECK(filters.selected_folder_hash != made_up_hash);
    }

    // Filtering works
    {
        PresetBrowserFilters filters {};
        PresetListingChanged(filters, &listing);

        auto selected = listing.Find(filters.selected_folder_hash);
        REQUIRE(selected);
        auto first_file = listing.GetFirstFileEntry();
        REQUIRE(first_file);

        CHECK(EntryMatchesSearchFilter(*first_file, listing, {}, selected));
        CHECK(EntryMatchesSearchFilter(*first_file, listing, "mirage", selected));
        CHECK(EntryMatchesSearchFilter(*first_file, listing, "MIRAGE", selected));
        CHECK(!EntryMatchesSearchFilter(*first_file, listing, "00000", selected));
        CHECK(!EntryMatchesSearchFilter(*first_file, listing, "floe", selected));
    }

    return k_success;
}

#if 0
TEST_CASE(TestCurrentPresetInfo) {
    auto listing = TRY(TestListing(tester.scratch_arena));
    CoreLibrary core;
    u64 random_seed = SeedFromTime();

    // Correctly updates preset info
    {
        PresetInfo current_preset_info;

        SetPresetInfo(current_preset_info, k_nullopt, nullptr, nullptr);
        CHECK_EQ(current_preset_info.Name(), k_default_preset_name);
        CHECK(current_preset_info.listing_entry == nullptr);

        SetPresetInfo(current_preset_info, "just name", nullptr, nullptr);
        CHECK_EQ(current_preset_info.Name(), "just name"_s);
        CHECK(current_preset_info.listing_entry == nullptr);

        const auto entry = listing.GetFirstFileEntry();
        const auto path = entry->Path();
        SetPresetInfo(current_preset_info, path, nullptr, nullptr);
        CHECK(current_preset_info.Name().Size());
        CHECK(current_preset_info.Path());
        CHECK(current_preset_info.listing_entry == nullptr);

        SetPresetInfo(current_preset_info, path, nullptr, &listing);
        CHECK(current_preset_info.Name().Size());
        CHECK(current_preset_info.Path());
        CHECK_EQ(current_preset_info.listing_entry, entry);
    }

    // LoadPresetFromListing
    {
        PresetInfo current_preset_info;
        const auto entry = listing.GetFirstFileEntry();

        const auto state = TRY(LoadPresetFromListing(*entry, listing, current_preset_info, core.core));
        CHECK_EQ(current_preset_info.Name(), entry->FilenameNoExt());
        REQUIRE(current_preset_info.Path());
        CHECK_EQ(String(*current_preset_info.Path()), entry->Path());
        CHECK_EQ(current_preset_info.listing_entry, entry);
    }

    // Next
    {
        PresetInfo current_preset_info;

        auto opt = LoadPresetFromListing(
            PresetSelectionCriteria {
                .type = PresetSelectionCriteria::Type::Direction,
                .direction = DirectoryListing::Direction::Next,
            },
            listing,
            current_preset_info,
            random_seed,
            core.core);
        REQUIRE(opt);
        const auto state = TRY(opt.Value());
        CHECK_NEQ(current_preset_info.Name().Size(), 0uz);
        REQUIRE(current_preset_info.Path());
    }

    return k_success;
}
#endif

TEST_REGISTRATION(RegisterPresetTests) { REGISTER_TEST(TestPresetBrowserFilters); }
