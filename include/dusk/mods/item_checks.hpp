#pragma once

#include <cstdint>

// Item-check callouts for the mod item service, called from game code at "check" sites — the
// places that decide which item a chest, NPC, or reward grants. Mods can override the granted
// item per named check or observe grants (trackers). Kept free of mods/svc/item.h so game code
// does not pull the mod SDK; the registry lives in src/dusk/mods/loader/item_checks.cpp.
//
// At decomp sites prefer the DUSK_ITEM_CHECK macro from global.h (expands to nothing off-PC).
// Check names are stable identifiers: explicit human-readable names at ambiguous sites
// (e.g. "Herding Goats Reward"), or host-synthesized derived names at mechanical funnels
// (formats documented on the helpers below).

class fopAc_ac_c;

namespace dusk::mods {

// Resolve the item granted at a named check site: applies mod overrides/resolvers, notifies
// observers, returns the (possibly overridden) item number. giver may be null when no single
// actor is responsible for the grant.
uint8_t item_check(const char* name, uint8_t itemNo, fopAc_ac_c* giver);

// Derived-name helpers for funnels whose identity is mechanical; the host synthesizes the
// stable name so naming policy stays in one place.
// Name format "chest:<stage>:<boxNo>" — treasure chests (daTbox), boxNo = getTboxNo().
uint8_t item_check_chest(uint8_t boxNo, uint8_t itemNo, fopAc_ac_c* chest);
// Name format "boss:<stage>" — the per-stage boss reward funnel (fopAcM_createItemForBoss).
uint8_t item_check_boss(uint8_t itemNo);

}  // namespace dusk::mods
