#pragma once

#include "mods/api.h"

#include <webgpu/webgpu.h>

#define GFX_SERVICE_ID "dev.twilitrealm.dusklight.gfx"
/*
 * The major version also pins the WebGPU C ABI: it is bumped whenever the
 * host's Dawn update changes the webgpu.h ABI (expected to be rare).
 */
#define GFX_SERVICE_MAJOR 1u
/* Minor 1: compute/encoder tasks (register_compute_type, unregister_compute_type,
 * push_compute).
 * Minor 2: GFX_STAGE_SCENE_AFTER_OPAQUE stage hook. */
#define GFX_SERVICE_MINOR 2u

// Maximum payload size for push_draw
#define GFX_INLINE_DRAW_PAYLOAD_SIZE 128u

/*
 * Threading contract:
 *
 * - Every service function must be called on the game thread.
 * - GfxStageFn callbacks run on the game thread during frame recording;
 *   push_draw, push_* and the pass primitives are valid there (and anywhere
 *   else GX commands are being recorded, e.g. hooked game draw functions).
 * - GfxDrawFn callbacks run on the render worker (GPU) thread while the frame is
 *   encoded. They may use the handles in their context struct and raw wgpu*
 *   calls, and nothing else: no other service may be called from them.
 * - GfxComputeFn callbacks follow the same rules as GfxDrawFn (render worker,
 *   context handles + raw wgpu* only, valid only during the call).
 * - All WGPU handles provided by this service are borrowed. Handles in a
 *   GfxDrawContext are valid only for the duration of the callback; views in
 *   GfxResolvedTargets are valid for the current frame only. Add-ref before
 *   stashing anything (and prefer not stashing).
 *
 * GPU objects a mod creates through raw wgpu calls are its own responsibility;
 * release them in mod_shutdown. The device outlives all mods.
 */

/* Generational handles; 0 is never valid. */
typedef uint64_t GfxDrawTypeHandle;
typedef uint64_t GfxStageHookHandle;
typedef uint64_t GfxComputeTypeHandle; /* minor 1 */

/* A suballocation in one of the shared per-frame streaming buffers. */
typedef struct GfxRange {
    uint32_t offset;
    uint32_t size;
} GfxRange;

/*
 * Describes the device and the scene pass configuration. Valid from
 * mod_initialize onward and stable for the session (MSAA/format changes
 * require a restart), so pipelines for scene-pass draws can be built at init.
 * Offscreen passes from create_pass are always single-sample; key pipeline
 * variants on GfxDrawContext when drawing into those.
 */
typedef struct GfxDeviceInfo {
    uint32_t struct_size;
    WGPUDevice device;              /* borrowed */
    WGPUQueue queue;                /* borrowed */
    WGPUTextureFormat color_format; /* scene color target format */
    WGPUTextureFormat depth_format; /* scene depth target format */
    uint32_t sample_count;          /* scene (EFB) pass MSAA sample count */
    bool uses_reversed_z;           /* 1.0 = near when true */
} GfxDeviceInfo;

#define GFX_DEVICE_INFO_INIT                                                                       \
    {sizeof(GfxDeviceInfo), NULL, NULL, WGPUTextureFormat_Undefined, WGPUTextureFormat_Undefined,  \
        1u, false}

/*
 * Passed to GfxDrawFn on the render worker thread; valid only during the call.
 * The pass's pipeline/bind group/viewport/scissor state is restored by the
 * host after the callback returns, so leave-as-you-like.
 */
typedef struct GfxDrawContext {
    uint32_t struct_size;
    WGPUDevice device;
    WGPUQueue queue;
    WGPURenderPassEncoder pass; /* the live pass the draw was recorded into */
    /* Shared streaming buffers; address contents via GfxRange from push_*.
     * These are the same buffer objects for the entire session, so bind groups
     * referencing them may be cached across frames. Per-frame variance is only
     * the GfxRange offset (avoidable with a dynamic-offset binding on an
     * explicit bind group layout). */
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;
    WGPUBuffer uniform_buffer;
    WGPUBuffer storage_buffer;
    /* Formats/sample count of the containing pass. Offscreen passes created by
     * create_pass are always single-sample; the EFB pass uses the configured
     * MSAA count. Pipelines must match the pass they draw into — including
     * declaring the pass's depth format even if they never touch depth. */
    WGPUTextureFormat color_format;
    WGPUTextureFormat depth_format;
    uint32_t sample_count;
    uint32_t target_width;
    uint32_t target_height;
    bool uses_reversed_z;
} GfxDrawContext;

typedef void (*GfxDrawFn)(ModContext* ctx, const GfxDrawContext* draw_ctx, const void* payload,
    size_t payload_size, void* user_data);

typedef struct GfxDrawTypeDesc {
    uint32_t struct_size;
    const char* label; /* optional debug label */
    GfxDrawFn draw;    /* required; called from GPU thread */
    void* user_data;
} GfxDrawTypeDesc;

