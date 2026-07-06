#pragma once

#include <cstdint>

// Item-check and item-give callouts for the mod item service, called from game code. A "check"
// site resolves which item a chest, NPC, or reward grants (pure — safe to call repeatedly, at
// spawn for display and again at pickup); a "give" is the moment an item lands in the player's
// inventory, reported to mod observers from execItemGet. Kept free of mods/svc/item.h so game
// code does not pull the mod SDK; the registry lives in src/dusk/mods/loader/item_checks.cpp
// and the give pipeline in src/dusk/mods/loader/item_gives.cpp.
//
// At decomp sites prefer the DUSK_ITEM_CHECK macro from global.h (expands to nothing off-PC).
// Check names are stable identifiers: explicit human-readable names at ambiguous sites
// (e.g. "Herding Goats Reward"), or host-synthesized derived names at mechanical funnels
// (formats documented on the helpers below).
//
// Give attribution: a site that spawns the granting actor tags it with an interned name id
// (give_tag*), carried on fopAc_ac_c::mDuskGiveTag through the fopAcM_create funnels; the
// execItemGet seam turns the tag back into the check name for observers. Tag 0 = unattributed.

class fopAc_ac_c;

namespace dusk::mods {

// Resolve the item granted at a named check site: applies mod overrides/resolvers and returns
// the (possibly overridden) item number. Pure — no observers fire; the give is reported
// separately when the item is actually granted. giver may be null when no single actor is
// responsible for the grant.
uint8_t item_check(const char* name, uint8_t itemNo, fopAc_ac_c* giver);

// Derived-name helpers for funnels whose identity is mechanical; the host synthesizes the
// stable name so naming policy stays in one place.
// Name format "chest:<stage>:<boxNo>" — treasure chests (daTbox), boxNo = getTboxNo().
uint8_t item_check_chest(uint8_t boxNo, uint8_t itemNo, fopAc_ac_c* chest);
// Name format "boss:<stage>" — the per-stage boss reward funnel (fopAcM_createItemForBoss).
uint8_t item_check_boss(uint8_t itemNo);

// Intern a check name for give attribution; returns a stable non-zero id for the session.
// Derived-name variants mirror the item_check_* helpers above.
uint32_t give_tag(const char* name);
uint32_t give_tag_chest(uint8_t boxNo);
uint32_t give_tag_boss();

// Resolve a named check and queue the result to be granted at the next safe moment (resolution
// runs at dispatch time, so progressive items see the then-current inventory). For sites whose
// vanilla behavior grants nothing, pass dItemNo_NONE_e as itemNo — a NONE resolution grants
// nothing. This is the in-tree entry to the give queue; mods use ItemService::give_item.
void item_check_enqueue(const char* name, uint8_t itemNo, fopAc_ac_c* giver);

// Give seam, called from execItemGet after the inventory write: notifies mod give observers
// and completes a matching in-flight queued give. giveTag is the granting actor's interned
// name id (0 = unattributed); giver may be null.
void item_granted(uint8_t itemNo, uint32_t giveTag, fopAc_ac_c* giver);

// Get-item demo seam (daAlink_c::procCoGetItemInit): a queued demo give carries no item event
// partner, so the demo init must force the create-item path (mDemo param0 = 0x100) and spawn
// the item from the event's GtItm — the randomizer branch's proven mechanism.
// give_queue_dispatching() is true while an in-flight queued give awaits its demo item;
// give_queue_take_tag() marks it spawned and returns its give tag for the created actor.
bool give_queue_dispatching();
uint32_t give_queue_take_tag();

}  // namespace dusk::mods
