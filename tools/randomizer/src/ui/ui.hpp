#pragma once

#include "mods/svc/ui.h"

namespace randomizer::ui {

// Register the "Randomizer" menu-bar tab (seed + settings window). Called once
// from mod_initialize.
ModResult initialize(const UiService* ui, ConfigVarHandle pending_seed_var);

// Per-frame: polls the seed-generation worker and updates the progress dialog.
void update();

}  // namespace randomizer::ui
