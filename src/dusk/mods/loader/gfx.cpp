#include "loader.hpp"

#include "dusk/frame_interpolation.h"
#include "dusk/logging.h"
#include "dusk/mods/gfx_stages.hpp"

#include <aurora/gfx.hpp>

#include <mutex>
#include <vector>

// Gfx service plumbing. Unlike the other loader records (game-thread-only by convention), the
// slot table here is read by draw trampolines on Aurora's render worker thread, so every access
// goes through s_mutex. Trampolines carry the slot handle (never a pointer into the table),
// resolve it under the mutex, and invoke the mod callback unlocked.
//
// Lifecycle safety: gfx_remove_mod marks the mod's slots dead (no new trampoline invocation can
// resolve them), unregisters its Aurora draw types, then aurora::gfx::synchronize() drains the
// worker — including any callback currently on its stack — so the one-tick deferred dlclose can
// never unmap code the worker is about to run.

namespace dusk::mods {
namespace {

aurora::Module Log("dusk::mods::gfx");

enum class GfxSlotKind : u8 {
    DrawType,
    StageHook,
};

struct GfxSlot {
    GfxSlotKind kind = GfxSlotKind::DrawType;
    uint32_t generation = 1;
    bool alive = false;
    LoadedMod* owner = nullptr;
    // Stable for the mod's lifetime; safe to hand to the worker while the slot is alive.
    ModContext* ownerContext = nullptr;
    std::string ownerId;
    void* userData = nullptr;

    GfxDrawFn drawFn = nullptr;
    aurora::gfx::DrawTypeId auroraId = aurora::gfx::InvalidDrawType;

    GfxStageFn stageFn = nullptr;
    GfxStage stage = GFX_STAGE_WORLD_LATE;
};

struct WorkerFailure {
    std::string modId;
    std::string message;
};

std::mutex s_mutex;
std::vector<GfxSlot> s_slots;
std::vector<uint32_t> s_freeSlots;
std::vector<WorkerFailure> s_workerFailures;

// Game thread only: an offscreen pass opened through gfx_create_pass is currently active. Used
// to distinguish mod-owned offscreen passes from the game's own GXCreateFrameBuffer scopes.
bool s_modOffscreenOpen = false;

constexpr uint64_t make_handle(uint32_t index, uint32_t generation) {
    return static_cast<uint64_t>(generation) << 32 | index;
}

// Requires s_mutex held.
GfxSlot* resolve_slot(uint64_t handle, GfxSlotKind kind) {
    const auto index = static_cast<uint32_t>(handle & 0xFFFFFFFFu);
    const auto generation = static_cast<uint32_t>(handle >> 32);
    if (handle == 0 || index >= s_slots.size()) {
        return nullptr;
    }
    auto& slot = s_slots[index];
    if (!slot.alive || slot.generation != generation || slot.kind != kind) {
        return nullptr;
    }
    return &slot;
}

// Requires s_mutex held.
GfxSlot* resolve_owned_slot(LoadedMod& mod, uint64_t handle, GfxSlotKind kind) {
    auto* slot = resolve_slot(handle, kind);
    if (slot == nullptr || slot->owner != &mod) {
        return nullptr;
    }
    return slot;
}

// Requires s_mutex held. The returned index stays valid across reallocation; re-resolve by
// handle rather than holding the reference across anything that can allocate.
uint32_t alloc_slot() {
    if (!s_freeSlots.empty()) {
        const auto index = s_freeSlots.back();
        s_freeSlots.pop_back();
        return index;
    }
    const auto index = static_cast<uint32_t>(s_slots.size());
    s_slots.emplace_back();
    return index;
}

// Requires s_mutex held.
void free_slot(uint32_t index) {
    auto& slot = s_slots[index];
    slot.alive = false;
    ++slot.generation;
    slot.owner = nullptr;
    slot.ownerContext = nullptr;
    slot.ownerId.clear();
    slot.userData = nullptr;
    slot.drawFn = nullptr;
    slot.auroraId = aurora::gfx::InvalidDrawType;
    slot.stageFn = nullptr;
    s_freeSlots.push_back(index);
}

// Requires s_mutex held. Stops all further worker invocations for the mod immediately;
// Aurora-side unregistration and the worker drain happen in gfx_remove_mod (or the next tick's
// teardown after a worker failure).
void kill_mod_slots(LoadedMod* owner) {
    for (uint32_t i = 0; i < s_slots.size(); ++i) {
        if (s_slots[i].alive && s_slots[i].owner == owner) {
            free_slot(i);
        }
    }
}

// Render worker (GPU) thread. userdata is the slot handle.
void draw_trampoline(const aurora::gfx::DrawContext& ctx, const wgpu::RenderPassEncoder& pass,
    const void* payload, size_t payloadSize, void* userdata) {
    const auto handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(userdata));
    GfxDrawFn fn = nullptr;
    void* userData = nullptr;
    ModContext* modContext = nullptr;
    LoadedMod* owner = nullptr;
    std::string ownerId;
    {
        std::lock_guard lock{s_mutex};
        auto* slot = resolve_slot(handle, GfxSlotKind::DrawType);
        if (slot == nullptr) {
            // Unregistered between record and replay; drop silently.
            return;
        }
        fn = slot->drawFn;
        userData = slot->userData;
        modContext = slot->ownerContext;
        owner = slot->owner;
        ownerId = slot->ownerId;
    }

