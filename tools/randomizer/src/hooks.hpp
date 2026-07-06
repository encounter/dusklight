#pragma once

#include "mods/svc/hook.h"

namespace randomizer::hooks {

// Install every game hook the randomizer needs (save-accessor tweaks, flow query
// tweaks, item dispatch, event seams). Called once from mod_initialize; the host
// removes the hooks when the mod unloads. Callbacks gate on randomizer_IsActive().
ModResult install(const HookService* hooks);

}  // namespace randomizer::hooks
