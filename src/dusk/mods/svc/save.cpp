#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include <string_view>

namespace dusk::mods::svc {
namespace {

constexpr size_t kMaxBlobNameLength = 256;

bool is_valid_blob_name(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const std::string_view view{name};
    return !view.empty() && view.size() <= kMaxBlobNameLength;
}

ModResult save_set_blob_(ModContext* context, const char* name, const void* data, size_t size) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_blob_name(name) || (data == nullptr && size != 0) ||
        size > SAVE_BLOB_BUDGET_BYTES)
    {
        return MOD_INVALID_ARGUMENT;
    }
    return save_set_blob(*mod, name, data, size);
}

ModResult save_get_blob_(ModContext* context, const char* name, void* buf, size_t* inoutSize) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_blob_name(name) || inoutSize == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_get_blob(*mod, name, buf, *inoutSize);
}

ModResult save_delete_blob_(ModContext* context, const char* name) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_blob_name(name)) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_delete_blob(*mod, name);
}

ModResult save_observe_saves_(ModContext* context, SaveEventFn onNewSave, SaveEventFn onLoaded,
    SaveEventFn onWritten, void* userData, SaveObserverHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || (onNewSave == nullptr && onLoaded == nullptr && onWritten == nullptr)) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = save_observe(*mod, onNewSave, onLoaded, onWritten, userData, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult save_unobserve_saves_(ModContext* context, SaveObserverHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_unobserve(*mod, handle);
}

ModResult save_peek_blob_(
    ModContext* context, uint32_t slot, const char* name, void* buf, size_t* inoutSize) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_blob_name(name) || inoutSize == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_peek_blob(*mod, slot, name, buf, *inoutSize);
}

ModResult save_register_new_save_gate_(
    ModContext* context, SaveNewSaveGateFn gate, void* userData, SaveGateHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || gate == nullptr || outHandle == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_register_gate(*mod, gate, userData, *outHandle);
}

ModResult save_unregister_new_save_gate_(ModContext* context, SaveGateHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_unregister_gate(*mod, handle);
}

ModResult save_complete_new_save_gate_(ModContext* context, bool proceed) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_complete_gate(*mod, proceed);
}

ModResult save_register_slot_info_provider_(
    ModContext* context, SaveSlotInfoFn provider, void* userData, SaveSlotInfoHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || provider == nullptr || outHandle == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_register_slot_info(*mod, provider, userData, *outHandle);
}

ModResult save_unregister_slot_info_provider_(ModContext* context, SaveSlotInfoHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return save_unregister_slot_info(*mod, handle);
}

constexpr SaveService s_saveService{
    .header = SERVICE_HEADER(SaveService, SAVE_SERVICE_MAJOR, SAVE_SERVICE_MINOR),
    .set_blob = save_set_blob_,
    .get_blob = save_get_blob_,
    .delete_blob = save_delete_blob_,
    .observe_saves = save_observe_saves_,
    .unobserve_saves = save_unobserve_saves_,
    .peek_blob = save_peek_blob_,
    .register_new_save_gate = save_register_new_save_gate_,
    .unregister_new_save_gate = save_unregister_new_save_gate_,
    .complete_new_save_gate = save_complete_new_save_gate_,
    .register_slot_info_provider = save_register_slot_info_provider_,
    .unregister_slot_info_provider = save_unregister_slot_info_provider_,
};

}  // namespace

const SaveService& save_service() {
    return s_saveService;
}
}  // namespace dusk::mods::svc
