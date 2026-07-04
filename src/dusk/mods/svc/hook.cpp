#include "registry.hpp"

#include "dusk/hook_system.hpp"
#include "dusk/mods/manifest.hpp"

namespace dusk::mods::svc {
namespace {

ModResult hook_install(
    ModContext* context, void* fnAddr, void* trampolineFn, void** outOriginalFn) {
    return hookInstallByAddr(context, fnAddr, trampolineFn, outOriginalFn);
}

ModResult hook_add_pre(
    ModContext* context, void* fnAddr, HookPreFn callback, const HookOptions* options) {
    return hookRegisterPre(fnAddr, context, callback, options);
}

ModResult hook_add_post(
    ModContext* context, void* fnAddr, HookPostFn callback, const HookOptions* options) {
    return hookRegisterPost(fnAddr, context, callback, options);
}

ModResult hook_set_replace(
    ModContext* context, void* fnAddr, HookReplaceFn callback, const HookOptions* options) {
    return hookSetReplace(fnAddr, context, callback, options);
}

ModResult hook_dispatch_pre(
    ModContext* context, void* fnAddr, void* args, void* retval, int* outSkipOriginal) {
    return hookDispatchPre(context, fnAddr, args, retval, outSkipOriginal);
}

ModResult hook_dispatch_post(ModContext* context, void* fnAddr, void* args, void* retval) {
    return hookDispatchPost(context, fnAddr, args, retval);
}

ModResult hook_resolve(ModContext*, const char* symbol, void** outAddr, uint32_t* outFlags) {
    if (symbol == nullptr || outAddr == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    switch (manifest::resolve(symbol, outAddr, outFlags)) {
    case manifest::ResolveStatus::Ok:
        return MOD_OK;
    case manifest::ResolveStatus::Unavailable:
        return MOD_UNSUPPORTED;
    case manifest::ResolveStatus::NotFound:
        return MOD_UNAVAILABLE;
    case manifest::ResolveStatus::Ambiguous:
        return MOD_CONFLICT;
    }
    return MOD_ERROR;
}

constexpr HookService s_hookService{
    .header = SERVICE_HEADER(HookService, HOOK_SERVICE_MAJOR, HOOK_SERVICE_MINOR),
    .install = hook_install,
    .add_pre = hook_add_pre,
    .add_post = hook_add_post,
    .set_replace = hook_set_replace,
    .dispatch_pre = hook_dispatch_pre,
    .dispatch_post = hook_dispatch_post,
    .resolve = hook_resolve,
};

}  // namespace

const HookService& hook_service() {
    return s_hookService;
}

}  // namespace dusk::mods::svc
