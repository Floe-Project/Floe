// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"
#include "os/threading.hpp"
#include "utils/error_notifications.hpp"
#include "utils/thread_extra/atomic_ref_list.hpp"
#include "utils/thread_extra/thread_extra.hpp"
#include "utils/thread_extra/thread_pool.hpp"

#include "audio_data.hpp"
#include "common/constants.hpp"
#include "sample_library/sample_library.hpp"

// Requirements:
// 1. Asynchronous
// 2. Fast; especially for already-loaded assets
// 3. In-progress loads that are no longer needed should be aborted
// 4. The main-thread should be able to retrieve the loading percentage for instruments
// 5. Each asset should not be duplicated in memory
// 6. Unused assets should be freed

namespace sample_lib_loader {

using RequestId = u64;

struct LoadedInstrument {
    sample_lib::Instrument const& instrument;
    Span<AudioData const*> audio_datas {}; // parallel to instrument.regions
    AudioData const* file_for_gui_waveform {};
};

enum class AssetType { Instrument, Ir };

struct InstrumentIdWithLayer {
    sample_lib::InstrumentId id;
    u32 layer_index;
};

using LoadRequest = TaggedUnion<AssetType,
                                TypeAndTag<InstrumentIdWithLayer, AssetType::Instrument>,
                                TypeAndTag<sample_lib::IrId, AssetType::Ir>>;

enum class RefCountChange { Retain, Release };

template <typename Type>
struct RefCounted {
    RefCounted() = default;
    RefCounted(Type& t, Atomic<u32>& r, WorkSignaller* s) : m_data(&t), m_refs(&r), m_work_signaller(s) {}

    inline void Retain() const {
        if (m_refs) m_refs->FetchAdd(1, MemoryOrder::Relaxed);
    }
    inline void Release() const {
        if (m_refs) {
            auto prev = m_refs->SubFetch(1, MemoryOrder::Relaxed);
            ASSERT(prev != ~(u32)0);
            if (prev == 0 && m_work_signaller) m_work_signaller->Signal();
        }
    }
    inline void Assign(RefCounted const& other) {
        Release();
        other.Retain();
        m_data = other.m_data;
        m_refs = other.m_refs;
        m_work_signaller = other.m_work_signaller;
    }
    inline void ChangeRefCount(RefCountChange t) const {
        switch (t) {
            case RefCountChange::Retain: Retain(); break;
            case RefCountChange::Release: Release(); break;
        }
    }

    constexpr explicit operator bool() const { return m_data != nullptr; }
    constexpr Type const* operator->() const { return m_data; }
    constexpr Type const& operator*() const { return *m_data; }

  private:
    Type const* m_data {};
    Atomic<u32>* m_refs {};
    WorkSignaller* m_work_signaller {};
};

using AssetRefUnion = TaggedUnion<AssetType,
                                  TypeAndTag<RefCounted<LoadedInstrument>, AssetType::Instrument>,
                                  TypeAndTag<RefCounted<AudioData>, AssetType::Ir>>;

struct LoadResult {
    enum class ResultType { Success, Error, Cancelled };
    using Result = TaggedUnion<ResultType,
                               TypeAndTag<AssetRefUnion, ResultType::Success>,
                               TypeAndTag<ErrorCode, ResultType::Error>>;

    void ChangeRefCount(RefCountChange t) const;
    void Retain() const { ChangeRefCount(RefCountChange::Retain); }
    void Release() const { ChangeRefCount(RefCountChange::Release); }

    RequestId id;
    Result result;
};

namespace detail {

extern u32 g_inst_debug_id;

enum class LoadingState {
    PendingLoad,
    PendingCancel,
    Loading,
    CompletedSucessfully,
    CompletedWithError,
    CompletedCancelled,
};

using AudioDataAllocator = PageAllocator;

struct ListedAudioData {
    ~ListedAudioData();

    DynamicArrayInline<char, k_max_library_name_size> library_name {};
    String path;
    AudioData audio_data;
    Atomic<u32> refs {};
    Atomic<LoadingState> state {LoadingState::PendingLoad};
    Optional<ErrorCode> error {};
};

struct ListedInstrument {
    ~ListedInstrument();

