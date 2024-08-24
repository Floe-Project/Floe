// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pthread.h>
#include <unistd.h>

#include "foundation/foundation.hpp"

#include "threading.hpp"

void SleepThisThread(int milliseconds) { usleep((unsigned)milliseconds * 1000); }
void YieldThisThread() { sched_yield(); }

u64 CurrentThreadId() { return (u64)(uintptr)pthread_self(); }

void SetCurrentThreadPriorityRealTime() {
    struct sched_param params = {};
    params.sched_priority = sched_get_priority_max(SCHED_RR);
    // IMPROVE: warn about the error somehow
    auto ret = pthread_setschedparam(pthread_self(), SCHED_RR, &params);
    (void)ret;
}

Mutex::Mutex() { pthread_mutex_init(&mutex.As<pthread_mutex_t>(), nullptr); }
Mutex::~Mutex() { pthread_mutex_destroy(&mutex.As<pthread_mutex_t>()); }
void Mutex::Lock() { pthread_mutex_lock(&mutex.As<pthread_mutex_t>()); }
bool Mutex::TryLock() { return pthread_mutex_trylock(&mutex.As<pthread_mutex_t>()) == 0; }
void Mutex::Unlock() { pthread_mutex_unlock(&mutex.As<pthread_mutex_t>()); }

Thread::Thread() {}
Thread::~Thread() { ASSERT(!Joinable()); }

Thread::Thread(Thread&& other) : m_thread(other.m_thread), m_active(other.m_active) {}
Thread& Thread::operator=(Thread&& other) {
    ASSERT(!Joinable());
    m_thread = other.m_thread;
    m_active = Exchange(other.m_active, false);
    return *this;
}

thread_local DynamicArrayInline<char, k_max_thread_name_size> g_thread_name {};

void SetThreadName(String name) {
    char buffer[k_max_thread_name_size];
    CopyStringIntoBufferWithNullTerm(buffer, name);
#if __APPLE__
    pthread_setname_np(buffer);
#else
    dyn::Assign(g_thread_name, name);
    // On Linux, even though we don't use pthread_getname_np to fetch the name, let's still set it because it
    // might help out external tools.
    pthread_setname_np(pthread_self(), buffer);
#endif
}

Optional<DynamicArrayInline<char, k_max_thread_name_size>> ThreadName() {
    if constexpr (IS_LINUX) {
        if (!g_thread_name.size) return nullopt;
        // On Linux, pthread_getname_np will return the name of the executable which is confusing, better to
        // have nothing and fetch the TID.
        return g_thread_name.Items();
    } else {
        char buffer[k_max_thread_name_size];
        if (pthread_getname_np(pthread_self(), buffer, k_max_thread_name_size) == 0)
            return FromNullTerminated(buffer);
        return nullopt;
    }
}

static void* ThreadStartProc(void* data) {
    auto d = (Thread::ThreadStartData*)data;
    if (d->thread_name[0] && !PRODUCTION_BUILD) SetThreadName(d->thread_name);
    d->StartThread();
    delete d;
    return nullptr;
}

void Thread::Start(StartFunction&& function, String name, ThreadStartOptions options) {
    auto thread_start_data = new ThreadStartData(Move(function), name, options);

    pthread_attr_t* attr_ptr = nullptr;
    pthread_attr_t attr;
    if (options.stack_size) {
        if (pthread_attr_init(&attr) == 0) {
            attr_ptr = &attr;
            if (pthread_attr_setstacksize(&attr, *options.stack_size) != 0) attr_ptr = nullptr;
        }
    }
    DEFER {
        if (attr_ptr) pthread_attr_destroy(attr_ptr);
    };

    int const result =
        pthread_create(&m_thread.As<pthread_t>(), attr_ptr, ThreadStartProc, thread_start_data);
    if (result != 0) Panic("Failed to create a thread");

    m_active = true;
}

void Thread::Detach() {
    ASSERT(m_active);
    auto result = pthread_detach(m_thread.As<pthread_t>());
    if (result != 0) Panic("Failed to detach a thread");
    m_active = false;
}

void Thread::Join() {
    ASSERT(m_active);
    auto result = pthread_join(m_thread.As<pthread_t>(), nullptr);
    if (result != 0) Panic("Failed to join a thread");
    m_active = false;
}

bool Thread::Joinable() const { return m_active; }

ConditionVariable::ConditionVariable() {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_cond_init(&m_cond_var.As<pthread_cond_t>(), &attr);
}

ConditionVariable::~ConditionVariable() { pthread_cond_destroy(&m_cond_var.As<pthread_cond_t>()); }

void ConditionVariable::Wait(ScopedMutexLock& lock) {
    pthread_cond_wait(&m_cond_var.As<pthread_cond_t>(), &lock.mutex.mutex.As<pthread_mutex_t>());
}

void ConditionVariable::TimedWait(ScopedMutexLock& lock, u64 wait_ms) {
    if (wait_ms > 0) {
#if __APPLE__
        struct timespec ts;
        ts.tv_sec += wait_ms / 1000;
        ts.tv_nsec += (wait_ms % 1000) * 1000000;
        pthread_cond_timedwait_relative_np(&m_cond_var.As<pthread_cond_t>(),
                                           &lock.mutex.mutex.As<pthread_mutex_t>(),
                                           &ts);
#else
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += wait_ms / 1000;
        ts.tv_nsec += (wait_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec++;
        }
        pthread_cond_timedwait(&m_cond_var.As<pthread_cond_t>(),
                               &lock.mutex.mutex.As<pthread_mutex_t>(),
                               &ts);
#endif
    }
}

void ConditionVariable::WakeOne() { pthread_cond_signal(&m_cond_var.As<pthread_cond_t>()); }

void ConditionVariable::WakeAll() { pthread_cond_broadcast(&m_cond_var.As<pthread_cond_t>()); }
