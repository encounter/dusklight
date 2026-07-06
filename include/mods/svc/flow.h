#pragma once

#include "mods/api.h"

#define FLOW_SERVICE_ID "dev.twilitrealm.dusklight.flow"
#define FLOW_SERVICE_MAJOR 1u
#define FLOW_SERVICE_MINOR 0u

/* Handle for a flow-node override registration. 0 is never a valid handle. */
typedef uint64_t FlowNodeHandle;

/*
 * Message-flow extensions.
 *
 * The game's message flows (.bmg flow graphs) dispatch branch nodes through a query table and
 * event nodes through an event table. This service extends both with runtime-registered
 * handlers, and lets mods redirect individual flow nodes to mod-supplied replacements — the
 * combination needed to reroute or extend NPC dialogue graphs.
 *
 * IDs are runtime-allocated per session and must never be persisted or hardcoded: register (or
 * find_*) at init and bake the returned id into node data you generate at runtime. Query ids
 * are allocated from 0x100 up (u16 space, effectively unbounded). Event ids live in the node
 * data as a single byte, so the shared budget across all mods is 43..255 (213 ids); exhaustion
 * fails registration loudly.
 *
 * Names are a global namespace (so mod B can find_* mod A's handlers); registering an already
 * registered name fails with MOD_CONFLICT. Prefix mod-private names with your mod id
 * ("my.mod:gate_open"). Handler callbacks run on the game thread, inside message-flow
 * processing.
 *
 * Registrations are owned by the calling mod and removed automatically when it is disabled,
 * reloaded, or fails. Dispatch of an unregistered extension id warns and acts as a safe
 * default (query -> 0 / first branch target, event -> no-op advance).
 */

/*
 * Query handler for branch nodes. param is the node's argument; mode is the dispatch pass
 * (0 = message-count scan, 1 = live branch). The return value selects the branch target
 * (0 = first). speaker_actor is the speaking fopAc_ac_c*, possibly NULL.
 */
typedef uint16_t (*FlowQueryFn)(
    ModContext* ctx, uint16_t param, void* speaker_actor, int mode, void* user_data);

/*
 * Event handler for event nodes. params is the node's 4 argument bytes (.bmg wire order).
 * Side effects only: the flow advances to the node's next index regardless.
 */
typedef void (*FlowEventFn)(
    ModContext* ctx, const uint8_t* params, void* speaker_actor, void* user_data);

typedef struct FlowService {
    ServiceHeader header;

    /* Register a query handler; writes the allocated id (>= 0x100) to out_id. */
    ModResult (*register_query)(
        ModContext* ctx, const char* name, FlowQueryFn fn, void* user_data, uint16_t* out_id);

    /* Remove a query previously registered by the calling mod. */
    ModResult (*unregister_query)(ModContext* ctx, uint16_t id);

    /* Register an event handler; writes the allocated id (43..255) to out_id. */
    ModResult (*register_event)(
        ModContext* ctx, const char* name, FlowEventFn fn, void* user_data, uint8_t* out_id);

    /* Remove an event previously registered by the calling mod. */
    ModResult (*unregister_event)(ModContext* ctx, uint8_t id);

    /* Look up another mod's registration by name. MOD_UNAVAILABLE when absent. */
    ModResult (*find_query)(ModContext* ctx, const char* name, uint16_t* out_id);
    ModResult (*find_event)(ModContext* ctx, const char* name, uint8_t* out_id);

    /*
     * Redirect one flow node: whenever the flow at (group, node_index) is processed, the
     * 8-byte replacement node is used instead of the .bmg's. node_bytes is in .bmg wire
     * format (big-endian fields; type/next-index/args exactly as on disc) and is copied.
     * group is the message group id, except nodes of the shared bmg (zel_00) register under
     * group 0. Replaces the calling mod's previous override for the same node, if any; when
     * several mods override one node, the later-loaded mod wins (warning logged).
     */
    ModResult (*override_flow_node)(ModContext* ctx, uint16_t group, uint16_t node_index,
        const void* node_bytes, FlowNodeHandle* out_handle);

    /* Remove a node override previously registered by the calling mod. */
    ModResult (*clear_flow_node_override)(ModContext* ctx, FlowNodeHandle handle);
} FlowService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<FlowService> {
    static constexpr const char* id = FLOW_SERVICE_ID;
    static constexpr uint16_t major_version = FLOW_SERVICE_MAJOR;
};
#endif
