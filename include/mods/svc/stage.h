#pragma once

#include "mods/api.h"

#define STAGE_SERVICE_ID "dev.twilitrealm.dusklight.stage"
#define STAGE_SERVICE_MAJOR 1u
#define STAGE_SERVICE_MINOR 1u

/* Handle for an actor-edit registration. 0 is never a valid handle. */
typedef uint64_t StageActorHandle;

/* Handle for a layer-resolver registration. 0 is never a valid handle. */
typedef uint64_t StageLayerHandle;

/*
 * Layer resolver: called while the game derives the active stage layer for (stage, room_no)
 * when no caller forced an explicit layer (dComIfG_play_c::getLayerNo_common_common). It runs
 * only inside the vanilla derivation branch — never for an explicit layer >= 0 and never for
 * Twilight stages (layer 14) — so current_layer is always -1 today (passed for
 * future-proofing). Return true and write *out_layer (-1 = no layer, which falls back to the
 * previous layer like a vanilla no-match; 0..14 = the layer) to supply the layer and skip the
 * vanilla event-bit ladder; return false to pass to the next resolver. First non-pass wins,
 * later-loaded mods first. Game thread, during stage/room load.
 */
typedef bool (*StageLayerResolveFn)(ModContext* ctx, const char* stage, int32_t room_no,
    int32_t current_layer, int32_t* out_layer, void* user_data);

/* room value for actors placed by the stage file rather than a room file. */
#define STAGE_ROOM_STAGE_FILE 0xFFu
/* layer value matching every layer. */
#define STAGE_LAYER_ANY 0xFFu

/* Actor record sizes (.bmg-style wire format, big-endian fields, exactly as on disc). */
#define STAGE_ACTOR_RECORD_SIZE 32u /* ACTR/TGOB/TRES: name[8] + params + pos + angle + enemyNo */
#define STAGE_TGSC_RECORD_SIZE 35u  /* TGSC/SCOB: ACTR + scale[3] */

/*
 * Stage actor edits: replace, delete, or add actors in room/stage placement data, applied at
 * spawn time (they take effect on the next room (re)load — already-spawned actors are not
 * despawned retroactively).
 *
 * Patches and deletes target a vanilla record by the CRC-32 (IEEE, polynomial 0xEDB88320,
 * reflected) of its raw wire bytes — STAGE_ACTOR_RECORD_SIZE for ACTR-shaped records,
 * STAGE_TGSC_RECORD_SIZE for scaled ones; the record's size tells the host which CRC to
 * match. stage is the 8-character stage name (e.g. "F_SP103"). layer is a specific layer or
 * STAGE_LAYER_ANY; room is the room number, or STAGE_ROOM_STAGE_FILE for stage-file actors
 * (additions require a real room).
 *
 * When several mods edit the same record, the later-loaded mod wins (warning logged).
 * Registrations are owned by the calling mod and removed automatically when it is disabled,
 * reloaded, or fails.
 */
typedef struct StageService {
    ServiceHeader header;

    /*
     * Replace the record matching record_crc with record (record_size bytes, wire format —
     * it also determines which CRC size is matched).
     */
    ModResult (*patch_actor)(ModContext* ctx, const char* stage, uint8_t room, uint8_t layer,
        uint32_t record_crc, const void* record, size_t record_size, StageActorHandle* out_handle);

    /* Suppress the spawn of the record matching record_crc (matched at either CRC size). */
    ModResult (*delete_actor)(ModContext* ctx, const char* stage, uint8_t room, uint8_t layer,
        uint32_t record_crc, StageActorHandle* out_handle);

    /* Spawn an additional actor from record (wire format) when the room loads. */
    ModResult (*add_actor)(ModContext* ctx, const char* stage, uint8_t room, uint8_t layer,
        const void* record, size_t record_size, StageActorHandle* out_handle);

    /* Remove an edit previously registered by the calling mod. */
    ModResult (*remove_actor_edit)(ModContext* ctx, StageActorHandle handle);

    /* --- minor 1: layer resolver --- */

    /* Add fn to the layer-resolver chain (see StageLayerResolveFn for the contract). */
    ModResult (*register_layer_resolver)(
        ModContext* ctx, StageLayerResolveFn fn, void* user_data, StageLayerHandle* out_handle);

    /* Remove a resolver previously registered by the calling mod. */
    ModResult (*unregister_layer_resolver)(ModContext* ctx, StageLayerHandle handle);
} StageService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<StageService> {
    static constexpr const char* id = STAGE_SERVICE_ID;
    static constexpr uint16_t major_version = STAGE_SERVICE_MAJOR;
};
#endif
