// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "utils/error_notifications.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "common_infrastructure/constants.hpp"
#include "common_infrastructure/sample_library/sample_library.hpp"

#include "directory_listing.hpp"
#include "rescan_mode.hpp"
#include "scanned_folder.hpp"

// TODO(1.0): this needs entirely replacing: use new ReadDirectoryChanges, AssetRefList, HashTable, etc
// Refer to the (now deleted) work on a sqlite based preset database if needed.
//
// NOTE: this has potential crashes. The lifetime of the listing can end while there are
// still jobs in the thread pool - which would then access deleted memory

struct PresetsListing {
    PresetsListing(String always_scanned_folder, ThreadsafeErrorNotifications& error_notifs);

    String const always_scanned_folder;
    ScannedFolder scanned_folder;
    ThreadsafeErrorNotifications& error_notifications;

    // 'double-buffer' technique
    Optional<DirectoryListing> listing {};
    MutexProtected<Optional<DirectoryListing>> listing_back {};
};

struct PresetMetadata {
    DynamicArrayBounded<sample_lib::LibraryIdRef, k_num_layers> used_libraries {};
};

struct PresetBrowserFilters {
    u64 selected_folder_hash {}; // IMPROVE: store an Entry pointer probably
    DynamicArrayBounded<char, 128> search_filter {};
};

void PresetListingChanged(PresetBrowserFilters& preset_browser_filters, DirectoryListing const* listing);

bool EntryMatchesSearchFilter(DirectoryListing::Entry const& entry,
                              DirectoryListing const& listing,
                              String search_filter,
                              DirectoryListing::Entry const* current_selected_folder);

enum class PresetSelectionMode { Adjacent, Random };

enum class PresetRandomiseMode {
    All,
    Folder,
    Library,
    BrowserFilters,
};

struct PresetLibraryInfo {
    sample_lib::LibraryId library_id;
    DynamicArrayBounded<char, 32> file_extension;
};

using PresetRandomiseCriteria =
    TaggedUnion<PresetRandomiseMode,
                TypeAndTag<PresetLibraryInfo, PresetRandomiseMode::Library>,
                TypeAndTag<PresetBrowserFilters, PresetRandomiseMode::BrowserFilters>>;

using PresetSelectionCriteria =
    TaggedUnion<PresetSelectionMode,
                TypeAndTag<DirectoryListing::AdjacentDirection, PresetSelectionMode::Adjacent>,
                TypeAndTag<PresetRandomiseCriteria, PresetSelectionMode::Random>>;

DirectoryListing::Entry const* SelectPresetFromListing(DirectoryListing const& listing,
                                                       PresetSelectionCriteria const& selection_criteria,
                                                       Optional<String> current_preset_path,
                                                       u64& random_seed);

struct PresetsFolderScanResult {
    bool is_loading {};
    DirectoryListing const* listing {}; // can be null
};

PresetsFolderScanResult FetchOrRescanPresetsFolder(PresetsListing& listing,
                                                   RescanMode mode,
                                                   Span<String const> extra_scan_folders,
                                                   ThreadPool* thread_pool);
