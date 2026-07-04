#include "dusk/hook_system.hpp"

#if DUSK_CODE_MODS

#include "dusk/logging.h"
#include "dusk/mod_loader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <funchook.h>
#include <unordered_map>
#include <vector>

namespace dusk {

namespace {

struct PreHookFn {
    ModContext* context = nullptr;
    HookPreFn callback = nullptr;
    HookOptions options = HOOK_OPTIONS_INIT;
    uint64_t order = 0;
};

struct VoidHookFn {
    ModContext* context = nullptr;
    HookReplaceFn replaceCallback = nullptr;
    HookPostFn postCallback = nullptr;
    HookOptions options = HOOK_OPTIONS_INIT;
    uint64_t order = 0;
};

struct HookSlot {
    std::vector<PreHookFn> pre;
    VoidHookFn replace{};
    std::vector<VoidHookFn> post;
};

// One per mod that requested a hook on a target: its template-generated trampoline and the
// address of its HookEntry::g_orig, both living in the mod's dylib. Any candidate's trampoline
// is interchangeable (dispatch walks the shared HookSlot), so when the active installer's mod
// unloads, the funchook detour is handed off to a surviving candidate and every candidate's
// *orig_store is rewritten to the new original pointer.
struct HookCandidate {
    ModContext* context = nullptr;
    void* trampoline = nullptr;
    void** orig_store = nullptr;
    uint64_t order = 0;
};

struct InstalledHook {
    funchook_t* handle = nullptr;
    void* original = nullptr;
    ModContext* active = nullptr;
    std::vector<HookCandidate> candidates;
};

std::unordered_map<uintptr_t, HookSlot> s_registry;
std::unordered_map<uintptr_t, InstalledHook> s_installed;
uint64_t s_nextOrder = 0;

const char* context_mod_id(const ModContext* context) {
    return context != nullptr && context->mod != nullptr ? context->mod->metadata.id.c_str() :
                                                           "mod";
}

HookOptions normalize_options(const HookOptions* options) {
    HookOptions normalized = HOOK_OPTIONS_INIT;
    if (options != nullptr) {
        normalized = *options;
    }
    if (normalized.struct_size < sizeof(HookOptions)) {
        normalized = HOOK_OPTIONS_INIT;
    }
    return normalized;
}

template <class T>
void sort_hooks(std::vector<T>& hooks) {
    std::ranges::stable_sort(hooks, [](const T& a, const T& b) {
        if (a.options.priority != b.options.priority) {
            return a.options.priority > b.options.priority;
        }
        return a.order < b.order;
    });
}

// Follow E9/FF25 chains to skip MSVC incremental-link and import stubs.
void* resolve_import_thunk(void* addr) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    for (int i = 0; i < 8; ++i) {
        const auto* p = static_cast<const uint8_t*>(addr);
        if (p[0] == 0xFF && p[1] == 0x25) {
            int32_t offset;
            std::memcpy(&offset, p + 2, 4);
            addr = const_cast<void*>(*reinterpret_cast<const void* const*>(p + 6 + offset));
            break;
        }
        if (p[0] == 0xE9) {
            int32_t offset;
            std::memcpy(&offset, p + 1, 4);
            addr = const_cast<uint8_t*>(p) + 5 + offset;
        } else {
            break;
        }
    }
#endif
    return addr;
}

funchook_t* install_trampoline(void* fn_addr, void* trampoline, void** out_original) {
    funchook_t* fh = funchook_create();
    if (fh == nullptr) {
        DuskLog.warn("HookSystem: funchook_create failed for {:p}", fn_addr);
        return nullptr;
    }

    void* fn = fn_addr;
    int prep = funchook_prepare(fh, &fn, trampoline);
    int inst = (prep == 0) ? funchook_install(fh, 0) : -1;
    if (prep != 0 || inst != 0) {
        const char* message = funchook_error_message(fh);
        DuskLog.warn("HookSystem: funchook failed for {:p} (prepare={} install={}): {}", fn_addr,
            prep, inst, message != nullptr && message[0] != '\0' ? message : "no details");
        funchook_destroy(fh);
        return nullptr;
    }

    *out_original = fn;
    return fh;
}

}  // namespace

