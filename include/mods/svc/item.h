#pragma once

#include "mods/api.h"

#define ITEM_SERVICE_ID "dev.twilitrealm.dusklight.item"
#define ITEM_SERVICE_MAJOR 1u
#define ITEM_SERVICE_MINOR 0u

/* Handle for a resolver/observer registration. 0 is never a valid handle. */
typedef uint64_t ItemCheckHandle;

/*
 * Item checks.
 *
 * A "check" is a place in game code that decides which item a chest, NPC, or reward grants,
 * identified by a stable name. Ambiguous sites carry explicit human-readable names
 * ("Herding Goats Reward"); mechanical funnels use host-synthesized derived names
 * ("chest:<stage>:<boxNo>", "boss:<stage>"). Names are case-sensitive, non-empty UTF-8.
 *
 * Composition across mods: value overrides and resolvers apply in mod load order, so the
 * later-loaded mod wins; two mods overriding the same name logs a warning naming the winner.
 * Observers are notify-only, run after resolution with the final item, and never conflict —
 * use them for trackers instead of a pass-through resolver.
 *
 * Registrations are owned by the calling mod and removed automatically when it is disabled,
 * reloaded, or fails. All callbacks run on the game thread, inside the game function that
 * granted the item.
 */

/*
 * Snapshot describing one check resolution. Host-owned and valid only for the duration of the
 * callback; never allocate or store one. Fields may be appended in future minors.
 */
typedef struct ItemCheckInfo {
    /* Check name, valid only during the call. */
    const char* name;
    /* The granting fopAc_ac_c*, or NULL when no single actor is responsible. */
    const void* giver_actor;
    /* The item the vanilla game would grant (dItemNo_* value). */
    uint8_t vanilla_item;
    /* The item as resolved so far (== final item for observers). */
    uint8_t current_item;
} ItemCheckInfo;

/*
 * Resolver: return true and write *out_item to override the item, false to pass. info reflects
 * earlier resolutions in the chain via current_item.
 */
typedef bool (*ItemCheckResolveFn)(
    ModContext* ctx, const ItemCheckInfo* info, uint8_t* out_item, void* user_data);

/* Observer: notified after resolution; current_item is the item actually granted. */
typedef void (*ItemCheckObserveFn)(ModContext* ctx, const ItemCheckInfo* info, void* user_data);

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

    /* Observe every check resolution (trackers, Archipelago clients). */
    ModResult (*observe_checks)(ModContext* ctx, ItemCheckObserveFn fn, void* user_data,
        ItemCheckHandle* out_handle);

    /* Remove an observer previously registered by the calling mod. */
    ModResult (*unobserve_checks)(ModContext* ctx, ItemCheckHandle handle);

    /*
     * Planned next append group (item slots): claim_item_slot/release_item_slot for registering
     * new items into unused dItemNo_* entries, with the service owning the dItem_data table
     * writes. Not part of minor 0.
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
