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
#include "sample_library/loaded_instrument.hpp"
#include "sample_library/sample_library.hpp"

// Requirements:
// 1. Asynchronous
// 2. Fast; especially for already-loaded assets
// 3. In-progress loads that are no longer needed should be aborted
// 4. The main-thread should be able to retrieve the loading percentage for instruments
// 5. Each asset should not be duplicated in memory
// 6. Unused assets should be freed

namespace sample_lib_server {

// forward declarations
namespace detail {
struct ListedInstrument;
}

using RequestId = u64;

enum class LoadRequestType { Instrument, Ir };

struct LoadRequestInstrumentIdWithLayer {
    sample_lib::InstrumentId id;
    u32 layer_index;
};

using LoadRequest = TaggedUnion<LoadRequestType,
                                TypeAndTag<LoadRequestInstrumentIdWithLayer, LoadRequestType::Instrument>,
                                TypeAndTag<sample_lib::IrId, LoadRequestType::Ir>>;

enum class RefCountChange { Retain, Release };

template <typename Type>
struct RefCounted {
    RefCounted() = default;
    RefCounted(Type& t, Atomic<u32>& r, WorkSignaller* s) : m_data(&t), m_refs(&r), m_work_signaller(s) {}

    void Retain() const {
        if (m_refs) m_refs->FetchAdd(1, MemoryOrder::Relaxed);
    }
    void Release() const {
        if (m_refs) {
            auto prev = m_refs->SubFetch(1, MemoryOrder::AcquireRelease);
            ASSERT(prev != ~(u32)0);
            if (prev == 0 && m_work_signaller) m_work_signaller->Signal();
        }
    }
    void Assign(RefCounted const& other) {
        Release();
        other.Retain();
        m_data = other.m_data;
        m_refs = other.m_refs;
        m_work_signaller = other.m_work_signaller;
    }
    void ChangeRefCount(RefCountChange t) const {
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

using RefUnion = TaggedUnion<LoadRequestType,
                             TypeAndTag<RefCounted<LoadedInstrument>, LoadRequestType::Instrument>,
                             TypeAndTag<RefCounted<AudioData>, LoadRequestType::Ir>>;

struct LoadResult {
    enum class ResultType { Success, Error, Cancelled };
    using Result = TaggedUnion<ResultType,
                               TypeAndTag<RefUnion, ResultType::Success>,
                               TypeAndTag<ErrorCode, ResultType::Error>>;

    void ChangeRefCount(RefCountChange t) const;
    void Retain() const { ChangeRefCount(RefCountChange::Retain); }
    void Release() const { ChangeRefCount(RefCountChange::Release); }

    RequestId id;
    Result result;
};

using LoadCompletedCallback = TrivialFixedSizeFunction<40, void(LoadResult)>;

struct AsyncCommsChannel {
    // -1 if not valid, else 0 to 100
    Array<Atomic<s32>, k_num_layers> instrument_loading_percents {};

    // private
    ThreadsafeErrorNotifications& error_notifications;
    Array<detail::ListedInstrument*, k_num_layers> desired_inst {};
    LoadCompletedCallback completed_callback;
    Atomic<bool> used {};
};

namespace detail {

extern u32 g_inst_debug_id;

enum class LoadingState : u32 {
    PendingLoad,
    PendingCancel,
    Loading,
    CompletedSucessfully,
    CompletedWithError,
    CompletedCancelled,
    Count,
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

    ArenaList<ListedInstrument, false> instruments {arena};
};

using LibrariesList = AtomicRefList<ListedLibrary>;

struct ScanFolder {
    enum class Source { AlwaysScannedFolder, ExtraFolder };
    enum class State { NotScanned, RescanRequested, Scanning, ScannedSuccessfully, ScanFailed };
    DynamicArray<char> path {Malloc::Instance()};
    Source source {};
    Atomic<State> state {State::NotScanned};
};

using ScanFolderList = AtomicRefList<ScanFolder>;

struct QueuedRequest {
    RequestId id;
    LoadRequest request;
    AsyncCommsChannel& async_comms_channel;
};

} // namespace detail

struct Server {
    Server(ThreadPool& pool,
           Span<String const> always_scanned_folders,
           ThreadsafeErrorNotifications& connection_independent_error_notif);
    ~Server();

    Atomic<u64> total_bytes_used_by_samples {};
    Atomic<u32> num_insts_loaded {};
    Atomic<u32> num_samples_loaded {};

    // internal
    Mutex scan_folders_writer_mutex;
    detail::ScanFolderList scan_folders;
    detail::LibrariesList libraries;
    Mutex libraries_by_name_mutex;
    DynamicHashTable<String, detail::LibrariesList::Node*> libraries_by_name {Malloc::Instance()};

    // Connection independent. If you have access to a channel, you can use their error_notifications
    // instead.
    ThreadsafeErrorNotifications& error_notifications;

    ThreadPool& thread_pool;
    Atomic<RequestId> request_id_counter {};
    MutexProtected<ArenaList<AsyncCommsChannel, true>> channels {Malloc::Instance()};
    Thread loading_thread {};
    Atomic<bool> end_thread {false};
    ThreadsafeQueue<detail::QueuedRequest> request_queue {PageAllocator::Instance()};
    WorkSignaller work_signaller {};
    Atomic<bool> request_debug_dump_current_state {false};
};

// The server owns the channel, you just get a reference to it that will be valid until you close it. The
// callback will be called whenever a request from this channel is completed. If you want to keep any of
// the assets that are contained in the LoadResult, you must 'retain' them in the callback. You can release
// them at any point after that. The callback is called from the server thread; you should not do any really
// slow operations in it because it will block the server thread from processing other requests.
AsyncCommsChannel& OpenAsyncCommsChannel(Server& server,
                                         ThreadsafeErrorNotifications& error_notifications,
                                         LoadCompletedCallback&& completed_callback);

void CloseAsyncCommsChannel(Server& server, AsyncCommsChannel& channel);

// If you send a request when there's already another loading with the same layer_index and channel, the
// old will be aborted, provided it's not needed by anything else.
// threadsafe
RequestId SendAsyncLoadRequest(Server& server, AsyncCommsChannel& channel, LoadRequest const& request);

// threadsafe
void SetExtraScanFolders(Server& server, Span<String const> folders);

// main-thread, you must call Release on all results
Span<RefCounted<sample_lib::Library>> AllLibrariesRetained(Server& server, ArenaAllocator& arena);
RefCounted<sample_lib::Library> FindLibraryRetained(Server& server, String name);

inline void ReleaseAll(Span<RefCounted<sample_lib::Library>> libs) {
    for (auto& l : libs)
        l.Release();
}

} // namespace sample_lib_server