// Hook installation, removal and dispatch all assume the game thread; hookRemoveMod must only
// run when no hooked function is on the stack (the mod loader applies lifecycle changes at the
// top of its tick).
ModResult hookInstallByAddr(ModContext* context, void* fn_addr, void* tramp_fn, void** orig_store) {
    if (fn_addr == nullptr || tramp_fn == nullptr || orig_store == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    fn_addr = resolve_import_thunk(fn_addr);
    auto key = reinterpret_cast<uintptr_t>(fn_addr);
    auto it = s_installed.find(key);
    if (it != s_installed.end()) {
        auto& entry = it->second;
        // hook_add_pre + hook_add_post on the same target share one g_orig per mod.
        const bool known = std::ranges::any_of(entry.candidates, [&](const HookCandidate& cand) {
            return cand.context == context && cand.orig_store == orig_store;
        });
        if (!known) {
            entry.candidates.push_back(HookCandidate{context, tramp_fn, orig_store, s_nextOrder++});
        }
        DuskLog.debug("HookSystem: candidate {} for {:p}: tramp={:p} orig_store={:p} (known={})",
            context_mod_id(context), fn_addr, tramp_fn, static_cast<void*>(orig_store), known);
        *orig_store = entry.original;
        return MOD_OK;
    }

    void* original = nullptr;
    funchook_t* fh = install_trampoline(fn_addr, tramp_fn, &original);
    if (fh == nullptr) {
        return MOD_ERROR;
    }

    auto& entry = s_installed[key];
    entry.handle = fh;
    entry.original = original;
    entry.active = context;
    entry.candidates.push_back(HookCandidate{context, tramp_fn, orig_store, s_nextOrder++});
    *orig_store = original;
    return MOD_OK;
}

ModResult hookRegisterPre(
    void* fn_addr, ModContext* context, HookPreFn callback, const HookOptions* options) {
    if (fn_addr == nullptr || context == nullptr || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    fn_addr = resolve_import_thunk(fn_addr);
    auto& hooks = s_registry[reinterpret_cast<uintptr_t>(fn_addr)].pre;
    hooks.push_back(PreHookFn{context, callback, normalize_options(options), s_nextOrder++});
    sort_hooks(hooks);
    return MOD_OK;
}

ModResult hookRegisterPost(
    void* fn_addr, ModContext* context, HookPostFn callback, const HookOptions* options) {
    if (fn_addr == nullptr || context == nullptr || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    fn_addr = resolve_import_thunk(fn_addr);
    auto& hooks = s_registry[reinterpret_cast<uintptr_t>(fn_addr)].post;
    HookOptions normalized = normalize_options(options);
    hooks.push_back(VoidHookFn{context, nullptr, callback, normalized, s_nextOrder++});
    sort_hooks(hooks);
    return MOD_OK;
}

ModResult hookSetReplace(
    void* fn_addr, ModContext* context, HookReplaceFn callback, const HookOptions* options) {
    if (fn_addr == nullptr || context == nullptr || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    HookOptions normalized = normalize_options(options);
    fn_addr = resolve_import_thunk(fn_addr);
    auto& slot = s_registry[reinterpret_cast<uintptr_t>(fn_addr)];
    if (slot.replace.replaceCallback == nullptr) {
        slot.replace = VoidHookFn{context, callback, nullptr, normalized, s_nextOrder++};
        return MOD_OK;
    }

    switch (normalized.replace_policy) {
    case HOOK_REPLACE_CONFLICT:
        DuskLog.error("HookSystem: '{}' conflicts with '{}', both replace the same function",
            context_mod_id(context), context_mod_id(slot.replace.context));
        return MOD_CONFLICT;
    case HOOK_REPLACE_PRIORITY:
        if (normalized.priority <= slot.replace.options.priority) {
            return MOD_CONFLICT;
        }
        slot.replace = VoidHookFn{context, callback, nullptr, normalized, s_nextOrder++};
        return MOD_OK;
    case HOOK_REPLACE_OVERRIDE:
        slot.replace = VoidHookFn{context, callback, nullptr, normalized, s_nextOrder++};
        return MOD_OK;
    }

    return MOD_INVALID_ARGUMENT;
}

ModResult hookDispatchPre(
    ModContext*, void* fn_addr, void* args, void* retval, int* out_skip_original) {
    if (out_skip_original != nullptr) {
        *out_skip_original = 0;
    }
    if (fn_addr == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    fn_addr = resolve_import_thunk(fn_addr);
    auto it = s_registry.find(reinterpret_cast<uintptr_t>(fn_addr));
    if (it == s_registry.end()) {
        return MOD_OK;
    }
    auto& slot = it->second;
    for (auto& hook : slot.pre) {
        if (hook.callback != nullptr &&
            hook.callback(hook.context, args, retval, hook.options.userdata) == HOOK_SKIP_ORIGINAL)
        {
            if (out_skip_original != nullptr) {
                *out_skip_original = 1;
            }
            return MOD_OK;
        }
    }
    if (slot.replace.replaceCallback != nullptr) {
        slot.replace.replaceCallback(
            slot.replace.context, args, retval, slot.replace.options.userdata);
        if (out_skip_original != nullptr) {
            *out_skip_original = 1;
        }
    }
    return MOD_OK;
}

ModResult hookDispatchPost(ModContext*, void* fn_addr, void* args, void* retval) {
    if (fn_addr == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    fn_addr = resolve_import_thunk(fn_addr);
    auto it = s_registry.find(reinterpret_cast<uintptr_t>(fn_addr));
    if (it == s_registry.end()) {
        return MOD_OK;
    }
    for (auto& hook : it->second.post) {
        if (hook.postCallback != nullptr) {
            hook.postCallback(hook.context, args, retval, hook.options.userdata);
        }
    }
    return MOD_OK;
}

void hookRemoveMod(ModContext* context) {
    for (auto it = s_registry.begin(); it != s_registry.end();) {
        auto& slot = it->second;
        auto erase = [&](auto& hooks) {
            hooks.erase(std::remove_if(hooks.begin(), hooks.end(),
                            [context](const auto& hook) { return hook.context == context; }),
                hooks.end());
        };
        erase(slot.pre);
        erase(slot.post);
        if (slot.replace.context == context) {
            slot.replace = {};
        }
        if (slot.pre.empty() && slot.post.empty() && slot.replace.replaceCallback == nullptr) {
            it = s_registry.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = s_installed.begin(); it != s_installed.end();) {
        auto& entry = it->second;
        // The departing mod's g_orig slots are about to be unmapped; drop its candidates before
        // any orig_store rewrites below.
        std::erase_if(entry.candidates,
            [context](const HookCandidate& cand) { return cand.context == context; });
        if (entry.active != context) {
            ++it;
            continue;
        }

        auto* target = reinterpret_cast<void*>(it->first);
        const int uninst = funchook_uninstall(entry.handle, 0);
        const int destr = funchook_destroy(entry.handle);
        if (uninst != 0 || destr != 0) {
            DuskLog.warn("HookSystem: funchook uninstall/destroy for {:p} returned {}/{}", target,
                uninst, destr);
        }
        entry.handle = nullptr;
        entry.active = nullptr;

        if (entry.candidates.empty()) {
            it = s_installed.erase(it);
            continue;
        }

        // Hand the detour off to a surviving candidate (lowest registration order first; the
        // vector is append-ordered). A candidate whose install fails stays in the list — its
        // g_orig must still track the current original pointer.
        for (auto& cand : entry.candidates) {
            void* original = nullptr;
            funchook_t* fh = install_trampoline(target, cand.trampoline, &original);
            if (fh == nullptr) {
                continue;
            }
            entry.handle = fh;
            entry.original = original;
            entry.active = cand.context;
            DuskLog.info("HookSystem: reinstalled trampoline for {:p}: {} -> {} (tramp={:p})",
                target, context_mod_id(context), context_mod_id(cand.context), cand.trampoline);
            break;
        }

        if (entry.active == nullptr) {
            DuskLog.warn("HookSystem: no reinstallable trampoline for {:p}; hooks there are "
                         "disabled until a mod reinstalls one",
                target);
            for (auto& cand : entry.candidates) {
                *cand.orig_store = target;
            }
            it = s_installed.erase(it);
            continue;
        }

        for (auto& cand : entry.candidates) {
            *cand.orig_store = entry.original;
        }
        ++it;
    }
}

}  // namespace dusk

#else

namespace dusk {

ModResult hookInstallByAddr(ModContext*, void*, void*, void**) {
    return MOD_UNSUPPORTED;
}

ModResult hookRegisterPre(void*, ModContext*, HookPreFn, const HookOptions*) {
    return MOD_UNSUPPORTED;
}

ModResult hookRegisterPost(void*, ModContext*, HookPostFn, const HookOptions*) {
    return MOD_UNSUPPORTED;
}

ModResult hookSetReplace(void*, ModContext*, HookReplaceFn, const HookOptions*) {
    return MOD_UNSUPPORTED;
}

ModResult hookDispatchPre(ModContext*, void*, void*, void*, int* out_skip_original) {
    if (out_skip_original != nullptr) {
        *out_skip_original = 0;
    }
    return MOD_UNSUPPORTED;
}

ModResult hookDispatchPost(ModContext*, void*, void*, void*) {
    return MOD_UNSUPPORTED;
}

void hookRemoveMod(ModContext*) {
}

}  // namespace dusk

#endif