    u32 debug_id {g_inst_debug_id++};
    LoadedInstrument inst;
    Atomic<u32> refs {};
    Atomic<u32>& library_refs;
    Span<ListedAudioData*> audio_data_set {};
    ArenaAllocator arena {PageAllocator::Instance()};
};

struct ListedLibrary {
    ~ListedLibrary() { ASSERT(instruments.Empty(), "missing instrument dereference"); }

    ArenaAllocator arena;
    sample_lib::Library* lib {};

    List<ListedInstrument> instruments {PageAllocator::Instance()};
};

using LibrariesList = AtomicRefList<ListedLibrary>;

} // namespace detail

using LoadCompletedCallback = TrivialFixedSizeFunction<40, void(LoadResult)>;

struct Connection {
    // -1 if not valid, else 0 to 100
    Array<Atomic<s32>, k_num_layers> instrument_loading_percents {};

    // private
    ThreadsafeErrorNotifications& error_notifications;
    Array<detail::ListedInstrument*, k_num_layers> desired_inst {};
    LoadCompletedCallback completed_callback;
    Atomic<bool> used {};
};

struct LoadingThread;

struct AvailableLibraries {
    struct ScanFolder {
        enum class Source { AlwaysScannedFolder, ExtraFolder };
        enum class State { NotScanned, RescanRequested, Scanning, ScannedSuccessfully, ScanFailed };
        DynamicArray<char> path {Malloc::Instance()};
        Source source {};
        Atomic<State> state {State::NotScanned};
    };

    using ScanFolderList = AtomicRefList<ScanFolder>;

    AvailableLibraries(Span<String const> always_scanned_folders,
                       ThreadsafeErrorNotifications& error_notifications);
    ~AvailableLibraries();

    // threadsafe
    void SetExtraScanFolders(Span<String const>);

    // main-thread, you must call Release on all results
    Span<RefCounted<sample_lib::Library>> AllRetained(ArenaAllocator& arena);
    RefCounted<sample_lib::Library> FindRetained(String name);

    // loading-thread
    void AttachLoadingThread(LoadingThread* t);

    // internal
    LoadingThread* loading_thread {};
    Mutex scan_folders_writer_mutex;
    ScanFolderList scan_folders;
    ThreadsafeErrorNotifications& error_notifications;
    detail::LibrariesList libraries;
    Mutex libraries_by_name_mutex;
    DynamicHashTable<String, detail::LibrariesList::Node*> libraries_by_name {Malloc::Instance()};
};

struct LoadingThread {
    struct QueuedRequest {
        RequestId id;
        LoadRequest request;
        Connection& connection;
    };

    LoadingThread(ThreadPool& pool, AvailableLibraries& libs);
    ~LoadingThread();

    Atomic<u64> total_bytes_used_by_samples {};
    Atomic<u32> num_insts_loaded {};
    Atomic<u32> num_samples_loaded {};

    // internal
    AvailableLibraries& available_libraries;
    ThreadPool& thread_pool;
    Atomic<RequestId> request_id_counter {};
    MutexProtected<List<Connection>> connections {Malloc::Instance()};
    Thread thread {};
    Atomic<bool> end_thread {false};
    ThreadsafeQueue<QueuedRequest> request_queue {PageAllocator::Instance()};
    WorkSignaller work_signaller {};
    Atomic<bool> debug_dump_current_state {false};
};

inline void ReleaseAll(Span<RefCounted<sample_lib::Library>> libs) {
    for (auto& l : libs)
        l.Release();
}

// The loading thread owns the connection, you just get a reference to it that will be valid until you call
// CloseConnection. The callback will be called whenever a request from this connection is completed. If you
// want to keep any of the assets that are contained in the LoadResult, you must 'retain' them in the
// callback. You can release them at any point after that. The callback is called from the asset thread; you
// should not do any really slow operations in it because it will block the asset thread from processing other
// requests.
Connection& OpenConnection(LoadingThread& thread,
                           ThreadsafeErrorNotifications& error_notifications,
                           LoadCompletedCallback&& completed_callback);

void CloseConnection(LoadingThread& thread, Connection& connection);

// If you send another request while one is already pending with the same connection, the instrument loading
// of the previous request, with the same layer_index will be aborted if needed.
RequestId SendLoadRequest(LoadingThread& thread, Connection& connection, LoadRequest const& request);

} // namespace sample_lib_loader
