#include "registry.hpp"

#include "dusk/logging.h"
#include "dusk/mods/loader/loader.hpp"

#include <webgpu/webgpu.h>

#include <aurora/gfx.hpp>

namespace dusk::mods::svc {
namespace {

ModResult gfx_get_device_info(ModContext* context, GfxDeviceInfo* outInfo) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || outInfo == nullptr || outInfo->struct_size < sizeof(GfxDeviceInfo)) {
        return MOD_INVALID_ARGUMENT;
    }
    outInfo->device = aurora::gfx::device().Get();
    outInfo->queue = aurora::gfx::queue().Get();
    outInfo->color_format = static_cast<WGPUTextureFormat>(aurora::gfx::color_format());
    outInfo->depth_format = static_cast<WGPUTextureFormat>(aurora::gfx::depth_format());
    outInfo->sample_count = aurora::gfx::sample_count();
    outInfo->uses_reversed_z = aurora::gfx::uses_reversed_z();
    return MOD_OK;
}

void* gfx_get_proc_address(ModContext* context, const char* name) {
    if (mod_from_context(context) == nullptr || name == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void*>(wgpuGetProcAddress(WGPUStringView{name, WGPU_STRLEN}));
}

ModResult gfx_register_draw_type_impl(
    ModContext* context, const GfxDrawTypeDesc* desc, GfxDrawTypeHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(GfxDrawTypeDesc) ||
        desc->draw == nullptr || outHandle == nullptr)
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result =
        gfx_register_draw_type(*mod, desc->label, desc->draw, desc->user_data, handle);
    if (result != MOD_OK) {
        return result;
    }
    *outHandle = handle;
    return MOD_OK;
}

ModResult gfx_unregister_draw_type_impl(ModContext* context, GfxDrawTypeHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = gfx_unregister_draw_type(*mod, handle);
    if (result != MOD_OK) {
        DuskLog.error("[{}] gfx: stale or invalid draw type handle {:#x}", mod->metadata.id,
            handle);
    }
    return result;
}

ModResult gfx_push_draw_impl(
    ModContext* context, GfxDrawTypeHandle handle, const void* payload, size_t payloadSize) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0 || payloadSize > GFX_INLINE_DRAW_PAYLOAD_SIZE ||
        (payloadSize > 0 && payload == nullptr))
    {
        return MOD_INVALID_ARGUMENT;
    }
    return gfx_push_draw(*mod, handle, payload, payloadSize);
}

ModResult gfx_push_stream_impl(ModContext* context, GfxStreamBuffer buffer, const void* data,
    size_t size, size_t alignment, GfxRange* outRange) {
    if (outRange != nullptr) {
        *outRange = GfxRange{0, 0};
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || data == nullptr || size == 0 || outRange == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return gfx_push_stream(buffer, data, size, alignment, *outRange);
}

ModResult gfx_push_verts_impl(
    ModContext* context, const void* data, size_t size, size_t alignment, GfxRange* outRange) {
    return gfx_push_stream_impl(context, GfxStreamBuffer::Verts, data, size, alignment, outRange);
}

ModResult gfx_push_indices_impl(
    ModContext* context, const void* data, size_t size, size_t alignment, GfxRange* outRange) {
    return gfx_push_stream_impl(
        context, GfxStreamBuffer::Indices, data, size, alignment, outRange);
}

ModResult gfx_push_uniform_impl(
    ModContext* context, const void* data, size_t size, GfxRange* outRange) {
    return gfx_push_stream_impl(context, GfxStreamBuffer::Uniform, data, size, 0, outRange);
}

ModResult gfx_push_storage_impl(
    ModContext* context, const void* data, size_t size, GfxRange* outRange) {
    return gfx_push_stream_impl(context, GfxStreamBuffer::Storage, data, size, 0, outRange);
}

ModResult gfx_register_stage_hook_impl(ModContext* context, GfxStage stage,
    const GfxStageHookDesc* desc, GfxStageHookHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(GfxStageHookDesc) ||
        desc->callback == nullptr || outHandle == nullptr ||
        (stage != GFX_STAGE_WORLD_LATE && stage != GFX_STAGE_BEFORE_HUD &&
            stage != GFX_STAGE_AFTER_HUD && stage != GFX_STAGE_WORLD_BEFORE_TERRAIN))
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result =
        gfx_register_stage_hook(*mod, stage, desc->callback, desc->user_data, handle);
    if (result != MOD_OK) {
        return result;
    }
    *outHandle = handle;
    return MOD_OK;
}

