// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "directory_listing.hpp"

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"

DirectoryListing::Entry* DirectoryListing::Entry::LastChild() const {
    if (!FirstChild()) return nullptr;
    return FirstChild()->GetLastSibling();
}

DirectoryListing::Entry* DirectoryListing::Entry::GetLastSibling() const {
    if (!Next()) return (Entry*)this;
    auto ptr = Next();
    while (ptr) {
        if (!ptr->Next()) return ptr;
        ptr = ptr->Next();
    }
    return nullptr;
}

using RecursiveDirectoryIteratorAllocator = ArenaAllocatorWithInlineStorage<2000>;

DirectoryListing::Index DirectoryListing::Entry::NumChildren(bool recursive) const {
    if (!recursive) {
        Index n = 0;
        for (auto e = FirstChild(); e != nullptr; e = e->Next())
            ++n;
        return n;
    } else {
        RecursiveDirectoryIteratorAllocator allocator;
        Index count = 0;
        RecursiveTreeWalker recursive_walker {(Entry*)this, allocator};
        while (auto child = recursive_walker.Next(true))
            ++count;
        return count;
    }
}
DirectoryListing::Index DirectoryListing::Entry::NumChildrenFiles(bool recursive) const {
    if (!recursive) {
        Index n = 0;
        for (auto e = FirstChild(); e != nullptr; e = e->Next())
            if (e->IsFile()) ++n;
        return n;
    } else {
        RecursiveDirectoryIteratorAllocator allocator;
        Index count = 0;
        RecursiveTreeWalker recursive_walker {(Entry*)this, allocator};
        while (auto child = recursive_walker.Next(true))
            if (child->IsFile()) ++count;
        return count;
    }
}
DirectoryListing::Index DirectoryListing::Entry::NumChildrenDirectories(bool recursive) const {
    if (!recursive) {
        Index n = 0;
        for (auto e = FirstChild(); e != nullptr; e = e->Next())
            if (e->IsDirectory()) ++n;
        return n;
    } else {
        RecursiveDirectoryIteratorAllocator allocator;
        Index count = 0;
        RecursiveTreeWalker recursive_walker {(Entry*)this, allocator};
        while (auto child = recursive_walker.Next(true))
            if (child->IsDirectory()) ++count;
        return count;
    }
}

bool DirectoryListing::Entry::IsDecendentOf(Entry const* possible_parent) const {
    for (auto p = Parent(); p != nullptr; p = p->Parent())
        if (p == possible_parent) return true;
    return false;
}

DirectoryListing::Entry::Entry(String path, Type type, void* metadata)
    : m_path(path)
    , m_hash(::Hash(path))
    , m_type(type)
    , m_metadata(metadata) {}

void DirectoryListing::Entry::AddChild(Entry* child, Index child_index) {
    if (m_first_child == k_null_index) {
        m_first_child = child_index;
    } else {
        auto last = LastChild();
        last->m_next = child_index;
        child->m_prev = CheckedCast<Index>(last - child->m_entry_array);
        ASSERT(LastChild() == child);
    }
    child->m_parent = CheckedCast<Index>(this - m_entry_array);
}

//
//
//

DirectoryListing::DirectoryListing(Allocator& a) : m_arena(a), m_entries(a) {}

DirectoryListing::DirectoryListing(DirectoryListing&& other)
    : m_arena(Move(other.m_arena))
    , m_entries(Move(other.m_entries)) {

    Swap(m_recursive, other.m_recursive);
    Swap(m_num_files, other.m_num_files);
    Swap(m_num_directories, other.m_num_directories);
    Swap(m_file_name_wildcards, other.m_file_name_wildcards);
    Swap(m_root_paths, other.m_root_paths);
    Swap(m_last_scan_result, other.m_last_scan_result);
    Swap(m_roots, other.m_roots);
}

template <typename Iterator, typename CallbackType>
static ErrorCodeOr<void> IterateDirTemplate(String dir, CallbackType callback) {
    ASSERT(path::IsAbsolute(dir));
    ArenaAllocatorWithInlineStorage<1000> allocator;
    auto it = TRY(Iterator::Create(allocator, dir));
    while (it.HasMoreFiles()) {
        TRY(callback(it.Get()));
        TRY(it.Increment());
    }
    return k_success;
}

template <typename CallbackType>
static ErrorCodeOr<void> IterateDir(String dir, bool recursive, CallbackType callback) {
    if (recursive)
        return IterateDirTemplate<RecursiveDirectoryIterator>(dir, callback);
    else
        return IterateDirTemplate<DirectoryIterator>(dir, callback);
}

