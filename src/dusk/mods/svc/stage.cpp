#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include <string_view>

namespace dusk::mods::svc {
namespace {

constexpr size_t kMaxStageNameLength = 8;

bool is_valid_stage_name(const char* stage) {
    if (stage == nullptr) {
        return false;
    }
    const std::string_view view{stage};
    return !view.empty() && view.size() <= kMaxStageNameLength;
}

bool is_valid_record_size(size_t size) {
    return size == STAGE_ACTOR_RECORD_SIZE || size == STAGE_TGSC_RECORD_SIZE;
}

ModResult stage_patch_actor_(ModContext* context, const char* stage, uint8_t room, uint8_t layer,
    uint32_t recordCrc, const void* record, size_t recordSize, StageActorHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_stage_name(stage) || record == nullptr ||
        !is_valid_record_size(recordSize))
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result =
        stage_patch_actor(*mod, stage, room, layer, recordCrc, record, recordSize, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult stage_delete_actor_(ModContext* context, const char* stage, uint8_t room, uint8_t layer,
    uint32_t recordCrc, StageActorHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || !is_valid_stage_name(stage)) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = stage_delete_actor(*mod, stage, room, layer, recordCrc, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult stage_add_actor_(ModContext* context, const char* stage, uint8_t room, uint8_t layer,
    const void* record, size_t recordSize, StageActorHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    // Additions spawn with a room's actor load; the stage-file pseudo-room is not spawnable.
    if (mod == nullptr || !is_valid_stage_name(stage) || room == STAGE_ROOM_STAGE_FILE ||
        record == nullptr || !is_valid_record_size(recordSize))
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = stage_add_actor(*mod, stage, room, layer, record, recordSize, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult stage_remove_actor_edit_(ModContext* context, StageActorHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return stage_remove_actor_edit(*mod, handle);
}

ModResult stage_register_layer_resolver_(ModContext* context, StageLayerResolveFn fn,
    void* userData, StageLayerHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || fn == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = stage_register_layer_resolver(*mod, fn, userData, handle);
    if (outHandle != nullptr) {
        *outHandle = handle;
    }
    return result;
}

ModResult stage_unregister_layer_resolver_(ModContext* context, StageLayerHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return stage_unregister_layer_resolver(*mod, handle);
}

constexpr StageService s_stageService{
    .header = SERVICE_HEADER(StageService, STAGE_SERVICE_MAJOR, STAGE_SERVICE_MINOR),
    .patch_actor = stage_patch_actor_,
    .delete_actor = stage_delete_actor_,
    .add_actor = stage_add_actor_,
    .remove_actor_edit = stage_remove_actor_edit_,
    .register_layer_resolver = stage_register_layer_resolver_,
    .unregister_layer_resolver = stage_unregister_layer_resolver_,
};

}  // namespace

const StageService& stage_service() {
    return s_stageService;
}
}  // namespace dusk::mods::svc
