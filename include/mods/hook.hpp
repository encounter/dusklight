#pragma once

#include "mods/svc/hook.h"

#include <cstring>
#include <memory>
#include <type_traits>

namespace dusk::mods {

template <class T>
T arg(void* argsRaw, int n) noexcept {
    void** args = static_cast<void**>(argsRaw);
    return *static_cast<std::add_pointer_t<std::remove_reference_t<T>>>(args[n]);
}

template <class T>
std::remove_reference_t<T>& arg_ref(void* argsRaw, int n) noexcept {
    void** args = static_cast<void**>(argsRaw);
    return *static_cast<std::add_pointer_t<std::remove_reference_t<T>>>(args[n]);
}

template <class F>
void* mfp_addr(F fn) noexcept {
    void* p = nullptr;
    static_assert(sizeof(fn) >= sizeof(void*), "unexpected function pointer size");
    std::memcpy(&p, &fn, sizeof(void*));
    return p;
}

template <auto Target, class R, class Self, class Orig, class... A>
struct HookEntryBase {
    static inline Orig g_orig = nullptr;
    static inline ServiceRef<HookService> hooks{};

    static bool dispatch_pre(void* args, void* retval) {
        if (!hooks) {
            return false;
        }

        int skipOriginal = 0;
        const ModResult result = SERVICE_CALL(
            hooks, dispatch_pre, mfp_addr(Target), args, retval, &skipOriginal);
        return result == MOD_OK && skipOriginal != 0;
    }

    static void dispatch_post(void* args, void* retval) {
        if (hooks) {
            SERVICE_CALL(hooks, dispatch_post, mfp_addr(Target), args, retval);
        }
    }

    static R trampoline(Self self, A... args) {
        void* ptrs[] = {
            static_cast<void*>(std::addressof(self)),
            static_cast<void*>(std::addressof(args))...,
        };

        if constexpr (std::is_void_v<R>) {
            const bool skipOriginal = dispatch_pre(static_cast<void*>(ptrs), nullptr);
            if (!skipOriginal) {
                g_orig(self, args...);
            }
            dispatch_post(static_cast<void*>(ptrs), nullptr);
        } else {
            R result{};
            const bool skipOriginal =
                dispatch_pre(static_cast<void*>(ptrs), static_cast<void*>(std::addressof(result)));
            if (!skipOriginal) {
                result = g_orig(self, args...);
            }
            dispatch_post(static_cast<void*>(ptrs), static_cast<void*>(std::addressof(result)));
            return result;
        }
    }
};

template <auto Target, class R, class Orig, class... A>
struct HookEntryFreeBase {
    static inline Orig g_orig = nullptr;
    static inline ServiceRef<HookService> hooks{};

    static bool dispatch_pre(void* args, void* retval) {
        if (!hooks) {
            return false;
        }

        int skipOriginal = 0;
        const ModResult result = SERVICE_CALL(
            hooks, dispatch_pre, mfp_addr(Target), args, retval, &skipOriginal);
        return result == MOD_OK && skipOriginal != 0;
    }

    static void dispatch_post(void* args, void* retval) {
        if (hooks) {
            SERVICE_CALL(hooks, dispatch_post, mfp_addr(Target), args, retval);
        }
    }

    static R trampoline(A... args) {
        if constexpr (sizeof...(A) == 0) {
            if constexpr (std::is_void_v<R>) {
                const bool skipOriginal = dispatch_pre(nullptr, nullptr);
                if (!skipOriginal) {
                    g_orig(args...);
                }
                dispatch_post(nullptr, nullptr);
            } else {
                R result{};
                const bool skipOriginal =
                    dispatch_pre(nullptr, static_cast<void*>(std::addressof(result)));
                if (!skipOriginal) {
                    result = g_orig(args...);
                }
                dispatch_post(nullptr, static_cast<void*>(std::addressof(result)));
                return result;
            }
        } else {
            void* ptrs[] = {static_cast<void*>(std::addressof(args))...};
            if constexpr (std::is_void_v<R>) {
                const bool skipOriginal = dispatch_pre(static_cast<void*>(ptrs), nullptr);
                if (!skipOriginal) {
                    g_orig(args...);
                }
                dispatch_post(static_cast<void*>(ptrs), nullptr);
            } else {
                R result{};
                const bool skipOriginal = dispatch_pre(
                    static_cast<void*>(ptrs), static_cast<void*>(std::addressof(result)));
                if (!skipOriginal) {
                    result = g_orig(args...);
                }
                dispatch_post(static_cast<void*>(ptrs), static_cast<void*>(std::addressof(result)));
                return result;
            }
        }
    }
};

template <auto Target>
struct HookEntry;

template <class C, class R, class... A, R (C::*Target)(A...)>
struct HookEntry<Target> : HookEntryBase<Target, R, C*, R (*)(C*, A...), A...> {};

template <class C, class R, class... A, R (C::*Target)(A...) const>
struct HookEntry<Target> : HookEntryBase<Target, R, const C*, R (*)(const C*, A...), A...> {};

template <class R, class... A, R (*Target)(A...)>
struct HookEntry<Target> : HookEntryFreeBase<Target, R, R (*)(A...), A...> {};

template <auto Target>
ModResult hook_install(const ServiceRef<HookService>& hooks) {
    if (!hooks) {
        return MOD_UNAVAILABLE;
    }

    using Entry = HookEntry<Target>;
    Entry::hooks = hooks;
    return SERVICE_CALL(hooks, install, mfp_addr(Target), reinterpret_cast<void*>(Entry::trampoline),
        reinterpret_cast<void**>(&Entry::g_orig));
}

template <auto Target>
ModResult hook_add_pre(
    const ServiceRef<HookService>& hooks, HookPreFn callback, const HookOptions* options = nullptr)
{
    const ModResult installed = hook_install<Target>(hooks);
    if (installed != MOD_OK) {
        return installed;
    }

    return SERVICE_CALL(hooks, add_pre, mfp_addr(Target), callback, options);
}

template <auto Target>
ModResult hook_add_post(
    const ServiceRef<HookService>& hooks, HookPostFn callback, const HookOptions* options = nullptr)
{
    const ModResult installed = hook_install<Target>(hooks);
    if (installed != MOD_OK) {
        return installed;
    }

    return SERVICE_CALL(hooks, add_post, mfp_addr(Target), callback, options);
}

template <auto Target>
ModResult hook_set_replace(const ServiceRef<HookService>& hooks, HookReplaceFn callback,
    const HookOptions* options = nullptr)
{
    const ModResult installed = hook_install<Target>(hooks);
    if (installed != MOD_OK) {
        return installed;
    }

    return SERVICE_CALL(hooks, set_replace, mfp_addr(Target), callback, options);
}

}  // namespace dusk::mods
