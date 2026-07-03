// Dynamic shadows example mod.
// The most invasive gfx-service consumer yet: at WORLD_LATE it re-executes the game's own
// opaque draw lists into an offscreen pass with a light-space projection — producing a real
// shadow map of the live scene, animation and all — then at BEFORE_HUD it composites deferred
// shadows over the world (scene depth + CameraService unproject + PCF against the map).
//
// Why this works: by draw time every J3D position matrix is camera-view-space (the view is
// baked in at calc time or concatenated at load time), so the single seam shared by every
// draw is the projection multiply. GXSetProjectionFull loads
//     P_shadow = lightOrtho * lightView * inverse(cameraView)
// which cancels the camera view and lands every vertex in the light's clip space, with zero
// changes to any matrix-load path — skinned, indexed, billboard and BG geometry all included,
// and frame interpolation stays consistent because the re-executed lists resolve the same
// interpolated matrices the visible pass does. Draw lists are not consumed by execution (the
// game's own dDlst_shadowReal_c re-draws packets the same way), so running them twice per
// frame is safe.
//
// The optional contact-shadow raymarch in the composite is reimplemented from Panos Karabelas'
// screen-space shadows (MIT, via Spartan Engine); see res/shadow.wgsl.

#include "global.h"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_kankyo_rain.h"
#include "dolphin/gx/GXAurora.h"
#include "dolphin/gx/GXGet.h"
#include "dolphin/gx/GXTransform.h"
#include "JSystem/J3DU/J3DUClipper.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/camera.h"
#include "mods/svc/config.h"
#include "mods/svc/gfx.h"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"
#include "mods/svc/resource.h"
#include "mods/svc/ui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>
#include <webgpu/webgpu.h>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE_VERSION(UiService, svc_ui, 2);
IMPORT_SERVICE(GfxService, svc_gfx);
IMPORT_SERVICE(CameraService, svc_camera);
IMPORT_SERVICE(HookService, svc_hook);

