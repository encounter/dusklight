#include "dusk/hook_system.hpp"

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

struct InstalledHook {
    funchook_t* handle = nullptr;
    void* original = nullptr;
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

}  // namespace

ModResult hookInstallByAddr(ModContext*, void* fn_addr, void* tramp_fn, void** orig_store) {
    if (fn_addr == nullptr || tramp_fn == nullptr || orig_store == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }

    fn_addr = resolve_import_thunk(fn_addr);
    auto key = reinterpret_cast<uintptr_t>(fn_addr);
    auto it = s_installed.find(key);
    if (it != s_installed.end()) {
        *orig_store = it->second.original;
        return MOD_OK;
    }

    funchook_t* fh = funchook_create();
    if (fh == nullptr) {
        DuskLog.warn("HookSystem: funchook_create failed for {:p}", fn_addr);
        return MOD_ERROR;
    }

    void* fn = fn_addr;
    int prep = funchook_prepare(fh, &fn, tramp_fn);
    int inst = (prep == 0) ? funchook_install(fh, 0) : -1;
    if (prep != 0 || inst != 0) {
        const char* message = funchook_error_message(fh);
        DuskLog.warn("HookSystem: funchook failed for {:p} (prepare={} install={}): {}", fn_addr,
            prep, inst, message != nullptr && message[0] != '\0' ? message : "no details");
        funchook_destroy(fh);
        return MOD_ERROR;
    }

    s_installed[key] = InstalledHook{fh, fn};
    *orig_store = fn;
    return MOD_OK;
}

ModResult hookRegisterPre(
    void* fn_addr, ModContext* context, HookPreFn callback, const HookOptions* options) {
    if (fn_addr == nullptr || context == nullptr || callback == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
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

void hookClearMod(ModContext* context) {
    for (auto& [addr, slot] : s_registry) {
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
    }
}

}  // namespace dusk
