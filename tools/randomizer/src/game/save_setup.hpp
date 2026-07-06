#pragma once

namespace randomizer::save_setup {

// Apply the active seed's starting state to a freshly created save file
// (branch: dComIfGs_setupRandomizerSave, called from dFile_select_c::nameInput2;
// now driven by the SaveService on_new_save callback).
void setup_new_save();

}  // namespace randomizer::save_setup