DirectoryListing::ScanResult DirectoryListing::Rescan() {
    ArenaAllocatorWithInlineStorage<4000> allocator;
    auto temp_root_paths = allocator.Clone(m_root_paths, CloneType::Deep);
    auto temp_wildcards = allocator.Clone(m_file_name_wildcards, CloneType::Deep);
    DynamicArray<Index> root_entry_indexes {allocator};

    m_num_directories = 0;
    m_num_files = 0;
    dyn::Clear(m_entries);
    m_arena.ResetCursorAndConsolidateRegions();
    m_file_name_wildcards = m_arena.Clone(temp_wildcards, CloneType::Deep);
    m_roots = m_arena.NewMultiple<Entry*>(temp_root_paths.size);
    m_root_paths = m_arena.AllocateExactSizeUninitialised<String>(temp_root_paths.size);
    for (auto const p_index : Range(temp_root_paths.size))
        m_root_paths[p_index] = CanonicalizePath(m_arena, temp_root_paths[p_index])
                                    .ValueOr(m_arena.Clone(temp_root_paths[p_index]));

    dyn::Append(m_entries, Entry {"All"_s, Entry::Type::Directory, nullptr});

    DynamicArray<ErrorWithPath> folder_errors {m_arena};
    folder_errors.Reserve(temp_root_paths.size);

    DynamicArray<ErrorWithPath> metadata_errors {m_arena};

    u32 successfully_scanned_folders = 0;

    for (auto root_path : m_root_paths) {
        auto const initial_num_entries = m_entries.size;

        dyn::Append(root_entry_indexes, CheckedCast<Index>(m_entries.size));

        if (m_entries.size == (Entry::k_last_valid_index + 1)) {
            dyn::Append(folder_errors,
                        ErrorWithPath {.path = "All"_s,
                                       .error = ErrorCode(FilesystemError::FolderContainsTooManyFiles)});
            break;
        }

        auto create_metadata = [&](String path) -> void* {
            if (m_create_metadata) {
                auto outcome = m_create_metadata(path, m_arena);
                if (outcome.HasError()) {
                    dyn::Append(metadata_errors, ErrorWithPath {.path = path, .error = outcome.Error()});
                    return nullptr;
                }
                return outcome.Value();
            }
            return nullptr;
        };

        dyn::Append(m_entries, Entry {root_path, Entry::Type::Directory, create_metadata(root_path)});

        auto callback = [this, &create_metadata](DirectoryEntry const& e) -> ErrorCodeOr<void> {
            if (m_entries.size == (Entry::k_last_valid_index + 1))
                return ErrorCode(FilesystemError::FolderContainsTooManyFiles);

            Entry::Type type {};
            if (e.type == FileType::Directory) {
                m_num_directories++;
                type = Entry::Type::Directory;
            } else if (e.type == FileType::File) {
                bool matches = false;
                for (auto wildcard : m_file_name_wildcards)
                    if (MatchWildcard(wildcard, path::Filename(e.path))) {
                        matches = true;
                        break;
                    }
                if (!matches) return k_success;
                m_num_files++;
                type = Entry::Type::File;
            } else {
                PanicIfReached();
                return k_success;
            }

            auto const entry_path = String(e.path).Clone(m_arena, CloneType::Shallow);
            dyn::Append(m_entries, Entry {entry_path, type, create_metadata(entry_path)});

            return k_success;
        };

        auto const outcome = IterateDir(root_path, m_recursive, callback);
        if (outcome.HasError())
            dyn::Append(folder_errors, ErrorWithPath {.path = root_path, .error = outcome.Error()});
        else
            successfully_scanned_folders++;

        // sort all child entries of this root
        Sort(m_entries.Items().SubSpan(initial_num_entries, m_entries.size - initial_num_entries),
             [](Entry const& a, Entry const& b) { return a.Path() < b.Path(); });
    }

    for (auto [index, e] : Enumerate<Index>(m_entries))
        e.m_entry_array = m_entries.data;

    for (auto [i, entry_index] : Enumerate(root_entry_indexes))
        m_roots[i] = &m_entries[entry_index];

    for (auto [index, e] : Enumerate<Index>(m_entries)) {
        if (index == 0) continue;

        bool is_root = false;
        for (auto root_entry : root_entry_indexes) {
            if (index == root_entry) {
                MasterRoot()->AddChild(&e, index);
                is_root = true;
            }
        }
        if (is_root) continue;

        auto parent = FindParentEntryOfPath(e.Path());
        ASSERT(parent != nullptr);
        parent->AddChild(&e, index);
    }

    m_last_scan_result = {
        .folder_successes = successfully_scanned_folders,
        .folder_errors = folder_errors.ToOwnedSpan(),
        .metadata_errors = metadata_errors.ToOwnedSpan(),
    };
    return m_last_scan_result;
}