#define GFX_DRAW_TYPE_DESC_INIT {sizeof(GfxDrawTypeDesc), NULL, NULL, NULL}

typedef enum GfxStage {
    /* Values are part of the mod ABI; keep them explicit. */
    /* After the camera/projection/light setup for a world camera window, before
     * terrain/background opaque scene lists are consumed. May fire more than
     * once per frame (once per camera window). */
    GFX_STAGE_SCENE_BEGIN = 3,
    /* After terrain/background/shadow lists, before object opaque and
     * translucent lists. May fire more than once per frame (once per camera
     * window). */
    GFX_STAGE_SCENE_AFTER_TERRAIN = 0,
    /* After sky/terrain/object opaque lists, before translucent lists and fog
     * priority particle overlays. May fire more than once per frame (once per
     * camera window). (Minor 2.) */
    GFX_STAGE_SCENE_AFTER_OPAQUE = 4,
    /* After 3D and wipe rendering, before any 2D/HUD draw lists. Fires once
     * per rendered frame. */
    GFX_STAGE_FRAME_BEFORE_HUD = 1,
    /* After all 2D/HUD draw lists, before the frame ends. Fires once per
     * rendered frame. */
    GFX_STAGE_FRAME_AFTER_HUD = 2,
} GfxStage;

typedef struct GfxStageContext {
    uint32_t struct_size;
    GfxStage stage;
    uint32_t window_index; /* camera window for scene stages; 0 otherwise */
    /* True when this rendered frame is a frame-interpolation presentation
     * frame. Stage callbacks fire on every rendered frame either way; read
     * interpolated state (camera etc.) fresh in each callback. */
    bool interpolated_frame;
    /* World-camera stages only: view_class* and view_port_class* for the
     * camera being painted. NULL for non-world stages. */
    const void* game_view;
    const void* game_viewport;
} GfxStageContext;

/*
 * Game thread, during frame recording. push_draw, the push_ streaming helpers,
 * resolve_pass and create_pass are valid inside. An offscreen pass opened with
 * create_pass must be closed with resolve_pass before returning.
 */
typedef void (*GfxStageFn)(ModContext* ctx, const GfxStageContext* stage_ctx, void* user_data);

typedef struct GfxStageHookDesc {
    uint32_t struct_size;
    GfxStageFn callback; /* required */
    void* user_data;
} GfxStageHookDesc;

#define GFX_STAGE_HOOK_DESC_INIT {sizeof(GfxStageHookDesc), NULL, NULL}

typedef struct GfxResolveDesc {
    uint32_t struct_size;
    bool color;
    bool depth;
} GfxResolveDesc;

#define GFX_RESOLVE_DESC_INIT {sizeof(GfxResolveDesc), true, false}

typedef struct GfxResolvedTargets {
    uint32_t struct_size;
    /* Borrowed views, valid for the current frame only. NULL if not requested
     * (depth is also NULL when unsupported by the device; check it). The views
     * come from a small per-frame pool: the same handles repeat across frames
     * until the resolution changes, so caches (e.g. of bind groups sampling
     * them) may key on the view handle — but must expect several distinct
     * handles, not one. */
    WGPUTextureView color; /* single-sample snapshot in color_format */
    WGPUTextureView depth; /* single-sample raw depth snapshot, R32Float */
    WGPUTextureFormat color_format;
    uint32_t width;
    uint32_t height;
} GfxResolvedTargets;

#define GFX_RESOLVED_TARGETS_INIT                                                                  \
    {sizeof(GfxResolvedTargets), NULL, NULL, WGPUTextureFormat_Undefined, 0u, 0u}

/*
 * Passed to GfxComputeFn on the render worker thread; valid only during the
 * call. (Minor 1.)
 */
typedef struct GfxComputeContext {
    uint32_t struct_size;
    WGPUDevice device;
    WGPUQueue queue;
    /* The frame's command encoder, positioned between two scene render passes.
     * Begin/end compute passes and record copies on it freely — a single
     * compute pass can chain dispatches (WebGPU synchronizes between them, so
     * a storage texture written by one dispatch is readable by the next in the
     * same pass). Leave no pass open when returning, and never Finish or
     * Release the encoder. */
    WGPUCommandEncoder encoder;
    /* Shared streaming buffers (see GfxDrawContext). Data pushed with the
     * push_* helpers before the task was recorded is GPU-visible inside it. */
    WGPUBuffer vertex_buffer;
    WGPUBuffer index_buffer;
    WGPUBuffer uniform_buffer;
    WGPUBuffer storage_buffer;
} GfxComputeContext;

typedef void (*GfxComputeFn)(ModContext* ctx, const GfxComputeContext* compute_ctx,
    const void* payload, size_t payload_size, void* user_data);

typedef struct GfxComputeTypeDesc {
    uint32_t struct_size;
    const char* label;     /* optional debug label */
    GfxComputeFn callback; /* required; called from GPU thread */
    void* user_data;
} GfxComputeTypeDesc;