namespace {

ConfigVarHandle g_cvarEnabled = 0;
ConfigVarHandle g_cvarMapSize = 0;
ConfigVarHandle g_cvarTerrainCasters = 0;
ConfigVarHandle g_cvarNoFrustumClipping = 0;
ConfigVarHandle g_cvarStrength = 0;
ConfigVarHandle g_cvarPcf = 0;
ConfigVarHandle g_cvarBias = 0;
ConfigVarHandle g_cvarBoxRadius = 0;
ConfigVarHandle g_cvarContactShadows = 0;
ConfigVarHandle g_cvarDebugView = 0;

GfxDrawTypeHandle g_drawType = 0;
GfxStageHookHandle g_worldLateHook = 0;
GfxStageHookHandle g_beforeHudHook = 0;
UiWindowHandle g_controlsWindow = 0;
ResourceBuffer g_shaderSource = RESOURCE_BUFFER_INIT;

GfxDeviceInfo g_deviceInfo = GFX_DEVICE_INFO_INIT;
WGPURenderPipeline g_compositePipeline = nullptr;      // multiply blend
WGPURenderPipeline g_compositeDebugPipeline = nullptr; // no blend (map/factor views)
WGPUBindGroupLayout g_compositeLayout = nullptr;
WGPUBindGroupLayout g_compositeDebugLayout = nullptr;

// Per-frame state handed from the WORLD_LATE map pass to the BEFORE_HUD composite (both game
// thread, same frame; the resolved view is frame-pooled and valid through the frame's encode).
bool g_mapReady = false;
WGPUTextureView g_shadowMapView = nullptr;
uint32_t g_mapSize = 0;
Mtx44 g_lightVp;          // world -> GC light clip, row-major game convention
float g_lightDirWorld[3]; // toward the light, normalized
float g_lightFade = 0.0f;

bool g_loggedMap = false;
bool g_mapHappened = false;
bool g_loggedComposite = false;
bool g_loggedSkip = false;
bool g_gameShadowsSkipped = false;
std::atomic<bool> g_compositeDrawn{false};

constexpr float kLightDistance = 30000.0f;
constexpr float kLightNear = 100.0f;
constexpr float kLightFar = 60000.0f;

using ClipperSphereClip = int (J3DUClipper::*)(f32 const (*)[4], Vec, f32) const;
using ClipperBoxClip = int (J3DUClipper::*)(f32 const (*)[4], Vec*, Vec*) const;
constexpr ClipperSphereClip kClipperSphereClip =
    static_cast<ClipperSphereClip>(&J3DUClipper::clip);
constexpr ClipperBoxClip kClipperBoxClip = static_cast<ClipperBoxClip>(&J3DUClipper::clip);

// Mirror of the WGSL Uniforms struct (keep in sync with res/shadow.wgsl).
struct ShadowUniforms {
    float world_from_proj[16];
    float view_from_proj[16];
    float proj_from_view[16];
    float light_vp[16];
    float light_dir_view[3];
    float bias;
    float size[2];
    float inv_size[2];
    float strength;
    float pcf_taps;
    float contact_enabled;
    float contact_thickness;
    float contact_length;
    uint32_t debug_mode;
    float _pad0;
    float _pad1;
};
static_assert(sizeof(ShadowUniforms) % 16 == 0);

struct DrawPayload {
    WGPUTextureView sceneDepth; // frame-pooled
    WGPUTextureView shadowMap;  // frame-pooled
    uint32_t uniform_offset;
    uint32_t uniform_size;
    uint32_t debug_mode;
};
static_assert(sizeof(DrawPayload) <= GFX_INLINE_DRAW_PAYLOAD_SIZE);
static_assert(std::is_trivially_copyable_v<DrawPayload>);

int64_t get_int_option(ConfigVarHandle handle, int64_t fallback) {
    int64_t value = fallback;
    if (handle == 0 || svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

bool get_bool_option(ConfigVarHandle handle, bool fallback) {
    bool value = fallback;
    if (handle == 0 || svc_config->get_bool(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

// Row-major game matrix -> column-major WGSL layout (matches CameraService conventions).
void store_column_major(const Mtx44 in, float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c * 4 + r] = in[r][c];
        }
    }
}

bool build_composite_pipeline(
    bool blend, WGPURenderPipeline& outPipeline, WGPUBindGroupLayout& outLayout) {
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {static_cast<const char*>(g_shaderSource.data), g_shaderSource.size};
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgsl.chain;
    moduleDesc.label = {"shadow composite", WGPU_STRLEN};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(g_deviceInfo.device, &moduleDesc);
    if (module == nullptr) {
        return false;
    }

    // Multiply blend: fragment output is the darkening multiplier (result = dst * src).
    WGPUBlendState blendState{
        .color = {.operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_Dst,
            .dstFactor = WGPUBlendFactor_Zero},
        .alpha = {.operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_Zero,
            .dstFactor = WGPUBlendFactor_One},
    };
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = g_deviceInfo.color_format;
    if (blend) {
        colorTarget.blend = &blendState;
    }
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = {"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = g_deviceInfo.depth_format;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.depthCompare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = {blend ? "shadow composite" : "shadow composite (debug)", WGPU_STRLEN};
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.depthStencil = &depthStencil;
    pipelineDesc.multisample.count = g_deviceInfo.sample_count;
    pipelineDesc.fragment = &fragment;
    outPipeline = wgpuDeviceCreateRenderPipeline(g_deviceInfo.device, &pipelineDesc);
    wgpuShaderModuleRelease(module);
    if (outPipeline == nullptr) {
        return false;
    }
    outLayout = wgpuRenderPipelineGetBindGroupLayout(outPipeline, 0);
    return outLayout != nullptr;
}

// Render worker thread: fullscreen deferred-shadow composite.
void on_draw(
    ModContext*, const GfxDrawContext* ctx, const void* payload, size_t payloadSize, void*) {
    if (payloadSize != sizeof(DrawPayload)) {
        return;
    }
    DrawPayload data;
    std::memcpy(&data, payload, sizeof(data));
    WGPURenderPipeline pipeline =
        data.debug_mode != 0 ? g_compositeDebugPipeline : g_compositePipeline;
    WGPUBindGroupLayout layout =
        data.debug_mode != 0 ? g_compositeDebugLayout : g_compositeLayout;
    if (data.sceneDepth == nullptr || data.shadowMap == nullptr || pipeline == nullptr) {
        return;
    }

    WGPUBindGroupEntry entries[3] = {
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].textureView = data.sceneDepth;
    entries[1].binding = 1;
    entries[1].textureView = data.shadowMap;
    entries[2].binding = 2;
    entries[2].buffer = ctx->uniform_buffer;
    entries[2].offset = data.uniform_offset;
    entries[2].size = data.uniform_size;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = layout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx->device, &bindGroupDesc);
    if (bindGroup == nullptr) {
        return;
    }

    wgpuRenderPassEncoderSetPipeline(ctx->pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(ctx->pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(ctx->pass, 3, 1, 0, 0);
    wgpuBindGroupRelease(bindGroup);
    g_compositeDrawn.store(true, std::memory_order_release);
}

// Picks the sun or moon (whichever is above the horizon) and returns the normalized
// world-space direction *toward* the light plus a horizon fade factor. False = no light.
bool compute_light(const view_class* view, float outDirToLight[3], float& outFade) {
    dScnKy_env_light_c* envLight = dKy_getEnvlight();
    if (envLight == nullptr) {
        return false;
    }
    // sun_pos is eye-relative absolute; moon_pos is stored as a bare offset.
    const cXyz eye = view->lookat.eye;
    cXyz offset = envLight->sun_pos - eye;
    if (offset.y <= 0.0f) {
        offset = envLight->moon_pos;
    }
    const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    if (length < 1.0f) {
        return false;
    }
    outDirToLight[0] = offset.x / length;
    outDirToLight[1] = offset.y / length;
    outDirToLight[2] = offset.z / length;
    // Fade shadows out as the light approaches the horizon (elevation below ~11 degrees).
    outFade = std::clamp((outDirToLight[1] - 0.05f) / 0.15f, 0.0f, 1.0f);
    return outFade > 0.0f;
}

// True when the dynamic shadow pass will run this frame: enabled, a camera exists, and the
// sun or moon is above the horizon. Also gates the game-shadow skip hooks, which fire earlier
// in the painter than our WORLD_LATE hook.
bool dynamic_shadows_wanted() {
    if (!get_bool_option(g_cvarEnabled, true)) {
        return false;
    }
    view_class* view = dComIfGd_getView();
    if (view == nullptr) {
        return false;
    }
    float dirToLight[3];
    float fade = 0.0f;
    return compute_light(view, dirToLight, fade);
}

// Pre-hook on the game's own shadow rendering (dDlst_shadowControl_c::imageDraw builds the
// projected-shadow textures, ::draw composites them plus the simple blob shadows). While the
// dynamic pass is active they would double up, so skip them; per-frame registration cleanup
// lives in dDlst_shadowControl_c::reset (called from dDlst_list_c::reset each sim tick), so
// skipping the render entry points leaks nothing.
HookAction on_game_shadow_pre(ModContext*, void*, void*, void*) {
    if (!dynamic_shadows_wanted()) {
        return HOOK_CONTINUE;
    }
    g_gameShadowsSkipped = true;
    return HOOK_SKIP_ORIGINAL;
}

HookAction on_frustum_clip_pre(ModContext*, void*, void* retval, void*) {
    if (!get_bool_option(g_cvarNoFrustumClipping, false) || !dynamic_shadows_wanted()) {
        return HOOK_CONTINUE;
    }

    if (retval != nullptr) {
        *static_cast<int*>(retval) = 0;
    }
    return HOOK_SKIP_ORIGINAL;
}

// Game thread, inside the world EFB pass: render the shadow map by re-executing the game's
// opaque draw lists with the light-space projection.
void on_world_late(ModContext*, const GfxStageContext* stageCtx, void*) {
    if (stageCtx->window_index != 0 || g_mapReady || !get_bool_option(g_cvarEnabled, true)) {
        return;
    }
    view_class* view = dComIfGd_getView();
    if (view == nullptr) {
        return;
    }
    float dirToLight[3];
    float fade = 0.0f;
    if (!compute_light(view, dirToLight, fade)) {
        return;
    }

    const uint32_t mapSize =
        1024u << std::clamp<int64_t>(get_int_option(g_cvarMapSize, 1), 0, 2);
    const float radius = static_cast<float>(
        std::clamp<int64_t>(get_int_option(g_cvarBoxRadius, 6000), 1000, 20000));

    // Fit a fixed-radius ortho box ahead of the camera.
    const cXyz eye = view->lookat.eye;
    cXyz forward = view->lookat.center - eye;
    const float forwardLength =
        std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (forwardLength > 0.001f) {
        forward = forward / forwardLength;
    } else {
        forward = cXyz{0.0f, 0.0f, -1.0f};
    }
    cXyz center = eye + forward * (radius * 0.75f);
    cXyz lightEye{center.x + dirToLight[0] * kLightDistance,
        center.y + dirToLight[1] * kLightDistance, center.z + dirToLight[2] * kLightDistance};
    const bool nearlyVertical = std::fabs(dirToLight[1]) > 0.99f;
    cXyz up = nearlyVertical ? cXyz{0.0f, 0.0f, 1.0f} : cXyz{0.0f, 1.0f, 0.0f};

    Mtx lightView;
    cMtx_lookAt(lightView, &lightEye, &center, &up, 0);
    // Snap the light-space translation to shadow-map texel increments so the map does not
    // shimmer as the camera moves.
    const float unitsPerTexel = (2.0f * radius) / static_cast<float>(mapSize);
    lightView[0][3] = std::round(lightView[0][3] / unitsPerTexel) * unitsPerTexel;
    lightView[1][3] = std::round(lightView[1][3] / unitsPerTexel) * unitsPerTexel;

    Mtx44 lightOrtho;
    C_MTXOrtho(lightOrtho, radius, -radius, -radius, radius, kLightNear, kLightFar);
    cMtx_concatProjView(lightOrtho, lightView, g_lightVp);
    // The projection seam: light clip = lightVP * invView * (view * world) = lightVP * world.
    Mtx44 shadowProj;
    cMtx_concatProjView(g_lightVp, view->invViewMtx, shadowProj);

    f32 savedProjection[7];
    GXGetProjectionv(savedProjection);
    if (svc_gfx->create_pass(mod_ctx, mapSize, mapSize) != MOD_OK) {
        return;
    }
    GXSetProjectionFull(shadowProj);

    // Re-execute the opaque scene lists from the light's point of view. Execution does not
    // consume the lists; the game draws them again (from the camera) right after this hook.
    if (get_bool_option(g_cvarTerrainCasters, true)) {
        dComIfGd_drawOpaListBG();
        dComIfGd_drawOpaListDarkBG();
        dComIfGd_drawOpaListMiddle();
    }
    dComIfGd_drawOpaList();
    dComIfGd_drawOpaListDark();
    dComIfGd_drawOpaListPacket();

    GXSetProjectionv(savedProjection);

    GfxResolveDesc resolveDesc = GFX_RESOLVE_DESC_INIT;
    resolveDesc.color = false;
    resolveDesc.depth = true;
    GfxResolvedTargets resolved = GFX_RESOLVED_TARGETS_INIT;
    if (svc_gfx->resolve_pass(mod_ctx, &resolveDesc, &resolved) != MOD_OK ||
        resolved.depth == nullptr)
    {
        return;
    }

    g_shadowMapView = resolved.depth;
    g_mapSize = mapSize;
    g_lightDirWorld[0] = dirToLight[0];
    g_lightDirWorld[1] = dirToLight[1];
    g_lightDirWorld[2] = dirToLight[2];
    g_lightFade = fade;
    g_mapReady = true;
    g_mapHappened = true;
}

// Game thread, after the full 3D scene: deferred composite.
void on_before_hud(ModContext*, const GfxStageContext*, void*) {
    const bool mapReady = std::exchange(g_mapReady, false);
    WGPUTextureView shadowMap = std::exchange(g_shadowMapView, nullptr);
    if (!mapReady || shadowMap == nullptr) {
        return;
    }

    CameraInfo camera = CAMERA_INFO_INIT;
    if (svc_camera->get_camera(mod_ctx, &camera) != MOD_OK) {
        return;
    }

    GfxResolveDesc resolveDesc = GFX_RESOLVE_DESC_INIT;
    resolveDesc.color = false;
    resolveDesc.depth = true;
    GfxResolvedTargets resolved = GFX_RESOLVED_TARGETS_INIT;
    if (svc_gfx->resolve_pass(mod_ctx, &resolveDesc, &resolved) != MOD_OK ||
        resolved.depth == nullptr)
    {
        return;
    }

    const int64_t debugMode = std::clamp<int64_t>(get_int_option(g_cvarDebugView, 0), 0, 2);

    ShadowUniforms uniforms{};
    std::memcpy(uniforms.world_from_proj, camera.world_from_proj,
        sizeof(uniforms.world_from_proj));
    std::memcpy(uniforms.view_from_proj, camera.view_from_proj, sizeof(uniforms.view_from_proj));
    std::memcpy(uniforms.proj_from_view, camera.proj_from_view, sizeof(uniforms.proj_from_view));
    store_column_major(g_lightVp, uniforms.light_vp);
    // Rotate the world-space light direction into view space (w = 0).
    for (int r = 0; r < 3; ++r) {
        uniforms.light_dir_view[r] = camera.view_from_world[0 * 4 + r] * g_lightDirWorld[0] +
            camera.view_from_world[1 * 4 + r] * g_lightDirWorld[1] +
            camera.view_from_world[2 * 4 + r] * g_lightDirWorld[2];
    }
    // Bias is configured in world units along the light direction.
    uniforms.bias =
        static_cast<float>(std::clamp<int64_t>(get_int_option(g_cvarBias, 15), 0, 200)) /
        (kLightFar - kLightNear);
    uniforms.size[0] = static_cast<float>(g_mapSize);
    uniforms.size[1] = static_cast<float>(g_mapSize);
    uniforms.inv_size[0] = 1.0f / uniforms.size[0];
    uniforms.inv_size[1] = 1.0f / uniforms.size[1];
    uniforms.strength = g_lightFade *
        static_cast<float>(std::clamp<int64_t>(get_int_option(g_cvarStrength, 45), 0, 100)) /
        100.0f;
    uniforms.pcf_taps =
        static_cast<float>(std::clamp<int64_t>(get_int_option(g_cvarPcf, 1), 0, 2));
    uniforms.contact_enabled = get_bool_option(g_cvarContactShadows, false) ? 1.0f : 0.0f;
    uniforms.contact_thickness = 25.0f;
    uniforms.contact_length = 60.0f;
    uniforms.debug_mode = static_cast<uint32_t>(debugMode);

    GfxRange uniformRange{0, 0};
    if (svc_gfx->push_uniform(mod_ctx, &uniforms, sizeof(uniforms), &uniformRange) != MOD_OK) {
        return;
    }
    const DrawPayload payload{resolved.depth, shadowMap, uniformRange.offset, uniformRange.size,
        static_cast<uint32_t>(debugMode)};
    svc_gfx->push_draw(mod_ctx, g_drawType, &payload, sizeof(payload));
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void add_toggle(UiElementHandle pane, const char* label, ConfigVarHandle cvar, const char* help) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    add_control(pane, control);
}

void add_select(UiElementHandle pane, const char* label, ConfigVarHandle cvar,
    const char** options, uint32_t optionCount, const char* help) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_SELECT;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    control.options = options;
    control.option_count = optionCount;
    add_control(pane, control);
}

void add_number(UiElementHandle pane, const char* label, ConfigVarHandle cvar, int64_t min,
    int64_t max, int64_t step, const char* suffix, const char* help) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    control.min = min;
    control.max = max;
    control.step = step;
    control.suffix = suffix;
    add_control(pane, control);
}

ModResult build_controls_tab(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    (void)right;

    svc_ui->pane_add_section(mod_ctx, left, "Shadow Map");
    add_toggle(left, "Enabled", g_cvarEnabled, "Enables dynamic shadows.");
    static const char* kMapSizes[] = {"1024", "2048", "4096"};
    add_select(left, "Map Size", g_cvarMapSize, kMapSizes, 3,
        "Shadow map resolution. Larger is sharper and slower.");
    add_toggle(left, "Terrain Casters", g_cvarTerrainCasters,
        "Also renders terrain and buildings into the shadow map (canyon walls shade actors), "
        "at the cost of a second terrain pass.");
    add_toggle(left, "No Frustum Clipping", g_cvarNoFrustumClipping,
        "Keeps camera-frustum-culled objects in draw lists so off-screen objects can cast "
        "dynamic shadows. This can be expensive.");
    add_number(left, "Coverage", g_cvarBoxRadius, 1000, 20000, 500, nullptr,
        "Radius of the shadowed area around the camera, in world units. Smaller is sharper.");

    svc_ui->pane_add_section(mod_ctx, left, "Appearance");
    add_number(left, "Strength", g_cvarStrength, 0, 100, 5, "%",
        "How dark shadowed areas become.");
    static const char* kPcfOptions[] = {"Off", "3x3", "5x5"};
    add_select(left, "Soft Shadows", g_cvarPcf, kPcfOptions, 3,
        "Percentage-closer filtering tap pattern; softens shadow edges.");
    add_number(left, "Bias", g_cvarBias, 0, 200, 5, nullptr,
        "Depth bias in world units. Raise to remove shadow acne; lower to reduce peter-panning.");
    add_toggle(left, "Contact Shadows", g_cvarContactShadows,
        "Adds a screen-space raymarch for small-scale contact darkening the map misses.");

    svc_ui->pane_add_section(mod_ctx, left, "Debug");
    static const char* kDebugOptions[] = {"Off", "Shadow Map", "Shadow Factor"};
    add_select(left, "Debug View", g_cvarDebugView, kDebugOptions, 3,
        "Shadow Map shows the light-space depth buffer; Shadow Factor shows the darkening "
        "term as grayscale.");
    return MOD_OK;
}

void on_controls_window_closed(ModContext*, UiWindowHandle, void*) {
    g_controlsWindow = 0;
}

void on_open_controls(ModContext*, void*) {
    if (g_controlsWindow != 0) {
        return;
    }
    UiTabDesc tabs[1] = {UI_TAB_DESC_INIT};
    tabs[0].title = "Controls";
    tabs[0].build = build_controls_tab;
    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 1;
    desc.on_closed = on_controls_window_closed;
    if (svc_ui->window_push(mod_ctx, &desc, &g_controlsWindow) != MOD_OK) {
        svc_log->error(mod_ctx, "failed to open shadow controls window");
    }
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Enabled";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarEnabled;
    add_control(panel, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_BUTTON;
    control.label = "Open Controls";
    control.on_pressed = on_open_controls;
    add_control(panel, control);
    return MOD_OK;
}

ModResult register_bool_option(
    const char* name, bool defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_BOOL;
    cvarDesc.default_bool = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register shadow option");
    }
    return MOD_OK;
}

ModResult register_int_option(
    const char* name, int64_t defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_INT;
    cvarDesc.default_int = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register shadow option");
    }
    return MOD_OK;
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = svc_resource->load(mod_ctx, "shadow.wgsl", &g_shaderSource);
    if (result != MOD_OK || g_shaderSource.data == nullptr) {
        return dusk::mods::set_error(error, result, "failed to load shadow.wgsl");
    }

    result = register_bool_option("effectEnabled", true, g_cvarEnabled, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("mapSize", 1, g_cvarMapSize, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_bool_option("terrainCasters", true, g_cvarTerrainCasters, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_bool_option("noFrustumClipping", false, g_cvarNoFrustumClipping, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("strength", 45, g_cvarStrength, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("pcf", 1, g_cvarPcf, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("bias", 15, g_cvarBias, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("boxRadius", 6000, g_cvarBoxRadius, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_bool_option("contactShadows", false, g_cvarContactShadows, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("debugView", 0, g_cvarDebugView, error);
    if (result != MOD_OK) {
        return result;
    }

    if (svc_gfx->get_device_info(mod_ctx, &g_deviceInfo) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to query device info");
    }
    if (!build_composite_pipeline(true, g_compositePipeline, g_compositeLayout) ||
        !build_composite_pipeline(false, g_compositeDebugPipeline, g_compositeDebugLayout))
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to create composite pipeline");
    }

    GfxDrawTypeDesc drawDesc = GFX_DRAW_TYPE_DESC_INIT;
    drawDesc.label = "shadow composite";
    drawDesc.draw = on_draw;
    if (svc_gfx->register_draw_type(mod_ctx, &drawDesc, &g_drawType) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register draw type");
    }
    GfxStageHookDesc stageDesc = GFX_STAGE_HOOK_DESC_INIT;
    stageDesc.callback = on_world_late;
    if (svc_gfx->register_stage_hook(mod_ctx, GFX_STAGE_WORLD_LATE, &stageDesc, &g_worldLateHook) !=
        MOD_OK)
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register stage hook");
    }
    stageDesc.callback = on_before_hud;
    if (svc_gfx->register_stage_hook(mod_ctx, GFX_STAGE_BEFORE_HUD, &stageDesc, &g_beforeHudHook) !=
        MOD_OK)
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register stage hook");
    }

    // Skip the game's own shadow rendering while the dynamic pass is active: the
    // shadowControl pair covers the actor real/blob shadows, drawCloudShadow the weather
    // cloud shadows.
    if (dusk::mods::hook_add_pre<&dDlst_shadowControl_c::imageDraw>(
            svc_hook, on_game_shadow_pre) != MOD_OK ||
        dusk::mods::hook_add_pre<&dDlst_shadowControl_c::draw>(svc_hook, on_game_shadow_pre) !=
            MOD_OK ||
        dusk::mods::hook_add_pre<&drawCloudShadow>(svc_hook, on_game_shadow_pre) != MOD_OK)
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to hook game shadow rendering");
    }
    if (dusk::mods::hook_add_pre<kClipperSphereClip>(svc_hook, on_frustum_clip_pre) != MOD_OK ||
        dusk::mods::hook_add_pre<kClipperBoxClip>(svc_hook, on_frustum_clip_pre) != MOD_OK)
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to hook frustum clipping");
    }

    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panelDesc);

    svc_log->info(mod_ctx, "shadow_mod ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    if (!g_loggedMap && g_mapHappened) {
        g_loggedMap = true;
        char logBuf[64];
        std::snprintf(logBuf, sizeof(logBuf), "shadow map pass OK (%ux%u)", g_mapSize, g_mapSize);
        svc_log->info(mod_ctx, logBuf);
    }
    if (!g_loggedComposite && g_compositeDrawn.load(std::memory_order_acquire)) {
        g_loggedComposite = true;
        svc_log->info(mod_ctx, "shadow composite OK");
    }
    if (!g_loggedSkip && g_gameShadowsSkipped) {
        g_loggedSkip = true;
        svc_log->info(mod_ctx, "game shadow rendering suppressed");
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    svc_resource->free(mod_ctx, &g_shaderSource);
    if (g_compositePipeline != nullptr) {
        wgpuRenderPipelineRelease(g_compositePipeline);
        g_compositePipeline = nullptr;
    }
    if (g_compositeDebugPipeline != nullptr) {
        wgpuRenderPipelineRelease(g_compositeDebugPipeline);
        g_compositeDebugPipeline = nullptr;
    }
    if (g_compositeLayout != nullptr) {
        wgpuBindGroupLayoutRelease(g_compositeLayout);
        g_compositeLayout = nullptr;
    }
    if (g_compositeDebugLayout != nullptr) {
        wgpuBindGroupLayoutRelease(g_compositeDebugLayout);
        g_compositeDebugLayout = nullptr;
    }
    g_cvarEnabled = g_cvarMapSize = g_cvarTerrainCasters = g_cvarNoFrustumClipping = 0;
    g_cvarStrength = 0;
    g_cvarPcf = g_cvarBias = g_cvarBoxRadius = g_cvarContactShadows = g_cvarDebugView = 0;
    g_drawType = g_worldLateHook = g_beforeHudHook = 0;
    g_controlsWindow = 0;
    g_shadowMapView = nullptr;
    g_mapReady = false;
    return MOD_OK;
}
}