ModResult gfx_unregister_stage_hook_impl(ModContext* context, GfxStageHookHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = gfx_unregister_stage_hook(*mod, handle);
    if (result != MOD_OK) {
        DuskLog.error("[{}] gfx: stale or invalid stage hook handle {:#x}", mod->metadata.id,
            handle);
    }
    return result;
}

ModResult gfx_resolve_pass_impl(
    ModContext* context, const GfxResolveDesc* desc, GfxResolvedTargets* outTargets) {
    if (outTargets != nullptr && outTargets->struct_size >= sizeof(GfxResolvedTargets)) {
        *outTargets = GfxResolvedTargets{.struct_size = sizeof(GfxResolvedTargets)};
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(GfxResolveDesc) ||
        outTargets == nullptr || outTargets->struct_size < sizeof(GfxResolvedTargets) ||
        (!desc->color && !desc->depth))
    {
        return MOD_INVALID_ARGUMENT;
    }
    return gfx_resolve_pass(*mod, *desc, *outTargets);
}

ModResult gfx_create_pass_impl(ModContext* context, uint32_t width, uint32_t height) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || width == 0 || height == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return gfx_create_pass(*mod, width, height);
}

ModResult gfx_register_compute_type_impl(
    ModContext* context, const GfxComputeTypeDesc* desc, GfxComputeTypeHandle* outHandle) {
    if (outHandle != nullptr) {
        *outHandle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(GfxComputeTypeDesc) ||
        desc->callback == nullptr || outHandle == nullptr)
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result =
        gfx_register_compute_type(*mod, desc->label, desc->callback, desc->user_data, handle);
    if (result != MOD_OK) {
        return result;
    }
    *outHandle = handle;
    return MOD_OK;
}

ModResult gfx_unregister_compute_type_impl(ModContext* context, GfxComputeTypeHandle handle) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    const auto result = gfx_unregister_compute_type(*mod, handle);
    if (result != MOD_OK) {
        DuskLog.error("[{}] gfx: stale or invalid compute type handle {:#x}", mod->metadata.id,
            handle);
    }
    return result;
}

ModResult gfx_push_compute_impl(
    ModContext* context, GfxComputeTypeHandle handle, const void* payload, size_t payloadSize) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || handle == 0 || payloadSize > GFX_INLINE_DRAW_PAYLOAD_SIZE ||
        (payloadSize > 0 && payload == nullptr))
    {
        return MOD_INVALID_ARGUMENT;
    }
    return gfx_push_compute(*mod, handle, payload, payloadSize);
}

constexpr GfxService s_gfxService{
    .header = SERVICE_HEADER(GfxService, GFX_SERVICE_MAJOR, GFX_SERVICE_MINOR),
    .get_device_info = gfx_get_device_info,
    .get_proc_address = gfx_get_proc_address,
    .register_draw_type = gfx_register_draw_type_impl,
    .unregister_draw_type = gfx_unregister_draw_type_impl,
    .push_draw = gfx_push_draw_impl,
    .push_verts = gfx_push_verts_impl,
    .push_indices = gfx_push_indices_impl,
    .push_uniform = gfx_push_uniform_impl,
    .push_storage = gfx_push_storage_impl,
    .register_stage_hook = gfx_register_stage_hook_impl,
    .unregister_stage_hook = gfx_unregister_stage_hook_impl,
    .resolve_pass = gfx_resolve_pass_impl,
    .create_pass = gfx_create_pass_impl,
    .register_compute_type = gfx_register_compute_type_impl,
    .unregister_compute_type = gfx_unregister_compute_type_impl,
    .push_compute = gfx_push_compute_impl,
};

}  // namespace

const GfxService& gfx_service() {
    return s_gfxService;
}

}  // namespace dusk::mods::svc
