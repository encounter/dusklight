#include "aurora/lib/logging.hpp"
#include "dusk/mod_loader.hpp"
#include "dusk/mods/flow.hpp"
#include "loader.hpp"

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::flow");

constexpr uint16_t kFirstQueryId = 0x100;
constexpr uint16_t kFirstEventId = FlowFirstExtensionEvent;  // 43
constexpr uint16_t kLastEventId = 0xFF;
constexpr int kEventBudget = kLastEventId - kFirstEventId + 1;  // 213

struct FlowHandlerRecord {
    std::string name;
    LoadedMod* mod = nullptr;
    void* fn = nullptr;  // FlowQueryFn or FlowEventFn
    void* userData = nullptr;
};

struct NodeOverrideRecord {
    uint64_t handle = 0;
    LoadedMod* mod = nullptr;
    uint64_t seq = 0;
    std::array<uint8_t, 8> node{};
};

// Game thread only: mutations happen in service calls made from mod code (init/update/hooks
// run inside ModLoader::tick), in the loader's deactivate path, or at shutdown — and dispatch
// fires from message-flow processing on the game thread.
std::unordered_map<uint16_t, FlowHandlerRecord> s_queries;
std::unordered_map<uint16_t, FlowHandlerRecord> s_events;
std::unordered_map<std::string, uint16_t> s_queryNames;
std::unordered_map<std::string, uint16_t> s_eventNames;
uint16_t s_nextQueryId = kFirstQueryId;
// Event ids are a 213-slot shared budget (u8 node field); freed ids are reused.
std::vector<uint16_t> s_freeEventIds;
uint16_t s_nextEventId = kFirstEventId;

std::unordered_map<uint32_t, std::vector<NodeOverrideRecord>> s_nodeOverrides;
uint64_t s_nextHandle = 1;
uint64_t s_nextSeq = 1;

std::unordered_set<uint16_t> s_warnedIds;
std::unordered_set<uint32_t> s_warnedNodeKeys;

// Position in m_mods (dependency-sorted load order) + 1; later-loaded mods win.
int32_t compute_mod_priority(const LoadedMod& mod) {
    int32_t index = 0;
    for (const auto& other : ModLoader::instance().mods()) {
        ++index;
        if (&other == &mod) {
            return index;
        }
    }
    return index + 1;
}

const FlowHandlerRecord* find_live_handler(
    std::unordered_map<uint16_t, FlowHandlerRecord>& table, uint16_t id, const char* kind) {
    const auto it = table.find(id);
    if (it == table.end() || !it->second.mod->active) {
        if (s_warnedIds.insert(id).second) {
            Log.warn("flow {} {} dispatched but not registered (stale flow data?); "
                     "using safe default",
                kind, id);
        }
        return nullptr;
    }
    return &it->second;
}

}  // namespace

uint16_t flow_dispatch_query(uint16_t queryIdx, uint16_t param, fopAc_ac_c* speaker, int mode) {
    const auto* record = find_live_handler(s_queries, queryIdx, "query");
    if (record == nullptr) {
        return 0;
    }
    // Copy before invoking: the callback may (un)register handlers.
    const auto local = *record;
    try {
        return reinterpret_cast<FlowQueryFn>(local.fn)(
            local.mod->context.get(), param, speaker, mode, local.userData);
    } catch (const std::exception& e) {
        fail_mod(*local.mod, MOD_ERROR,
            fmt::format("exception in flow query '{}': {}", local.name, e.what()));
    } catch (...) {
        fail_mod(*local.mod, MOD_ERROR,
            fmt::format("unknown exception in flow query '{}'", local.name));
    }
    return 0;
}

void flow_dispatch_event(uint16_t eventIdx, const uint8_t params[4], fopAc_ac_c* speaker) {
    const auto* record = find_live_handler(s_events, eventIdx, "event");
    if (record == nullptr) {
        return;
    }
    const auto local = *record;
    try {
        reinterpret_cast<FlowEventFn>(local.fn)(
            local.mod->context.get(), params, speaker, local.userData);
    } catch (const std::exception& e) {
        fail_mod(*local.mod, MOD_ERROR,
            fmt::format("exception in flow event '{}': {}", local.name, e.what()));
    } catch (...) {
        fail_mod(*local.mod, MOD_ERROR,
            fmt::format("unknown exception in flow event '{}'", local.name));
    }
}

bool flow_node_override(uint32_t key, void* outNode) {
    const auto it = s_nodeOverrides.find(key);
    if (it == s_nodeOverrides.end()) {
        return false;
    }
    const NodeOverrideRecord* winner = nullptr;
    int32_t winnerPriority = 0;
    const LoadedMod* firstOwner = nullptr;
    for (const auto& record : it->second) {
        if (!record.mod->active) {
            continue;
        }
        if (firstOwner == nullptr) {
            firstOwner = record.mod;
        } else if (firstOwner != record.mod && s_warnedNodeKeys.insert(key).second) {
            Log.warn("flow node {:#x} overridden by multiple mods; the later-loaded one wins",
                key);
        }
        const auto priority = compute_mod_priority(*record.mod);
        if (winner == nullptr || priority > winnerPriority ||
            (priority == winnerPriority && record.seq > winner->seq))
        {
            winner = &record;
            winnerPriority = priority;
        }
    }
    if (winner == nullptr) {
        return false;
    }
    std::memcpy(outNode, winner->node.data(), winner->node.size());
    return true;
}