#define GFX_COMPUTE_TYPE_DESC_INIT {sizeof(GfxComputeTypeDesc), NULL, NULL, NULL}

/*
 * Raw WebGPU access integrated into the frame, plus hooks at named points of
 * the render process.
 *
 * The intended post-process shape: register a stage hook (e.g.
 * GFX_STAGE_FRAME_AFTER_HUD) and a draw type once; per frame, from the stage
 * callback, resolve_pass the scene into snapshot textures, build a bind group
 * sampling them, push_uniform any parameters, and push_draw a fullscreen
 * triangle that writes back over the frame. Everything downstream (UI
 * compositing included) sees the processed frame. Multi-resolution chains use
 * create_pass for intermediate targets.
 */
typedef struct GfxService {
    ServiceHeader header;

    ModResult (*get_device_info)(ModContext* ctx, GfxDeviceInfo* out_info);

    /* Resolve a wgpu* entry point by name (wgpuGetProcAddress). Escape hatch
     * for bindings and platforms where linking against the host's exported
     * wgpu symbols is impractical. NULL if unknown. */
    void* (*get_proc_address)(ModContext* ctx, const char* name);

    ModResult (*register_draw_type)(
        ModContext* ctx, const GfxDrawTypeDesc* desc, GfxDrawTypeHandle* out_handle);
    ModResult (*unregister_draw_type)(ModContext* ctx, GfxDrawTypeHandle handle);

    /* Record an inline custom draw into the currently open pass at the current
     * position in the command stream. The payload (<= GFX_INLINE_DRAW_PAYLOAD_SIZE)
     * is copied as raw bytes: it must be trivially copyable (no RAII members),
     * and any pointers inside it must stay valid until the frame's encode
     * completes (frame-pooled objects like resolved views qualify, stack
     * locals do not). MOD_UNAVAILABLE outside an active pass: call from stage
     * callbacks or GX-record-time hooks. */
    ModResult (*push_draw)(
        ModContext* ctx, GfxDrawTypeHandle handle, const void* payload, size_t payload_size);

    /* Stream transient data into the shared per-frame buffers. Returned ranges
     * are valid for the current frame only. MOD_UNAVAILABLE outside an active
     * recording frame. */
    ModResult (*push_verts)(
        ModContext* ctx, const void* data, size_t size, size_t alignment, GfxRange* out_range);
    ModResult (*push_indices)(
        ModContext* ctx, const void* data, size_t size, size_t alignment, GfxRange* out_range);
    ModResult (*push_uniform)(ModContext* ctx, const void* data, size_t size, GfxRange* out_range);
    ModResult (*push_storage)(ModContext* ctx, const void* data, size_t size, GfxRange* out_range);

    ModResult (*register_stage_hook)(ModContext* ctx, GfxStage stage, const GfxStageHookDesc* desc,
        GfxStageHookHandle* out_handle);
    ModResult (*unregister_stage_hook)(ModContext* ctx, GfxStageHookHandle handle);

    /* Snapshot the current pass targets into pooled textures, then: on the
     * scene (EFB) pass, continue on a fresh pass that loads the existing
     * contents; in an offscreen pass opened by create_pass, end it and restore
     * the scene pass. MOD_UNAVAILABLE outside an active pass or when the only
     * active pass belongs to the game (an open GXCreateFrameBuffer scope). */
    ModResult (*resolve_pass)(
        ModContext* ctx, const GfxResolveDesc* desc, GfxResolvedTargets* out_targets);

    /* Open a cleared single-sample offscreen pass at (width, height); draws
     * target it until resolve_pass restores the scene pass. Must be balanced
     * with resolve_pass before the enclosing stage callback returns. Nesting
     * is unsupported: MOD_UNAVAILABLE outside an active pass or while any
     * offscreen pass is open. */
    ModResult (*create_pass)(ModContext* ctx, uint32_t width, uint32_t height);

    /* ---- minor 1 ---- */

    ModResult (*register_compute_type)(
        ModContext* ctx, const GfxComputeTypeDesc* desc, GfxComputeTypeHandle* out_handle);
    ModResult (*unregister_compute_type)(ModContext* ctx, GfxComputeTypeHandle handle);

    /* Record a compute/encoder task at the current position in the frame: the
     * scene pass is split here and the callback runs on the frame's command
     * encoder between the two halves — dispatches see everything drawn (and
     * resolved) before this call, and draws recorded after it see the task's
     * output. The typical shape: resolve_pass for input snapshots, push_compute
     * for the compute chain, push_draw to composite the result. Payload
     * semantics match push_draw. MOD_UNAVAILABLE outside an active pass or
     * while any offscreen pass is open. */
    ModResult (*push_compute)(
        ModContext* ctx, GfxComputeTypeHandle handle, const void* payload, size_t payload_size);
} GfxService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<GfxService> {
    static constexpr const char* id = GFX_SERVICE_ID;
    static constexpr uint16_t major_version = GFX_SERVICE_MAJOR;
};
#endif
