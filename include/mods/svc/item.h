#pragma once

#include "mods/api.h"

#define ITEM_SERVICE_ID "dev.twilitrealm.dusklight.item"
#define ITEM_SERVICE_MAJOR 2u
#define ITEM_SERVICE_MINOR 0u

/* Handle for a resolver registration. 0 is never a valid handle. */
typedef uint64_t ItemCheckHandle;
/* Handle for a give-observer registration. 0 is never a valid handle. */
typedef uint64_t ItemGiveHandle;

/*
 * Item checks and item gives.
 *
 * A "check" is a place in game code that decides which item a chest, NPC, or reward grants,
 * identified by a stable name. Ambiguous sites carry explicit human-readable names
 * ("Herding Goats Reward"); mechanical funnels use host-synthesized derived names
 * ("chest:<stage>:<boxNo>", "boss:<stage>"). Names are case-sensitive, non-empty UTF-8.
 *
 * Resolution is PURE and idempotent: the override/resolver chain maps (name, vanilla item) to
 * the item a check grants, with no side effects. The game may resolve the same check any number
 * of times — at spawn to pick a display model, again at pickup for progressive items — so
 * resolvers must not treat "my resolver ran" as "the player received the item".
 *
 * A "give" is the moment an item actually lands in the player's inventory. Give observers fire
 * once per grant, after the inventory write, for EVERY grant that passes through the game's
 * item pipeline — check-attributed gives carry the check name; everything else (vanilla
 * pickups, other mods' gives) is reported with a NULL name. This is the surface trackers and
 * Archipelago clients should watch; never infer completion from resolver calls.
 *
 * dItemNo_NONE_e (0xFF) means "nothing" on both sides of resolution: a check whose vanilla
 * grant is nothing resolves over NONE, and resolving a real item to NONE suppresses the give
 * (give observers still see the suppressed give with item == NONE, so a client granting the
 * "real" item externally can react).
 *
 * Composition across mods: value overrides and resolvers apply in mod load order, so the
 * later-loaded mod wins; two mods overriding the same name logs a warning naming the winner.
 * Observers are notify-only and never conflict.
 *
 * Registrations and queued gives are owned by the calling mod and removed automatically when
 * it is disabled, reloaded, or fails. All callbacks run on the game thread.
 */

/*
 * Snapshot describing one check resolution. Host-owned and valid only for the duration of the
 * callback; never allocate or store one. Fields may be appended in future minors.
 */
typedef struct ItemCheckInfo {
    /* Check name, valid only during the call. */
    const char* name;
    /* The granting fopAc_ac_c*, or NULL when no single actor is responsible or not known. */
    const void* giver_actor;
    /* The item the vanilla game would grant (dItemNo_* value). */
    uint8_t vanilla_item;
    /* The item as resolved so far by earlier links in the chain. */
    uint8_t current_item;
} ItemCheckInfo;

/*
 * Resolver: return true and write *out_item to override the item, false to pass. Must be pure —
 * it may run several times per check and must not assume the result is ever granted.
 */
typedef bool (*ItemCheckResolveFn)(
    ModContext* ctx, const ItemCheckInfo* info, uint8_t* out_item, void* user_data);

/* Where a give came from. */
typedef enum ItemGiveOrigin {
    /* Granted directly by game code (pickups, demos, NPC gives). */
    ITEM_GIVE_ORIGIN_GAME = 0,
    /* Dispatched from the give queue as a get-item demo. */
    ITEM_GIVE_ORIGIN_QUEUE = 1,
    /* Dispatched from the give queue silently. */
    ITEM_GIVE_ORIGIN_QUEUE_SILENT = 2,
} ItemGiveOrigin;

/*
 * Snapshot describing one grant. Host-owned and valid only for the duration of the callback.
 * Fields may be appended in future minors.
 */
typedef struct ItemGiveInfo {
    /* Originating check name, or NULL for an unattributed grant. Valid only during the call. */
    const char* check_name;
    /* The granting fopAc_ac_c*, or NULL when unknown. */
    const void* giver_actor;
    /* The item granted (dItemNo_* value); dItemNo_NONE_e for a suppressed give. */
    uint8_t item;
    /* ItemGiveOrigin value. */
    uint8_t origin;
} ItemGiveInfo;

/* Observer: notified once per grant, after the inventory write. */
typedef void (*ItemGiveObserveFn)(ModContext* ctx, const ItemGiveInfo* info, void* user_data);