DirectoryListing::ScanResult DirectoryListing::ScanFolders(Span<String const> paths,
                                                           bool recursive,
                                                           Span<String const> file_name_wildcards,
                                                           CreateMetadataFunction&& create_metadata) {
    m_recursive = recursive;
    m_file_name_wildcards = m_arena.Clone(file_name_wildcards, CloneType::Deep);
    m_root_paths = m_arena.Clone(paths, CloneType::Deep);
    m_create_metadata = Move(create_metadata);
    return Rescan();
}

bool DirectoryListing::ContainsHash(u64 hash) const {
    for (auto const& e : m_entries)
        if (e.Hash() == hash) return true;
    return false;
}

DirectoryListing::Entry const* DirectoryListing::Find(u64 hash) const {
    for (auto& e : m_entries)
        if (e.Hash() == hash) return &e;
    return nullptr;
}

DirectoryListing::Entry const* DirectoryListing::GetFirstFileEntry() const {
    for (auto& e : m_entries)
        if (e.IsFile()) return &e;
    return nullptr;
}

static bool ShouldSkipEntryInFileSearch(DirectoryListing::Entry const& e,
                                        DirectoryListing::SearchCriteria const& criteria) {
    if (!e.IsFile()) return true;

    // IMPROVE: if only one file matches the criteria, but it's also the one marked to skip then we should
    // allow it
    if (criteria.file_hash_to_skip && *criteria.file_hash_to_skip == e.Hash()) return true;
    if (criteria.required_file_extension && *criteria.required_file_extension != e.Extension()) return true;
    if (criteria.required_parent_folder_hash && e.Parent() &&
        (*criteria.required_parent_folder_hash != e.Parent()->Hash()))
        return true;
    if (criteria.meets_custom_requirement && (*criteria.meets_custom_requirement)(e) == false) return true;
    return false;
}

DirectoryListing::Index DirectoryListing::NumFiles(SearchCriteria search_criteria) const {
    Index result = 0;
    for (auto const& e : m_entries)
        if (!ShouldSkipEntryInFileSearch(e, search_criteria)) ++result;
    return result;
}

DirectoryListing::Entry const* DirectoryListing::GetRandomFile(u64& seed,
                                                               SearchCriteria search_criteria) const {
    auto const num_files = NumFiles(search_criteria);
    if (!num_files) return nullptr;

    auto const index = RandomIntInRange<usize>(seed, 0, num_files - 1);
    usize count = 0;
    for (auto& e : m_entries) {
        if (!ShouldSkipEntryInFileSearch(e, search_criteria)) {
            if (count == index) return &e;
            ++count;
        }
    }
    return nullptr;
}

DirectoryListing::Entry const*
DirectoryListing::GetNextFileEntryAtInterval(Entry const* e, AdjacentDirection direction) const {
    if (NumFiles() == 0) return nullptr;
    if (!e) return nullptr;
    if (NumFiles() == 1) return e;

    auto const index = (int)(e - m_entries.data);
    ASSERT(index >= 0 && index < (int)m_entries.size);

    auto const interval = ({
        int i;
        switch (direction) {
            case AdjacentDirection::Next: i = 1; break;
            case AdjacentDirection::Previous: i = -1; break;
        }
        i;
    });

    auto next = index + interval;
    while (next != index) {
        if (next >= (int)m_entries.size) next = 0;
        if (next < 0) next = (int)(m_entries.size - 1);
        if (m_entries[(usize)next].IsFile()) return &m_entries[(usize)next];
        next += interval;
    }
    return nullptr;
}

DirectoryListing::Entry* DirectoryListing::FindParentEntryOfPath(String path) {
    auto const dir = ({
        auto p = path::Directory(path);
        if (!p) return nullptr;
        *p;
    });
    auto const parent_hash = ::Hash(dir);
    for (auto& e : m_entries)
        if (parent_hash == e.Hash()) return &e;
    return nullptr;
}