    GfxDrawContext drawContext{
        .struct_size = sizeof(GfxDrawContext),
        .device = ctx.device.Get(),
        .queue = ctx.queue.Get(),
        .pass = pass.Get(),
        .vertex_buffer = ctx.vertexBuffer.Get(),
        .index_buffer = ctx.indexBuffer.Get(),
        .uniform_buffer = ctx.uniformBuffer.Get(),
        .storage_buffer = ctx.storageBuffer.Get(),
        .color_format = static_cast<WGPUTextureFormat>(ctx.colorFormat),
        .depth_format = static_cast<WGPUTextureFormat>(ctx.depthFormat),
        .sample_count = ctx.sampleCount,
        .target_width = ctx.targetWidth,
        .target_height = ctx.targetHeight,
        .uses_reversed_z = aurora::gfx::uses_reversed_z(),
    };

    std::string failure;
    try {
        fn(modContext, &drawContext, payload, payloadSize, userData);
        return;
    } catch (const std::exception& e) {
        failure = fmt::format("exception in gfx draw callback: {}", e.what());
    } catch (...) {
        failure = "unknown exception in gfx draw callback";
    }

    // fail_mod is game-thread-only; queue the failure for gfx_drain_worker_failures and stop
    // invoking any of the mod's gfx callbacks right away.
    std::lock_guard lock{s_mutex};
    kill_mod_slots(owner);
    s_workerFailures.push_back(WorkerFailure{
        .modId = std::move(ownerId),
        .message = std::move(failure),
    });
}

}  // namespace

ModResult gfx_register_draw_type(
    LoadedMod& mod, const char* label, GfxDrawFn draw, void* userData, uint64_t& outHandle) {
    outHandle = 0;

    uint64_t handle = 0;
    {
        std::lock_guard lock{s_mutex};
        const auto index = alloc_slot();
        auto& slot = s_slots[index];
        slot.kind = GfxSlotKind::DrawType;
        slot.alive = true;
        slot.owner = &mod;
        slot.ownerContext = mod.context.get();
        slot.ownerId = mod.metadata.id;
        slot.userData = userData;
        slot.drawFn = draw;
        handle = make_handle(index, slot.generation);
    }

    // Register with Aurora outside the lock; the trampoline resolves the slot by handle.
    const auto auroraId = aurora::gfx::register_draw_type(aurora::gfx::DrawTypeDescriptor{
        .label = label,
        .draw = draw_trampoline,
        .userdata = reinterpret_cast<void*>(static_cast<uintptr_t>(handle)),
    });
    if (auroraId == aurora::gfx::InvalidDrawType) {
        std::lock_guard lock{s_mutex};
        if (resolve_owned_slot(mod, handle, GfxSlotKind::DrawType) != nullptr) {
            free_slot(static_cast<uint32_t>(handle & 0xFFFFFFFFu));
        }
        return MOD_ERROR;
    }

    {
        std::lock_guard lock{s_mutex};
        if (auto* slot = resolve_owned_slot(mod, handle, GfxSlotKind::DrawType)) {
            slot->auroraId = auroraId;
        }
    }
    outHandle = handle;
    return MOD_OK;
}

ModResult gfx_unregister_draw_type(LoadedMod& mod, uint64_t handle) {
    aurora::gfx::DrawTypeId auroraId = aurora::gfx::InvalidDrawType;
    {
        std::lock_guard lock{s_mutex};
        auto* slot = resolve_owned_slot(mod, handle, GfxSlotKind::DrawType);
        if (slot == nullptr) {
            return MOD_INVALID_ARGUMENT;
        }
        auroraId = slot->auroraId;
        free_slot(static_cast<uint32_t>(handle & 0xFFFFFFFFu));
    }
    aurora::gfx::unregister_draw_type(auroraId);
    return MOD_OK;
}

