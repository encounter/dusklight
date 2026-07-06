#pragma once

#include "mods/svc/config.h"
#include "mods/svc/flow.h"
#include "mods/svc/item.h"
#include "mods/svc/save.h"
#include "mods/svc/stage.h"
#include "mods/svc/text.h"

namespace randomizer::session {

struct Services {
    const ItemService* item;
    const FlowService* flow;
    const TextService* text;
    const StageService* stage;
    const SaveService* save;
    const ConfigService* config;
};

// Wire up save lifecycle observers + the pending-seed config var. Called once from
// mod_initialize.
ModResult initialize(const Services& services);

// Per-frame driver (mod_update): detects title-screen return (deactivates the seed),
// runs the RandomizerState execute tick while a seed is active.
void update();

// Load seed data for hash and register everything with the services. Returns false
// (and stays inactive) if the seed data can't be loaded.
bool activate_seed(const char* hash);

// Unregister all seed-derived service state and reset the context.
void deactivate_seed();

// ConfigService handle of the pending-seed var ("seed"), for UI binding.
ConfigVarHandle pending_seed_var();

// Sky-character letter counter, persisted as a SaveService blob on the current slot
// (replaces the branch's save-file padding byte mAncientDocumentNum).
uint8_t sky_characters();
void set_sky_characters(uint8_t num);

}  // namespace randomizer::session