/* give_item flags. */
enum {
    /*
     * Grant silently (direct inventory write at a safe frame) instead of playing the get-item
     * demo. Use for bulk grants (e.g. Archipelago receives after reconnect); the queue drains
     * consecutive silent entries in a single frame.
     */
    ITEM_GIVE_SILENT = 1u << 0,
    /*
     * Re-run check resolution at dispatch time: item_no is treated as the check's vanilla item
     * and the chain resolves it just before the grant (progressive items resolve against the
     * inventory as it is then, not as it was at enqueue). Requires a non-NULL check_name.
     * A NONE resolution drops the entry (observers see the suppressed give).
     */
    ITEM_GIVE_RESOLVE = 1u << 1,
};

typedef struct ItemService {
    ServiceHeader header;

    /*
     * Grant item_no at the named check. Replaces the calling mod's previous override for the
     * same name, if any.
     */
    ModResult (*set_check_override)(ModContext* ctx, const char* name, uint8_t item_no);

    /* Remove the calling mod's value override for name. */
    ModResult (*clear_check_override)(ModContext* ctx, const char* name);

    /*
     * Register a resolver for one named check, or for every check when name is NULL (the
     * catch-all a full randomizer wants). A mod may register multiple resolvers; they run in
     * registration order after its value overrides.
     */
    ModResult (*set_check_resolver)(ModContext* ctx, const char* name, ItemCheckResolveFn fn,
        void* user_data, ItemCheckHandle* out_handle);

    /* Remove a resolver previously registered by the calling mod. */
    ModResult (*clear_check_resolver)(ModContext* ctx, ItemCheckHandle handle);

    /*
     * Run the resolution chain for a named check right now and write the result to *out_item.
     * Pure query for display/peek sites (shop pedestals, file-select previews); fires no
     * observers. vanilla_item is what the vanilla game would grant there.
     */
    ModResult (*resolve_check)(
        ModContext* ctx, const char* name, uint8_t vanilla_item, uint8_t* out_item);

    /*
     * Queue an item to be granted to the player. check_name (nullable) attributes the give for
     * observers and is required with ITEM_GIVE_RESOLVE; flags is a bitmask of ITEM_GIVE_*.
     *
     * Queue mechanics:
     *  - Global FIFO across all mods; at most one demo give is in flight at a time and runs the
     *    vanilla get-item demo (DEFAULT_GETITEM) to completion before the next entry dispatches.
     *  - Entries dispatch only at a safe moment: the player actor exists, Link is in a plain
     *    wait/move proc (human or wolf), no event/cutscene is running, and no message flow is
     *    active. Until then the queue simply waits; there is no timeout.
     *  - Consecutive ITEM_GIVE_SILENT entries at the head drain together in one frame.
     *  - The queue is volatile and belongs to the active save slot: it is cleared on returning
     *    to title, on save-slot change, and on shutdown. Persist-and-replay is the caller's
     *    concern (an Archipelago client resyncs from its server).
     *  - Entries queued by a mod are dropped if that mod is disabled, reloaded, or fails.
     *  - item_no == dItemNo_NONE_e is MOD_INVALID_ARGUMENT unless ITEM_GIVE_RESOLVE is set
     *    (a vanilla-nothing check resolves over NONE).
     *  - Returns MOD_UNAVAILABLE when the queue is full.
     */
    ModResult (*give_item)(
        ModContext* ctx, const char* check_name, uint8_t item_no, uint32_t flags);

    /* Observe every grant (trackers, Archipelago clients). */
    ModResult (*observe_gives)(
        ModContext* ctx, ItemGiveObserveFn fn, void* user_data, ItemGiveHandle* out_handle);

    /* Remove a give observer previously registered by the calling mod. */
    ModResult (*unobserve_gives)(ModContext* ctx, ItemGiveHandle handle);

    /*
     * Planned next append group (item slots): claim_item_slot/release_item_slot for registering
     * new items into unused dItemNo_* entries, with the service owning the dItem_data table
     * writes, plus per-item get-demo params (face/SE/status). Not part of minor 0.
     */
} ItemService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<ItemService> {
    static constexpr const char* id = ITEM_SERVICE_ID;
    static constexpr uint16_t major_version = ITEM_SERVICE_MAJOR;
};
#endif
