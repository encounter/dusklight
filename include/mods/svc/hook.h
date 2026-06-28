#pragma once

#include "mods/api.h"

#define HOOK_SERVICE_ID "dev.twilitrealm.dusklight.hook"
#define HOOK_SERVICE_MAJOR 1u
#define HOOK_SERVICE_MINOR 0u

typedef enum HookAction {
    HOOK_CONTINUE = 0,
    HOOK_SKIP_ORIGINAL = 1,
} HookAction;

typedef enum HookReplacePolicy {
    HOOK_REPLACE_CONFLICT = 0,
    HOOK_REPLACE_PRIORITY = 1,
    HOOK_REPLACE_OVERRIDE = 2,
} HookReplacePolicy;

typedef enum HookFlags {
    HOOK_FLAG_NONE = 0u,
    HOOK_FLAG_FAIL_ON_CONFLICT = 1u << 0u,
} HookFlags;

typedef HookAction (*HookPreFn)(
    ModContext* ctx, void* args, void* retval, void* userdata);
typedef void (*HookPostFn)(ModContext* ctx, void* args, void* retval, void* userdata);
typedef void (*HookReplaceFn)(ModContext* ctx, void* args, void* retval, void* userdata);

typedef struct HookOptions {
    uint32_t struct_size;
    int32_t priority;
    uint32_t flags;
    HookReplacePolicy replace_policy;
    void* userdata;
} HookOptions;

#define HOOK_OPTIONS_INIT                                                                         \
    { sizeof(HookOptions), 0, HOOK_FLAG_NONE, HOOK_REPLACE_CONFLICT, NULL }

typedef struct HookService {
    ServiceHeader header;

    ModResult (*install)(
        ModContext* ctx, void* fn_addr, void* trampoline_fn, void** out_original_fn);
    ModResult (*add_pre)(
        ModContext* ctx, void* fn_addr, HookPreFn callback, const HookOptions* options);
    ModResult (*add_post)(
        ModContext* ctx, void* fn_addr, HookPostFn callback, const HookOptions* options);
    ModResult (*set_replace)(
        ModContext* ctx, void* fn_addr, HookReplaceFn callback, const HookOptions* options);

    ModResult (*dispatch_pre)(
        ModContext* ctx, void* fn_addr, void* args, void* retval, int* out_skip_original);
    ModResult (*dispatch_post)(ModContext* ctx, void* fn_addr, void* args, void* retval);
} HookService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<HookService> {
    static constexpr const char* id = HOOK_SERVICE_ID;
    static constexpr uint16_t major_version = HOOK_SERVICE_MAJOR;
};
#endif
