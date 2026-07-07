#pragma once

#include "mods/api.h"

#define SAVE_SERVICE_ID "dev.twilitrealm.dusklight.save"
#define SAVE_SERVICE_MAJOR 1u
#define SAVE_SERVICE_MINOR 1u

/* Handle for a save-observer registration. 0 is never a valid handle. */
typedef uint64_t SaveObserverHandle;
typedef uint64_t SaveGateHandle;
typedef uint64_t SaveSlotInfoHandle;

/* Total blob storage per mod per save slot. */
#define SAVE_BLOB_BUDGET_BYTES 65536u

/*
 * Per-save-slot mod data.
 *
 * Named binary blobs that travel with a save slot: stored in a host-owned sidecar next to the
 * emulated memory card, written when the game writes the card (in-game save, autosave), and
 * following file-select copy and erase. Blobs are namespaced per mod — two mods can use the
 * same name without collision. Format versioning inside a blob is the mod's own concern.
 *
 * Blob calls target the *current* slot, which exists from save load / new-save creation until
 * the player returns to file select; MOD_UNAVAILABLE otherwise. Data written between game
 * saves lives in memory only — like vanilla save state, it persists when the player saves.
 * A new save clears the slot's previous blob data before on_new_save fires.
 *
 * Staleness: the sidecar snapshots a checksum of each slot's vanilla save data at write time
 * and warns at load when they diverge (e.g. the card file was replaced externally); blobs are
 * still delivered — treat them with suspicion in that session.
 *
 * Callbacks run on the game thread. Registrations are owned by the calling mod and removed
 * automatically when it is disabled, reloaded, or fails.
 */

/* slot is the save-file index (0..2). */
typedef void (*SaveEventFn)(ModContext* ctx, uint32_t slot, void* user_data);

/*
 * New-save gate: invoked when the player opens an empty file in file select, before name
 * entry. The gate runs an interactive flow (UiService dialogs/windows pushed from the
 * callback or its UI callbacks) and finishes it with complete_new_save_gate: proceed
 * continues to name entry (and eventually on_new_save), cancel backs out to file select.
 * File select stays paused until the gate completes AND its documents are closed; a gate
 * that neither completes nor has a document visible is skipped (as proceed) after a grace
 * period, with a warning. Gates from multiple mods chain in load order; the first cancel
 * ends the chain.
 */
typedef void (*SaveNewSaveGateFn)(ModContext* ctx, uint32_t slot, void* user_data);

/*
 * Slot info text for the file-select info panel: replaces the "Save time" and "Total play
 * time" labels next to a slot's data (e.g. "Randomizer" + the seed hash). Fixed-size UTF-8
 * buffers; the panel truncates to what its textboxes hold (~31 chars).
 */
typedef struct SaveSlotInfoText {
    uint32_t struct_size;
    char save_time[64];
    char play_time[64];
} SaveSlotInfoText;

#define SAVE_SLOT_INFO_TEXT_INIT {sizeof(SaveSlotInfoText), {0}, {0}}

/*
 * Fill out_text for slot and return true, or return false to pass (vanilla labels, or the
 * next provider). First non-pass wins, later-loaded mods first. Called while file select
 * rebuilds a slot's info panel; must be cheap and must not call back into SaveService.
 */
typedef bool (*SaveSlotInfoFn)(
    ModContext* ctx, uint32_t slot, SaveSlotInfoText* out_text, void* user_data);

typedef struct SaveService {
    ServiceHeader header;

    /*
     * Write a named blob for the current slot (copied). Fails with MOD_UNAVAILABLE when no
     * slot is active or the mod's SAVE_BLOB_BUDGET_BYTES for the slot would be exceeded.
     */
    ModResult (*set_blob)(ModContext* ctx, const char* name, const void* data, size_t size);

    /*
     * Read a named blob from the current slot. With buf NULL, writes the blob's size to
     * *inout_size. Otherwise *inout_size is the buffer capacity in, bytes written out.
     * MOD_UNAVAILABLE when no slot is active or the blob does not exist.
     */
    ModResult (*get_blob)(ModContext* ctx, const char* name, void* buf, size_t* inout_size);

    /* Remove a named blob from the current slot. */
    ModResult (*delete_blob)(ModContext* ctx, const char* name);

    /*
     * Observe save lifecycle. Any callback may be NULL:
     *  - on_new_save: a new file was finalized in file select (name entry complete); the
     *    slot's blob store is empty — write initial blobs here. Like vanilla save data,
     *    nothing hits disk until the first in-game save.
     *  - on_save_loaded: a slot was loaded into the live game (file select or quick-load);
     *    blobs are readable.
     *  - on_save_written: the game persisted the slot (menu save or autosave); blob data
     *    captured with it is on disk.
     */
    ModResult (*observe_saves)(ModContext* ctx, SaveEventFn on_new_save,
        SaveEventFn on_save_loaded, SaveEventFn on_save_written, void* user_data,
        SaveObserverHandle* out_handle);

    /* Remove an observer previously registered by the calling mod. */
    ModResult (*unobserve_saves)(ModContext* ctx, SaveObserverHandle handle);

    /* --- minor 1: slot peeks, new-save gates, slot info --- */

    /*
     * Read a named blob from any slot (0..2), including at file select when no slot is
     * active. Same buffer contract as get_blob. The calling mod's own namespace only.
     */
    ModResult (*peek_blob)(
        ModContext* ctx, uint32_t slot, const char* name, void* buf, size_t* inout_size);

    /* Register/remove a new-save gate (contract on SaveNewSaveGateFn above). */
    ModResult (*register_new_save_gate)(
        ModContext* ctx, SaveNewSaveGateFn gate, void* user_data, SaveGateHandle* out_handle);
    ModResult (*unregister_new_save_gate)(ModContext* ctx, SaveGateHandle handle);

    /*
     * Finish the calling mod's currently running gate. MOD_UNAVAILABLE unless a gate of
     * this mod is running and not yet completed.
     */
    ModResult (*complete_new_save_gate)(ModContext* ctx, bool proceed);

    /* Register/remove a slot info provider (contract on SaveSlotInfoFn above). */
    ModResult (*register_slot_info_provider)(
        ModContext* ctx, SaveSlotInfoFn provider, void* user_data, SaveSlotInfoHandle* out_handle);
    ModResult (*unregister_slot_info_provider)(ModContext* ctx, SaveSlotInfoHandle handle);
} SaveService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<SaveService> {
    static constexpr const char* id = SAVE_SERVICE_ID;
    static constexpr uint16_t major_version = SAVE_SERVICE_MAJOR;
};
#endif
