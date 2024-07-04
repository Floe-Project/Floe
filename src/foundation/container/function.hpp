// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/memory/allocators.hpp"
#include "foundation/universal_defs.hpp"

template <typename T>
struct FunctionRef;

template <typename ReturnType, class... Args>
struct FunctionRef<ReturnType(Args...)> {
    constexpr FunctionRef() = default;
    constexpr FunctionRef(decltype(nullptr)) {}

    template <FunctionPointer F>
    requires(FunctionWithSignature<F, ReturnType, Args...>)
    constexpr FunctionRef(F& f) {
        Set(&f);
    }

    template <typename F>
    requires(!Same<FunctionRef, RemoveReference<F>> && FunctionWithSignature<F, ReturnType, Args...>)
    constexpr FunctionRef(F&& func) {
        Set(&func);
    }

    template <typename F>
    constexpr void Set(F* f) {
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(F*)d)(args...); };
        function_object = (void*)f;
    }

    constexpr ReturnType operator()(Args... args) const {
        return invoke_function(function_object, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    void* function_object = nullptr;
};

template <typename T>
struct TrivialFunctionRef;

template <typename ReturnType, class... Args>
struct TrivialFunctionRef<ReturnType(Args...)> {
    constexpr TrivialFunctionRef() = default;
    constexpr TrivialFunctionRef(decltype(nullptr)) {}

    template <FunctionPointer F>
    requires(FunctionWithSignature<F, ReturnType, Args...>)
    constexpr TrivialFunctionRef(F& f) {
        Set(&f);
        function_object_size = sizeof(F);
    }

    template <typename F>
    requires(!Same<TrivialFunctionRef, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr TrivialFunctionRef(F const& func) {
        Set(&func);
        function_object_size = sizeof(F);
    }

    template <typename F>
    constexpr void Set(F* f) {
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(F*)d)(args...); };
        function_object = (void*)f;
    }

    TrivialFunctionRef CloneObject(ArenaAllocator& a) {
        auto allocation = a.Allocate(
            {.size = function_object_size, .alignment = k_max_alignment, .allow_oversized_result = false});
        __builtin_memcpy(allocation.data, function_object, function_object_size);
        TrivialFunctionRef result;
        result.invoke_function = invoke_function;
        result.function_object = (void*)allocation.data;
        result.function_object_size = function_object_size;
        return result;
    }

    constexpr ReturnType operator()(Args... args) const {
        return invoke_function(function_object, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    void* function_object = nullptr;
    usize function_object_size = 0;
};

template <usize, class T>
struct TrivialFixedSizeFunction;

template <usize k_capacity, class ReturnType, class... Args>
struct TrivialFixedSizeFunction<k_capacity, ReturnType(Args...)> {
    constexpr TrivialFixedSizeFunction() = default;

    constexpr TrivialFixedSizeFunction(decltype(nullptr)) {}

    template <typename F>
    requires(FunctionPointer<Decay<F>>)
    constexpr TrivialFixedSizeFunction(F* f) {
        Decay<F> function_pointer = f;
        Set(function_pointer);
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr TrivialFixedSizeFunction(F const& func) {
        Set(func);
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr TrivialFixedSizeFunction& operator=(F const& func) {
        Set(func);
        return *this;
    }

    template <typename F>
    requires(!Same<TrivialFixedSizeFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<F>)
    constexpr void Set(F const& func) {
        static_assert(sizeof(F) <= k_capacity);
        __builtin_memcpy_inline(function_object_storage, &func, sizeof(F));
        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(F*)d)(args...); };
    }

    constexpr ReturnType operator()(Args... args) const {
        ASSERT(invoke_function);
        return invoke_function((void*)function_object_storage, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    alignas(k_max_alignment) u8 function_object_storage[Max(k_capacity, sizeof(void*))];
};

template <typename T>
struct TrivialAllocatedFunction;

template <typename ReturnType, class... Args>
struct TrivialAllocatedFunction<ReturnType(Args...)> {
    constexpr TrivialAllocatedFunction(Allocator& a) : allocator(a) {}

    constexpr ~TrivialAllocatedFunction() { allocator.Free(function_object_storage); }

    constexpr TrivialAllocatedFunction(TrivialAllocatedFunction const& other) : allocator(other.allocator) {
        invoke_function = other.invoke_function;
        function_object_storage = allocator.Clone(other.function_object_storage, CloneType::Deep);
    }

    constexpr TrivialAllocatedFunction& operator=(TrivialAllocatedFunction const& other) {
        invoke_function = other.invoke_function;
        function_object_storage =
            allocator.Reallocate<u8>(other.function_object_storage.size, function_object_storage, 0, true);
        __builtin_memcpy(function_object_storage.data,
                         other.function_object_storage.data,
                         other.function_object_storage.size);
        return *this;
    }

    constexpr TrivialAllocatedFunction(TrivialFunctionRef<ReturnType(Args...)> const& ref, Allocator& a)
        : allocator(a) {
        if (ref.function_object_size) {
            function_object_storage = allocator.Allocate({.size = ref.function_object_size,
                                                          .alignment = k_max_alignment,
                                                          .allow_oversized_result = true});
            __builtin_memcpy(function_object_storage.data, ref.function_object, ref.function_object_size);
        }
        invoke_function = ref.invoke_function;
    }

    template <typename F>
    requires(FunctionPointer<Decay<F>>)
    constexpr TrivialAllocatedFunction(F* f, Allocator& a) : allocator(a) {
        Decay<F> function_pointer = f;
        Set(function_pointer);
    }

    template <typename F>
    requires(!Same<TrivialAllocatedFunction, RemoveReference<F>> &&
             !Same<TrivialFunctionRef<ReturnType(Args...)>, RemoveReference<F>> &&
             FunctionWithSignature<F, ReturnType, Args...> && TriviallyCopyable<RemoveReference<F>>)
    constexpr TrivialAllocatedFunction(F&& func, Allocator& a) : allocator(a) {
        Set(Forward<F>(func));
    }

    template <typename F>
    requires(!Same<TrivialAllocatedFunction, RemoveReference<F>> &&
             !Same<TrivialFunctionRef<ReturnType(Args...)>, RemoveReference<F>> &&
             FunctionWithSignature<F, ReturnType, Args...> && TriviallyCopyable<RemoveReference<F>>)
    constexpr TrivialAllocatedFunction& operator=(F&& func) {
        Set(Forward<F>(func));
        return *this;
    }

    template <typename F>
    requires(!Same<TrivialAllocatedFunction, F> && FunctionWithSignature<F, ReturnType, Args...> &&
             TriviallyCopyable<RemoveReference<F>>)
    constexpr void Set(F&& func) {
        using FUnref = RemoveReference<F>;

        if (sizeof(FUnref) > function_object_storage.size)
            function_object_storage = allocator.Reallocate<FUnref>(1, function_object_storage, 0, true);

        __builtin_memcpy_inline(function_object_storage.data, &func, sizeof(FUnref));

        invoke_function = [](void* d, Args... args) -> ReturnType { return (*(FUnref*)d)(args...); };
    }

    constexpr ReturnType operator()(Args... args) const {
        ASSERT(invoke_function);
        return invoke_function((void*)function_object_storage.data, Forward<Args>(args)...);
    }

    constexpr operator bool() const { return invoke_function != nullptr; }

    ReturnType (*invoke_function)(void*, Args...) = nullptr;
    Allocator& allocator;
    Span<u8> function_object_storage {};
};
