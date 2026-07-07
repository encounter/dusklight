// Example CRT post-process filter mod utilizing the `gfx` service.
// Showcases how to hook into a specific rendering stage and execute custom draw logic directly
// through WebGPU and with WGSL shaders, fully cross-platform.
//
// Every frame, after the game frame is fully rendered:
// - It calls `resolve_pass`, resolving the finished scene into a texture that can be sampled.
// - A fullscreen triangle is drawn with a simple CRT fragment shader sampling the resolved scene.
// Dusklight UI is rendered on top of the post-processed game frame.

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/gfx.h"
#include "mods/svc/log.h"
#include "mods/svc/resource.h"
#include "mods/svc/ui.h"

#include <algorithm>
#include <cstring>
#include <webgpu/webgpu.h>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE_VERSION(UiService, svc_ui, 2);
IMPORT_SERVICE(GfxService, svc_gfx);

namespace {

// Handles for configuration variables (CVars)
ConfigVarHandle g_cvarEnabled = 0;
ConfigVarHandle g_cvarScanlineStrength = 0;
ConfigVarHandle g_cvarLineScale = 0;
ConfigVarHandle g_cvarMaskStyle = 0;
ConfigVarHandle g_cvarMaskStrength = 0;
ConfigVarHandle g_cvarMaskScale = 0;
ConfigVarHandle g_cvarCurvature = 0;
ConfigVarHandle g_cvarVignette = 0;
ConfigVarHandle g_cvarChroma = 0;
ConfigVarHandle g_cvarBloom = 0;
ConfigVarHandle g_cvarSharpness = 0;
ConfigVarHandle g_cvarBrightnessBoost = 0;
// Custom draw type, allocated on init
GfxDrawTypeHandle g_drawType = 0;
// Handle for the `on_stage` hook
GfxStageHookHandle g_stageHook = 0;
// Handle for the "Controls" window
UiWindowHandle g_controlsWindow = 0;
// Loaded resource bytes from res/shader.wgsl
ResourceBuffer g_shaderResource = RESOURCE_BUFFER_INIT;

// Game thread submits this payload with every draw,
// and the GPU thread encodes commands using it
struct DrawPayload {
    // Frame-pooled texture, Aurora-managed, valid through this frame's encode
    WGPUTextureView scene;
    uint32_t uniform_offset;
    uint32_t uniform_size;
};
// Ensure that we're under the maximum size allowed
static_assert(sizeof(DrawPayload) <= GFX_INLINE_DRAW_PAYLOAD_SIZE);
// A draw payload must be bytes-copyable and must not have any C++
// destruction logic (RAII handles, etc.)
static_assert(std::is_trivially_copyable_v<DrawPayload>);
static_assert(std::is_trivially_destructible_v<DrawPayload>);

// Mirror of the shader-side uniforms struct
// (Keep in sync with shader.wgsl!)
struct DrawUniforms {
    float width;
    float height;
    float scanline_strength;
    float line_scale;
    float mask_strength;
    float mask_scale;
    float curvature;
    float vignette;
    float chroma;
    float bloom;
    float sharpness;
    float brightness;
    uint32_t mask_style;
    float _pad0 = 0.f;
    float _pad1 = 0.f;
    float _pad2 = 0.f;
};
static_assert(sizeof(DrawUniforms) % 16 == 0);

// Persistent WebGPU resources
WGPURenderPipeline g_pipeline = nullptr;
WGPUBindGroupLayout g_bindGroupLayout = nullptr;
WGPUSampler g_sampler = nullptr;

int64_t get_int_option(ConfigVarHandle handle, int64_t fallback) {
    int64_t value = fallback;
    if (handle == 0 || svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

float get_percent_option(
    ConfigVarHandle handle, int64_t fallback, float minValue = 0.0f, float maxValue = 1.0f) {
    return std::clamp(
        static_cast<float>(get_int_option(handle, fallback)) / 100.0f, minValue, maxValue);
}

DrawUniforms build_uniforms(const GfxResolvedTargets& targets) {
    return DrawUniforms{
        .width = static_cast<float>(targets.width),
        .height = static_cast<float>(targets.height),
        .scanline_strength = get_percent_option(g_cvarScanlineStrength, 65),
        .line_scale =
            static_cast<float>(std::clamp<int64_t>(get_int_option(g_cvarLineScale, 2), 1, 8)),
        .mask_strength = get_percent_option(g_cvarMaskStrength, 45),
        .mask_scale =
            static_cast<float>(std::clamp<int64_t>(get_int_option(g_cvarMaskScale, 1), 1, 8)),
        .curvature = get_percent_option(g_cvarCurvature, 0) * 0.12f,
        .vignette = get_percent_option(g_cvarVignette, 10),
        .chroma = get_percent_option(g_cvarChroma, 4),
        .bloom = get_percent_option(g_cvarBloom, 12),
        .sharpness = get_percent_option(g_cvarSharpness, 35),
        .brightness = 1.0f + get_percent_option(g_cvarBrightnessBoost, 18),
        .mask_style =
            static_cast<uint32_t>(std::clamp<int64_t>(get_int_option(g_cvarMaskStyle, 1), 0, 3)),
    };
}

ModResult register_bool_option(
    const char* name, bool defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_BOOL;
    cvarDesc.default_bool = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register CRT option");
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
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register CRT option");
    }
    return MOD_OK;
}

// Build our pipeline and sampler.
bool build_pipeline() {
    GfxDeviceInfo info = GFX_DEVICE_INFO_INIT;
    if (svc_gfx->get_device_info(mod_ctx, &info) != MOD_OK) {
        return false;
    }

    // Compile shader
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {static_cast<const char*>(g_shaderResource.data), g_shaderResource.size};
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgsl.chain;
    moduleDesc.label = {"CRT shader", WGPU_STRLEN};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(info.device, &moduleDesc);
    if (module == nullptr) {
        return false;
    }

    // Our color *and* depth target state must match the render pass,
    // despite our pipeline never reading or writing depth.
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = info.color_format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = {"fs_main", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = info.depth_format;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.depthCompare = WGPUCompareFunction_Always;

    // Compile pipeline
    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = {"CRT pipeline", WGPU_STRLEN};
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.depthStencil = &depthStencil;
    pipelineDesc.multisample.count = info.sample_count;
    pipelineDesc.fragment = &fragment;
    g_pipeline = wgpuDeviceCreateRenderPipeline(info.device, &pipelineDesc);
    wgpuShaderModuleRelease(module);
    if (g_pipeline == nullptr) {
        return false;
    }
    g_bindGroupLayout = wgpuRenderPipelineGetBindGroupLayout(g_pipeline, 0);

    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    samplerDesc.label = {"CRT sampler", WGPU_STRLEN};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    g_sampler = wgpuDeviceCreateSampler(info.device, &samplerDesc);
    return g_sampler != nullptr;
}

// Draw callbacks run on a separate GPU thread.
// Generally only safe to call wgpu functions here.
// Per-draw information is provided via `DrawPayload`.
void on_draw(
    ModContext*, const GfxDrawContext* ctx, const void* payload, size_t payloadSize, void*) {
    if (payloadSize != sizeof(DrawPayload)) {
        return;
    }
    DrawPayload data;
    std::memcpy(&data, payload, sizeof(data));
    if (data.scene == nullptr || g_pipeline == nullptr) {
        return;
    }

    WGPUBindGroupEntry entries[3] = {
        WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].sampler = g_sampler;
    entries[1].binding = 1;
    entries[1].textureView = data.scene;
    entries[2].binding = 2;
    entries[2].buffer = ctx->uniform_buffer;
    entries[2].offset = data.uniform_offset;
    entries[2].size = data.uniform_size;
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = g_bindGroupLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx->device, &bindGroupDesc);
    if (bindGroup == nullptr) {
        return;
    }

    wgpuRenderPassEncoderSetPipeline(ctx->pass, g_pipeline);
    wgpuRenderPassEncoderSetBindGroup(ctx->pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(ctx->pass, 3, 1, 0, 0);
    wgpuBindGroupRelease(bindGroup);  // Internally ref-counted; safe to release immediately
}

// Called on game thread after the whole game frame (HUD included) is in the EFB.
void on_stage(ModContext*, const GfxStageContext*, void*) {
    bool enabled = false;
    if (svc_config->get_bool(mod_ctx, g_cvarEnabled, &enabled) != MOD_OK || !enabled) {
        return;
    }

    GfxResolveDesc resolveDesc = GFX_RESOLVE_DESC_INIT;
    GfxResolvedTargets targets = GFX_RESOLVED_TARGETS_INIT;
    if (svc_gfx->resolve_pass(mod_ctx, &resolveDesc, &targets) != MOD_OK ||
        targets.color == nullptr)
    {
        return;
    }

    const DrawUniforms uniforms = build_uniforms(targets);
    GfxRange uniformRange{0, 0};
    if (svc_gfx->push_uniform(mod_ctx, &uniforms, sizeof(uniforms), &uniformRange) != MOD_OK) {
        return;
    }

    const DrawPayload payload{targets.color, uniformRange.offset, uniformRange.size};
    svc_gfx->push_draw(mod_ctx, g_drawType, &payload, sizeof(payload));
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

void add_number_control(UiElementHandle pane, const char* label, ConfigVarHandle cvar, int64_t min,
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
    (void)right;  // Unused

    svc_ui->pane_add_section(mod_ctx, left, "Signal");
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Enabled";
    control.help_rml = "Enables the post-process pass.";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarEnabled;
    add_control(left, control);

    static const char* kMaskOptions[] = {"Off", "Aperture Grille", "Slot Mask", "Dot Triad"};
    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_SELECT;
    control.label = "Mask Style";
    control.help_rml = "Procedural phosphor mask applied in screen pixels.";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarMaskStyle;
    control.options = kMaskOptions;
    control.option_count = 4;
    add_control(left, control);

    add_number_control(left, "Scanlines", g_cvarScanlineStrength, 0, 100, 5, "%",
        "Blends from the unfiltered frame to the CRT-Lottes scanline filter.");
    add_number_control(left, "Line Scale", g_cvarLineScale, 1, 8, 1, "x",
        "Sets the virtual source-line height used by the CRT filter.");
    add_number_control(left, "Mask Strength", g_cvarMaskStrength, 0, 100, 5, "%",
        "Controls how strongly the selected phosphor mask modulates RGB.");
    add_number_control(left, "Mask Scale", g_cvarMaskScale, 1, 8, 1, "x",
        "Enlarges the phosphor mask pattern when it aliases at high internal resolutions.");
    add_number_control(left, "Chroma Spread", g_cvarChroma, 0, 60, 2, "%",
        "Offsets red and blue slightly toward the curved edges.");

    svc_ui->pane_add_section(mod_ctx, left, "Display");
    add_number_control(left, "Curvature", g_cvarCurvature, 0, 100, 5, "%",
        "Barrel-distorts the sampled image before it is written back.");
    add_number_control(left, "Vignette", g_cvarVignette, 0, 100, 5, "%",
        "Darkens the corners after the mask and scanline passes.");
    add_number_control(left, "Bloom", g_cvarBloom, 0, 100, 5, "%",
        "Adds a small single-pass glow from bright nearby samples.");
    add_number_control(left, "Sharpness", g_cvarSharpness, 0, 100, 5, "%",
        "Adds a light unsharp mask before scanlines.");
    add_number_control(left, "Brightness Boost", g_cvarBrightnessBoost, 0, 100, 5, "%",
        "Compensates for mask and scanline darkening; zero is neutral.");
    return MOD_OK;
}

void on_controls_window_closed(ModContext*, UiWindowHandle, void*) {
    g_controlsWindow = 0;  // Clear our handle
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
        svc_log->error(mod_ctx, "failed to open CRT controls window");
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

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    // Load res/shader.wgsl on init
    ModResult result = svc_resource->load(mod_ctx, "shader.wgsl", &g_shaderResource);
    if (result != MOD_OK || g_shaderResource.data == nullptr) {
        return dusk::mods::set_error(error, result, "failed to load shader.wgsl");
    }

    // Register all config variables
    result = register_bool_option("effectEnabled", true, g_cvarEnabled, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("scanlineStrength", 65, g_cvarScanlineStrength, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("lineScale", 2, g_cvarLineScale, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("maskStyle", 1, g_cvarMaskStyle, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("maskStrength", 45, g_cvarMaskStrength, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("maskScale", 1, g_cvarMaskScale, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("curvature", 0, g_cvarCurvature, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("vignette", 10, g_cvarVignette, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("chromaSpread", 4, g_cvarChroma, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("bloom", 12, g_cvarBloom, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("sharpness", 35, g_cvarSharpness, error);
    if (result != MOD_OK) {
        return result;
    }
    result = register_int_option("brightnessBoost", 18, g_cvarBrightnessBoost, error);
    if (result != MOD_OK) {
        return result;
    }

    // Build our WebGPU resources up front
    if (!build_pipeline()) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to create CRT pipeline");
    }

    // `on_draw` will run on a separate GPU thread when recording WebGPU commands
    GfxDrawTypeDesc drawDesc = GFX_DRAW_TYPE_DESC_INIT;
    drawDesc.label = "CRT filter";
    drawDesc.draw = on_draw;
    if (svc_gfx->register_draw_type(mod_ctx, &drawDesc, &g_drawType) != MOD_OK) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register draw type");
    }

    // `on_stage` will run on the main thread "after HUD" in the frame's draw
    GfxStageHookDesc stageDesc = GFX_STAGE_HOOK_DESC_INIT;
    stageDesc.callback = on_stage;
    if (svc_gfx->register_stage_hook(mod_ctx, GFX_STAGE_FRAME_AFTER_HUD, &stageDesc, &g_stageHook) !=
        MOD_OK)
    {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to register stage hook");
    }

    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panelDesc);
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    svc_resource->free(mod_ctx, &g_shaderResource);
    if (g_pipeline != nullptr) {
        wgpuRenderPipelineRelease(g_pipeline);
        g_pipeline = nullptr;
    }
    if (g_bindGroupLayout != nullptr) {
        wgpuBindGroupLayoutRelease(g_bindGroupLayout);
        g_bindGroupLayout = nullptr;
    }
    if (g_sampler != nullptr) {
        wgpuSamplerRelease(g_sampler);
        g_sampler = nullptr;
    }
    g_cvarEnabled = 0;
    g_cvarScanlineStrength = 0;
    g_cvarLineScale = 0;
    g_cvarMaskStyle = 0;
    g_cvarMaskStrength = 0;
    g_cvarMaskScale = 0;
    g_cvarCurvature = 0;
    g_cvarVignette = 0;
    g_cvarChroma = 0;
    g_cvarBloom = 0;
    g_cvarSharpness = 0;
    g_cvarBrightnessBoost = 0;
    g_drawType = 0;
    g_stageHook = 0;
    g_controlsWindow = 0;
    return MOD_OK;
}
}
