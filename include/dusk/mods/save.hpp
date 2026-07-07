#pragma once

#include <cstddef>
#include <cstdint>

// Save-lifecycle seam entry points for the mod save service, called from the slot-aware save
// paths (d_file_select.cpp, d_menu_save.cpp, d_s_logo.cpp quick-load, src/dusk/autosave.cpp).
// All three save slots live in one emulated-card file, so slot identity only exists at these
// game-layer call sites — the registry and sidecar live in src/dusk/mods/loader/save.cpp.
// slotData points at the slot's serialized save (QUEST_LOG_SIZE bytes) for the staleness
// snapshot; it is read synchronously during the call.

namespace dusk::mods {

// A new file was finalized in file select (name entry complete) for slot.
void save_slot_new(uint32_t slot);
// slot was decoded into the live game info (file select start / quick-load).
void save_slot_loaded(uint32_t slot, const void* slotData);
// slot was successfully written to the card (menu save / autosave).
void save_slot_written(uint32_t slot, const void* slotData);
// File-select copy/erase (both rewrite the card; the sidecar follows).
void save_slot_copied(uint32_t fromSlot, uint32_t toSlot);
void save_slot_erased(uint32_t slot);
// File select opened: no slot is current until load/new fires again.
void save_no_slot();

// New-save gate chain (interactive mod flows between opening an empty file and name entry).
// File select begins the chain when the player opens an empty slot and polls update() every
// frame of its gate proc; the result is sticky until the next begin.
enum class SaveGateResult {
    Pending,  // a gate is still running (or its documents are still closing)
    Proceed,  // every gate proceeded (or none are registered) — continue to name entry
    Cancel,   // a gate canceled — back out to file select
};
bool save_new_save_gates_registered();
void save_new_save_gates_begin(uint32_t slot);
SaveGateResult save_new_save_gates_update();

// First non-pass slot-info text for the file-select info panel; false leaves the vanilla
// "Save time" / "Total play time" labels.
bool save_slot_info_text(
    uint32_t slot, char* saveTime, size_t saveTimeSize, char* playTime, size_t playTimeSize);

}  // namespace dusk::mods
