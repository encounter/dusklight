#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include <string_view>

namespace dusk::mods::svc {
namespace {

constexpr size_t kMaxFlowNameLength = 256;

bool is_valid_flow_name(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const std::string_view view{name};
    return !view.empty() && view.size() <= kMaxFlowNameLength;
}

ModResult flow_register_query_(
    ModContext* context, const char* name, FlowQueryFn fn, void* userData, uint16_t* outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_flow_name(name) || fn == nullptr || outId == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return flow_register_query(*mod, name, fn, userData, *outId);
}

ModResult flow_unregister_query_(ModContext* context, uint16_t id) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return flow_unregister_query(*mod, id);
}

ModResult flow_register_event_(
    ModContext* context, const char* name, FlowEventFn fn, void* userData, uint8_t* outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_flow_name(name) || fn == nullptr || outId == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint16_t id = 0;
    const auto result = flow_register_event(*mod, name, fn, userData, id);
    *outId = static_cast<uint8_t>(id);
    return result;
}

ModResult flow_unregister_event_(ModContext* context, uint8_t id) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return flow_unregister_event(*mod, id);
}

ModResult flow_find_query_(ModContext* context, const char* name, uint16_t* outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    if (mod_from_context(context) == nullptr || !is_valid_flow_name(name) || outId == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return flow_find_query(name, *outId);
}

ModResult flow_find_event_(ModContext* context, const char* name, uint8_t* outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    if (mod_from_context(context) == nullptr || !is_valid_flow_name(name) || outId == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint16_t id = 0;
    const auto result = flow_find_event(name, id);
    *outId = static_cast<uint8_t>(id);
    return result;
}

ModResult flow_override_node_(ModContext* context, uint16_t group, uint16_t nodeIndex,
    const void* nodeBytes, FlowNodeHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || nodeBytes == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = flow_override_node(*mod, group, nodeIndex, nodeBytes, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult flow_clear_node_override_(ModContext* context, FlowNodeHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return flow_clear_node_override(*mod, handle);
}

constexpr FlowService s_flowService{
    .header = SERVICE_HEADER(FlowService, FLOW_SERVICE_MAJOR, FLOW_SERVICE_MINOR),
    .register_query = flow_register_query_,
    .unregister_query = flow_unregister_query_,
    .register_event = flow_register_event_,
    .unregister_event = flow_unregister_event_,
    .find_query = flow_find_query_,
    .find_event = flow_find_event_,
    .override_flow_node = flow_override_node_,
    .clear_flow_node_override = flow_clear_node_override_,
};

}  // namespace

const FlowService& flow_service() {
    return s_flowService;
}
}  // namespace dusk::mods::svc