ModResult flow_register_query(
    LoadedMod& mod, const char* name, FlowQueryFn fn, void* userData, uint16_t& outId) {
    if (s_queryNames.contains(name)) {
        Log.error("[{}] flow query '{}' is already registered", mod.metadata.id, name);
        return MOD_CONFLICT;
    }
    const uint16_t id = s_nextQueryId++;
    s_queries.emplace(id,
        FlowHandlerRecord{
            .name = name, .mod = &mod, .fn = reinterpret_cast<void*>(fn), .userData = userData});
    s_queryNames.emplace(name, id);
    s_warnedIds.erase(id);
    outId = id;
    return MOD_OK;
}

ModResult flow_register_event(
    LoadedMod& mod, const char* name, FlowEventFn fn, void* userData, uint16_t& outId) {
    if (s_eventNames.contains(name)) {
        Log.error("[{}] flow event '{}' is already registered", mod.metadata.id, name);
        return MOD_CONFLICT;
    }
    uint16_t id;
    if (!s_freeEventIds.empty()) {
        id = s_freeEventIds.back();
        s_freeEventIds.pop_back();
    } else if (s_nextEventId <= kLastEventId) {
        id = s_nextEventId++;
    } else {
        Log.error("[{}] flow event '{}' rejected: the shared event-id budget ({}) is exhausted",
            mod.metadata.id, name, kEventBudget);
        return MOD_UNAVAILABLE;
    }
    s_events.emplace(id,
        FlowHandlerRecord{
            .name = name, .mod = &mod, .fn = reinterpret_cast<void*>(fn), .userData = userData});
    s_eventNames.emplace(name, id);
    s_warnedIds.erase(id);
    outId = id;
    Log.info("flow events registered: {}/{}", s_events.size(), kEventBudget);
    return MOD_OK;
}

namespace {

ModResult unregister_handler(std::unordered_map<uint16_t, FlowHandlerRecord>& table,
    std::unordered_map<std::string, uint16_t>& names, LoadedMod& mod, uint16_t id,
    bool freeEventId) {
    const auto it = table.find(id);
    if (it == table.end() || it->second.mod != &mod) {
        return MOD_INVALID_ARGUMENT;
    }
    names.erase(it->second.name);
    table.erase(it);
    if (freeEventId) {
        s_freeEventIds.push_back(id);
    }
    return MOD_OK;
}

}  // namespace

ModResult flow_unregister_query(LoadedMod& mod, uint16_t id) {
    return unregister_handler(s_queries, s_queryNames, mod, id, false);
}

ModResult flow_unregister_event(LoadedMod& mod, uint16_t id) {
    return unregister_handler(s_events, s_eventNames, mod, id, true);
}

ModResult flow_find_query(const char* name, uint16_t& outId) {
    const auto it = s_queryNames.find(name);
    if (it == s_queryNames.end()) {
        return MOD_UNAVAILABLE;
    }
    outId = it->second;
    return MOD_OK;
}

ModResult flow_find_event(const char* name, uint16_t& outId) {
    const auto it = s_eventNames.find(name);
    if (it == s_eventNames.end()) {
        return MOD_UNAVAILABLE;
    }
    outId = it->second;
    return MOD_OK;
}

ModResult flow_override_node(
    LoadedMod& mod, uint16_t group, uint16_t nodeIndex, const void* nodeBytes, uint64_t& outHandle) {
    const uint32_t key = static_cast<uint32_t>(group) << 16 | nodeIndex;
    auto& records = s_nodeOverrides[key];
    for (auto& record : records) {
        if (record.mod == &mod) {
            std::memcpy(record.node.data(), nodeBytes, record.node.size());
            record.seq = s_nextSeq++;
            outHandle = record.handle;
            return MOD_OK;
        }
    }
    auto& record = records.emplace_back();
    record.handle = s_nextHandle++;
    record.mod = &mod;
    record.seq = s_nextSeq++;
    std::memcpy(record.node.data(), nodeBytes, record.node.size());
    outHandle = record.handle;
    return MOD_OK;
}

ModResult flow_clear_node_override(LoadedMod& mod, uint64_t handle) {
    for (auto it = s_nodeOverrides.begin(); it != s_nodeOverrides.end(); ++it) {
        const auto removed = std::erase_if(it->second, [&](const auto& record) {
            return record.handle == handle && record.mod == &mod;
        });
        if (removed != 0) {
            if (it->second.empty()) {
                s_nodeOverrides.erase(it);
            }
            return MOD_OK;
        }
    }
    return MOD_INVALID_ARGUMENT;
}

void flow_remove_mod(LoadedMod& mod) {
    for (auto it = s_queries.begin(); it != s_queries.end();) {
        if (it->second.mod == &mod) {
            s_queryNames.erase(it->second.name);
            it = s_queries.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = s_events.begin(); it != s_events.end();) {
        if (it->second.mod == &mod) {
            s_eventNames.erase(it->second.name);
            s_freeEventIds.push_back(it->first);
            it = s_events.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = s_nodeOverrides.begin(); it != s_nodeOverrides.end();) {
        std::erase_if(it->second, [&](const auto& record) { return record.mod == &mod; });
        it = it->second.empty() ? s_nodeOverrides.erase(it) : std::next(it);
    }
}

}  // namespace dusk::mods