ModResult gfx_push_draw(LoadedMod& mod, uint64_t handle, const void* payload, size_t payloadSize) {
    aurora::gfx::DrawTypeId auroraId = aurora::gfx::InvalidDrawType;
    {
        std::lock_guard lock{s_mutex};
        auto* slot = resolve_owned_slot(mod, handle, GfxSlotKind::DrawType);
        if (slot == nullptr) {
            return MOD_INVALID_ARGUMENT;
        }
        auroraId = slot->auroraId;
    }
    if (!aurora::gfx::push_custom_draw(auroraId, payload, payloadSize)) {
        return MOD_UNAVAILABLE;
    }
    return MOD_OK;
}

ModResult gfx_push_stream(
    GfxStreamBuffer buffer, const void* data, size_t size, size_t alignment, GfxRange& outRange) {
    aurora::gfx::Range range;
    const auto* bytes = static_cast<const uint8_t*>(data);
    switch (buffer) {
    case GfxStreamBuffer::Verts:
        range = aurora::gfx::push_verts(bytes, size, alignment);
        break;
    case GfxStreamBuffer::Indices:
        range = aurora::gfx::push_indices(bytes, size, alignment);
        break;
    case GfxStreamBuffer::Uniform:
        range = aurora::gfx::push_uniform(bytes, size);
        break;
    case GfxStreamBuffer::Storage:
        range = aurora::gfx::push_storage(bytes, size);
        break;
    }
    if (range.size == 0) {
        // The aurora push_* guards return an empty range outside an active frame.
        return MOD_UNAVAILABLE;
    }
    outRange = GfxRange{.offset = range.offset, .size = range.size};
    return MOD_OK;
}

ModResult gfx_register_stage_hook(
    LoadedMod& mod, GfxStage stage, GfxStageFn callback, void* userData, uint64_t& outHandle) {
    outHandle = 0;
    std::lock_guard lock{s_mutex};
    const auto index = alloc_slot();
    auto& slot = s_slots[index];
    slot.kind = GfxSlotKind::StageHook;
    slot.alive = true;
    slot.owner = &mod;
    slot.ownerContext = mod.context.get();
    slot.ownerId = mod.metadata.id;
    slot.userData = userData;
    slot.stageFn = callback;
    slot.stage = stage;
    outHandle = make_handle(index, slot.generation);
    return MOD_OK;
}

