#pragma once

#include "mods/svc/save.h"
#include "mods/svc/ui.h"

namespace randomizer::ui {

// Register the "Randomizer" menu-bar tab (seed + settings window) and the new-save
// play-type gate (file-select Vanilla/Randomizer flow). Called once from
// mod_initialize.
ModResult initialize(const UiService* ui, const SaveService* save, const ConfigService* config,
    ConfigVarHandle pending_seed_var);

// Per-frame: polls the seed-generation worker and updates the progress dialog.
void update();

}  // namespace randomizer::ui
