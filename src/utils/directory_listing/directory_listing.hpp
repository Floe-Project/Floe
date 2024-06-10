// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// TODO: probably remove this in favour of ad-hoc data structures that use DirectoryIterator

#pragma once
#include "foundation/foundation.hpp"

class DirectoryListing {
  public:
    using Index = u16;
    using CreateMetadataFunction =
        TrivialFixedSizeFunction<32, ErrorCodeOr<void*>(String path, ArenaAllocator& arena)>;

    class Entry {
      public:
        static constexpr Index k_null_index = LargestRepresentableValue<Index>();
        static constexpr Index k_last_valid_index = k_null_index - 1;

        enum class Type { File, Directory };

        class RecursiveTreeWalker {
          public:
            RecursiveTreeWalker(Entry* root, Allocator& a) : m_root(root), m_stack(a) {
                m_stack.Reserve(32);
                Reset();
            }

            void Reset() {
                dyn::Clear(m_stack);
                dyn::Append(m_stack, m_root);
            }

            Entry* Next(bool skip_root) {
                Entry* next;
                if (!m_stack.size) return nullptr;
                next = Last(m_stack);
                ASSERT(next != nullptr);
                dyn::Pop(m_stack);
                for (auto child = next->LastChild(); child != nullptr; child = child->Prev())
                    dyn::Append(m_stack, child);
                if (skip_root && next == m_root) return Next(skip_root);
                return next;
            }

          private:
            Entry* m_root;
            DynamicArray<Entry*> m_stack;
        };

        friend class DirectoryListing;

        bool IsDirectory() const { return m_type == Type::Directory; }
        bool IsFile() const { return m_type == Type::File; }
        bool HasChildren() const { return FirstChild() != nullptr; }
        bool HasSiblings() const { return Next() != nullptr || Prev() != nullptr; }
        bool IsFirstSibling() const { return Prev() == nullptr; }
        bool IsLastSibling() const { return Next() == nullptr; }

        String Filename() const { return path::Filename(m_path); }
        String FilenameNoExt() const { return path::FilenameWithoutExtension(m_path); }
        String Extension() const { return path::Extension(m_path); }
        String Path() const { return m_path; }

        void* Metadata() const { return m_metadata; }

        Entry* Next() const { return (m_next == k_null_index) ? nullptr : &m_entry_array[m_next]; }
        Entry* Prev() const { return (m_prev == k_null_index) ? nullptr : &m_entry_array[m_prev]; }
        Entry* Parent() const { return (m_parent == k_null_index) ? nullptr : &m_entry_array[m_parent]; }
        Entry* FirstChild() const {
            return (m_first_child == k_null_index) ? nullptr : &m_entry_array[m_first_child];
        }
        Entry* LastChild() const;
        u64 Hash() const { return m_hash; }

        Entry* GetLastSibling() const;

        Index NumChildren(bool recursive) const;
        Index NumChildrenFiles(bool recursive) const;
        Index NumChildrenDirectories(bool recursive) const;

        bool IsDecendentOf(Entry const* possible_parent) const;

        bool operator==(Entry const& other) const { return m_hash == other.m_hash; }

      private:
        Entry(String path, Type type, void* metadata);

        void AddChild(Entry* child, Index index);

        String m_path; // utf8
        u64 m_hash;
        Type m_type;
        void* m_metadata;
        Entry* m_entry_array {};
        Index m_next {k_null_index};
        Index m_prev {k_null_index};
        Index m_parent {k_null_index};
        Index m_first_child {k_null_index};
    };

    struct ScanResult {
        u32 folder_successes;
        Span<ErrorWithPath> folder_errors;
        Span<ErrorWithPath> metadata_errors;
    };

    struct SearchCriteria {
        Optional<u64> file_hash_to_skip;
        Optional<String> required_file_extension;
        Optional<u64> required_parent_folder_hash;
        Optional<FunctionRef<bool(Entry const&)>> meets_custom_requirement;
    };

    NON_COPYABLE(DirectoryListing);
    DirectoryListing(DirectoryListing&& other);

    DirectoryListing(Allocator& alloc);

    [[nodiscard]] ScanResult Rescan();
    [[nodiscard]] ScanResult ScanFolders(Span<String const> path,
                                         bool recursive,
                                         Span<String const> file_name_wildcards,
                                         CreateMetadataFunction&& create_metadata);

    ScanResult LastScanResult() const { return m_last_scan_result; }

    Entry const* MasterRoot() const { return &m_entries[0]; }
    Entry* MasterRoot() { return &m_entries[0]; }
    Span<Entry*> Roots() const { return m_roots; }
    Span<Entry const> Entries() const { return m_entries; }

    Index NumDirectories() const { return m_num_directories; }
    Index NumEntries() const { return CheckedCast<Index>(m_entries.size - 1 - m_roots.size); }
    Index NumFiles() const { return m_num_files; }
    Index NumFiles(SearchCriteria search_criteria) const;

    bool ContainsHash(u64 hash) const;

    Entry const* Find(u64 hash) const;
    Entry const* Find(String path) const { return Find(Hash(path)); }

    enum class AdjacentDirection { Next, Previous };
    Entry const* GetNextFileEntryAtInterval(Entry const* e, AdjacentDirection direction) const;
    Entry const* GetRandomFile(u64& seed, SearchCriteria criteria) const;
    Entry const* GetFirstFileEntry() const;

  private:
    Entry* FindParentEntryOfPath(String path);

    ArenaAllocator m_arena;
    bool m_recursive {};
    Index m_num_files {};
    Index m_num_directories {};
    Span<String> m_file_name_wildcards {};
    CreateMetadataFunction m_create_metadata {};
    Span<String> m_root_paths {};
    ScanResult m_last_scan_result {};
    Span<Entry*> m_roots {};
    DynamicArray<Entry> m_entries;
};