ModResult gfx_unregister_stage_hook(LoadedMod& mod, uint64_t handle) {
    std::lock_guard lock{s_mutex};
    auto* slot = resolve_owned_slot(mod, handle, GfxSlotKind::StageHook);
    if (slot == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    free_slot(static_cast<uint32_t>(handle & 0xFFFFFFFFu));
    return MOD_OK;
}

ModResult gfx_resolve_pass(LoadedMod& mod, const GfxResolveDesc& desc, GfxResolvedTargets& out) {
    out = GfxResolvedTargets{.struct_size = sizeof(GfxResolvedTargets)};
    if (aurora::gfx::is_offscreen() && !s_modOffscreenOpen) {
        // The game is inside its own GXCreateFrameBuffer scope; resolving it out from under the
        // game would corrupt its rendering.
        Log.error("[{}] resolve_pass: the active offscreen pass belongs to the game",
            mod.metadata.id);
        return MOD_UNAVAILABLE;
    }
    const bool closesModOffscreen = s_modOffscreenOpen;

    aurora::gfx::ResolvedTargets resolved;
    if (!aurora::gfx::resolve_pass(
            aurora::gfx::ResolveDesc{.color = desc.color, .depth = desc.depth}, resolved))
    {
        return MOD_UNAVAILABLE;
    }
    if (closesModOffscreen) {
        s_modOffscreenOpen = false;
    }

    out.color = resolved.color.Get();
    out.depth = resolved.depth.Get();
    out.color_format = static_cast<WGPUTextureFormat>(resolved.colorFormat);
    out.width = resolved.width;
    out.height = resolved.height;
    return MOD_OK;
}

ModResult gfx_create_pass(LoadedMod& mod, uint32_t width, uint32_t height) {
    if (aurora::gfx::is_offscreen()) {
        Log.error("[{}] create_pass: an offscreen pass is already active (nesting is unsupported)",
            mod.metadata.id);
        return MOD_UNAVAILABLE;
    }
    if (!aurora::gfx::create_pass(width, height)) {
        return MOD_UNAVAILABLE;
    }
    s_modOffscreenOpen = true;
    return MOD_OK;
}

static_assert(GfxStageWorldLate == GFX_STAGE_WORLD_LATE &&
              GfxStageBeforeHud == GFX_STAGE_BEFORE_HUD && GfxStageAfterHud == GFX_STAGE_AFTER_HUD,
    "gfx_stages.hpp mirror values out of sync with GfxStage");

void gfx_run_stage(uint32_t stageValue, uint32_t windowIndex) {
    const auto stage = static_cast<GfxStage>(stageValue);
    struct StageEntry {
        uint64_t handle;
        GfxStageFn fn;
        void* userData;
        ModContext* context;
        LoadedMod* owner;
    };
    std::vector<StageEntry> entries;
    {
        std::lock_guard lock{s_mutex};
        for (uint32_t i = 0; i < s_slots.size(); ++i) {
            const auto& slot = s_slots[i];
            if (slot.alive && slot.kind == GfxSlotKind::StageHook && slot.stage == stage) {
                entries.push_back(StageEntry{
                    .handle = make_handle(i, slot.generation),
                    .fn = slot.stageFn,
                    .userData = slot.userData,
                    .context = slot.ownerContext,
                    .owner = slot.owner,
                });
            }
        }
    }
    if (entries.empty()) {
        return;
    }

    const GfxStageContext stageContext{
        .struct_size = sizeof(GfxStageContext),
        .stage = stage,
        .window_index = windowIndex,
        .interpolated_frame = frame_interp::is_enabled() && !frame_interp::is_sim_frame(),
    };

    for (const auto& entry : entries) {
        // A previous callback may have failed this mod (or unregistered the hook); re-check.
        {
            std::lock_guard lock{s_mutex};
            if (resolve_slot(entry.handle, GfxSlotKind::StageHook) == nullptr) {
                continue;
            }
        }
        if (!entry.owner->active) {
            continue;
        }

        const bool wasOffscreen = aurora::gfx::is_offscreen();
        try {
            entry.fn(entry.context, &stageContext, entry.userData);
        } catch (const std::exception& e) {
            fail_mod(*entry.owner, MOD_ERROR,
                fmt::format("exception in gfx stage callback: {}", e.what()));
        } catch (...) {
            fail_mod(*entry.owner, MOD_ERROR, "unknown exception in gfx stage callback");
        }

        // Balance guardrail: a callback that leaves an offscreen pass open would route the
        // game's subsequent GX draws into its offscreen target. Force-restore and fail the mod.
        if (aurora::gfx::is_offscreen() != wasOffscreen) {
            aurora::gfx::ResolvedTargets discarded;
            aurora::gfx::resolve_pass(
                aurora::gfx::ResolveDesc{.color = false, .depth = false}, discarded);
            s_modOffscreenOpen = false;
            fail_mod(*entry.owner, MOD_ERROR,
                "gfx stage callback returned with its offscreen pass still open");
        }
    }
}

void gfx_drain_worker_failures() {
    std::vector<WorkerFailure> failures;
    {
        std::lock_guard lock{s_mutex};
        failures.swap(s_workerFailures);
    }
    for (auto& failure : failures) {
        for (auto& mod : ModLoader::instance().mods()) {
            if (mod.metadata.id == failure.modId && mod.active) {
                fail_mod(mod, MOD_ERROR, failure.message);
                break;
            }
        }
    }
}

void gfx_remove_mod(LoadedMod& mod) {
    std::vector<aurora::gfx::DrawTypeId> auroraIds;
    {
        std::lock_guard lock{s_mutex};
        for (uint32_t i = 0; i < s_slots.size(); ++i) {
            auto& slot = s_slots[i];
            if (!slot.alive || slot.owner != &mod) {
                continue;
            }
            if (slot.kind == GfxSlotKind::DrawType &&
                slot.auroraId != aurora::gfx::InvalidDrawType)
            {
                auroraIds.push_back(slot.auroraId);
            }
            free_slot(i);
        }
    }
    if (auroraIds.empty()) {
        return;
    }
    for (const auto id : auroraIds) {
        aurora::gfx::unregister_draw_type(id);
    }
    // Draw callbacks run on the render worker; drain it so no callback from this mod is on the
    // worker's stack (or queued) when the dylib is retired for dlclose.
    aurora::gfx::synchronize();
}

}  // namespace dusk::mods
