#pragma once

#include "mods/svc/hook.h"

namespace dusk {

ModResult hookInstallByAddr(ModContext* context, void* fn_addr, void* tramp_fn, void** orig_store);

ModResult hookRegisterPre(
    void* fn_addr, ModContext* context, HookPreFn callback, const HookOptions* options);
ModResult hookRegisterPost(
    void* fn_addr, ModContext* context, HookPostFn callback, const HookOptions* options);
ModResult hookSetReplace(
    void* fn_addr, ModContext* context, HookReplaceFn callback, const HookOptions* options);

ModResult hookDispatchPre(
    ModContext* context, void* fn_addr, void* args, void* retval, int* out_skip_original);
ModResult hookDispatchPost(ModContext* context, void* fn_addr, void* args, void* retval);

void hookClearMod(ModContext* context);

}  // namespace dusk
