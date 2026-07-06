// Tests every feature of the Dusklight mod API. Results are shown in the mod tab.

#include "d/actor/d_a_alink.h"
#include "dolphin/dvd.h"
#include "dolphin/mtx.h"
#include "dusk/mods/item_checks.hpp"
#include "f_ap/f_ap_game.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/camera.h"
#include "mods/svc/config.h"
#include "mods/svc/game.h"
#include "mods/svc/gfx.h"
#include "mods/svc/hook.h"
#include "mods/svc/host.h"
#include "mods/svc/item.h"
#include "mods/svc/log.h"
#include "mods/svc/overlay.h"
#include "mods/svc/resource.h"
#include "mods/svc/texture.h"
#include "mods/svc/ui.h"
#include "test_services.h"

#include <atomic>
#include <cmath>
#include <cstring>

DEFINE_MOD();
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE_VERSION(UiService, svc_ui, 2);
IMPORT_SERVICE_VERSION(HookService, svc_hook, 1);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(TextureService, svc_texture);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE_VERSION(GfxService, svc_gfx, 1);
IMPORT_SERVICE(CameraService, svc_camera);
IMPORT_SERVICE(GameService, svc_game);
IMPORT_SERVICE(ItemService, svc_item);
// Both provided by mod_test_dep, which sorts *after* mod_test.dusk in the mods directory;
// dependency ordering must initialize it first regardless. The deferred service only
// resolves if mod_test_dep published it during its initialization.
IMPORT_SERVICE(TestDepService, svc_test_dep);
IMPORT_SERVICE(TestDepDeferredService, svc_test_dep_deferred);

namespace {

int g_ticks = 0;
bool g_pre_fired = false;
bool g_post_fired = false;
bool g_replace_fired = false;
bool g_arg_write_ok = false;
int g_pre_cancel_count = 0;
int g_post_count = 0;
int g_last_link_y_angle = 0;
bool g_seen_link = false;
bool g_resource_ok = false;
char g_resource_text[256] = {};
char g_mod_dir_snippet[64] = {};
int32_t g_initialized = 0;
bool g_dep_order_ok = false;
bool g_dep_deferred_ok = false;
bool g_overlay_add_ok = false;
bool g_overlay_read_ok = false;
bool g_texture_reg_ok = false;
bool g_asset_neg_ok = false;
bool g_config_ok = false;
bool g_config_change_ok = false;
bool g_config_neg_ok = false;
bool g_item_check_ok = false;
bool g_item_check_neg_ok = false;
int g_item_observed = -1;
int g_item_observed_vanilla = -1;
int g_config_change_count = 0;
int64_t g_config_prev_value = -1;
int64_t g_config_curr_value = -1;
bool g_ui_stack_ok = false;
bool g_ui_rcss_ok = false;
bool g_ui_neg_ok = false;
bool g_gfx_device_ok = false;
bool g_gfx_neg_ok = false;
bool g_camera_neg_ok = false;
bool g_camera_checked = false;  // first successful get_camera snapshot processed
bool g_camera_ok = false;
float g_camera_near = 0.0f;
float g_camera_far = 0.0f;
bool g_gfx_stage_ran = false;  // set once by the stage callback (game thread)
bool g_gfx_stage_ok = false;
bool g_gfx_resolve_ok = false;
bool g_gfx_create_ok = false;
bool g_gfx_logged_stage = false;
bool g_gfx_logged_draw = false;
int g_gfx_draw_wait_ticks = 0;
GfxDrawTypeHandle g_gfx_draw_type = 0;
GfxStageHookHandle g_gfx_stage_hook = 0;
// Written by the draw callback on the render worker thread.
std::atomic<bool> g_gfx_draw_fired{false};
std::atomic<bool> g_gfx_draw_ctx_ok{false};
std::atomic<bool> g_gfx_offscreen_draw_ok{false};
// Compute/encoder task coverage (gfx minor 1).
bool g_gfx_compute_neg_ok = false;
bool g_gfx_compute_push_ok = false;
bool g_gfx_logged_compute = false;
GfxComputeTypeHandle g_gfx_compute_type = 0;
std::atomic<bool> g_gfx_compute_fired{false};
std::atomic<bool> g_gfx_compute_ok{false};
WGPUComputePipeline g_gfx_compute_pipeline = nullptr;
WGPUBindGroupLayout g_gfx_compute_bgl = nullptr;
WGPUTexture g_gfx_compute_tex = nullptr;
WGPUTextureView g_gfx_compute_view = nullptr;

// Kept live for the UI test window's cvar-bound controls.
ConfigVarHandle g_cfg_flag = 0;
ConfigVarHandle g_cfg_int = 0;
ConfigVarHandle g_cfg_string = 0;
ConfigVarHandle g_cfg_choice = 0;
ConfigVarHandle g_cfg_pride = 0;
// The registered Pride Mode style, 0 while disabled.
UiStyleHandle g_style = 0;
UiWindowHandle g_test_window = 0;
UiElementHandle g_el_win_counter = 0;
int g_window_updates = 0;

// Backing state for the callback-bound controls.
bool g_cb_toggle = false;
int64_t g_cb_number = 5;
char g_cb_string[64] = "callback";

void on_config_changed(ModContext*, ConfigVarHandle, const ConfigVarValue* value,
    const ConfigVarValue* previous, void* userData) {
    ++*static_cast<int*>(userData);
    if (value != nullptr && value->type == CONFIG_VAR_INT) {
        g_config_curr_value = value->int_value;
    }
    if (previous != nullptr && previous->type == CONFIG_VAR_INT) {
        g_config_prev_value = previous->int_value;
    }
}

constexpr char kRuntimeOverlayData[] = "runtime overlay data OK";
alignas(32) const uint8_t s_rawTexture[8 * 8 * 4] = {0xff};

int32_t test_main_initialized(ModContext*) {
    return g_initialized;
}

constexpr TestMainService s_testMainService{
    .header = SERVICE_HEADER(TestMainService, TEST_MAIN_SERVICE_MAJOR, TEST_MAIN_SERVICE_MINOR),
    .initialized = test_main_initialized,
};

UiElementHandle g_el_pre_badge = 0;
UiElementHandle g_el_post_badge = 0;
UiElementHandle g_el_replace_badge = 0;
UiElementHandle g_el_argwrite_badge = 0;
UiElementHandle g_el_cancel_count = 0;
UiElementHandle g_el_post_count = 0;
UiElementHandle g_el_link_angle = 0;
UiElementHandle g_el_overlay_read_badge = 0;

ModResult require_ok(const ModResult result, ModError* error, const char* message) {
    if (result != MOD_OK) {
        return dusk::mods::set_error(error, result, message);
    }
    return MOD_OK;
}

bool sdk_mtx_exports_ok() {
    Mtx44 projection{};
    C_MTXPerspective(projection, 60.0f, 16.0f / 9.0f, 10.0f, 10000.0f);
    return std::isfinite(projection[0][0]) && std::isfinite(projection[1][1]) &&
           projection[3][2] == -1.0f;
}

// Pre-hook on posMove. Hold L to test arg_ref writes and cancellation.
HookAction on_pos_move_pre(ModContext*, void* args, void*, void*) {
    g_pre_fired = true;
    auto* link = dusk::mods::arg<daAlink_c*>(args, 0);
    if (link != nullptr) {
        g_last_link_y_angle = link->shape_angle.y;
        g_seen_link = true;
    }
    if (link != nullptr && (mDoCPd_c::getHoldL(PAD_1) != 0 || mDoCPd_c::getHoldLockL(PAD_1) != 0)) {
        dusk::mods::arg_ref<daAlink_c*>(args, 0)->shape_angle.y = 0;
        g_arg_write_ok = true;
        ++g_pre_cancel_count;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// Post-hook on posMove. Fires even when the pre-hook cancelled.
void on_pos_move_post(ModContext*, void* args, void* retval, void*) {
    g_post_fired = true;
    ++g_post_count;
    (void)args;
    (void)retval;
}

// Replace-hook on execute. Calls through to the original so gameplay is unaffected.
using ExecuteEntry = dusk::mods::HookEntry<&daAlink_c::execute>;
void on_execute_replace(ModContext*, void* args, void* retval, void*) {
    g_replace_fired = true;
    int result = ExecuteEntry::g_orig(dusk::mods::arg<daAlink_c*>(args, 0));
    if (retval != nullptr) {
        *static_cast<int*>(retval) = result;
    }
}

// Pre-hook on the main game loop; dispatches every frame, including headless runs (the other
// hooks need Link in-game).
int g_game_loop_dispatches = 0;
HookAction on_game_loop_pre(ModContext*, void*, void*, void*) {
    if (g_game_loop_dispatches++ == 0) {
        svc_log->info(mod_ctx, "mod_test: game loop hook active");

        // Runtime overlays registered in mod_initialize were pushed at startup.
        // Read one back through the DVD API end to end.
        DVDFileInfo fileInfo;
        if (DVDOpen("/mod_test_runtime.txt", &fileInfo)) {
            alignas(32) char buf[64] = {};
            const s32 read = DVDReadPrio(&fileInfo, buf, 32, 0, 2);
            constexpr s32 expected = sizeof(kRuntimeOverlayData) - 1;
            g_overlay_read_ok =
                read == expected && std::memcmp(buf, kRuntimeOverlayData, expected) == 0;
            DVDClose(&fileInfo);
        }
        if (g_overlay_read_ok) {
            svc_log->info(mod_ctx, "OverlayService runtime read OK");
        } else {
            svc_log->error(mod_ctx, "OverlayService runtime read FAILED");
        }
    }
    return HOOK_CONTINUE;
}

void on_reset(ModContext*, void*) {
    g_pre_fired = false;
    g_post_fired = false;
    g_replace_fired = false;
    g_arg_write_ok = false;
    g_pre_cancel_count = 0;
    g_post_count = 0;
}

// --- UI test window (interactive) ---

void cb_toggle_get(ModContext*, void*, UiControlValue* value) {
    value->bool_value = g_cb_toggle;
}
void cb_toggle_set(ModContext*, void*, const UiControlValue* value) {
    g_cb_toggle = value->bool_value;
}
void cb_number_get(ModContext*, void*, UiControlValue* value) {
    value->int_value = g_cb_number;
}
void cb_number_set(ModContext*, void*, const UiControlValue* value) {
    g_cb_number = value->int_value;
}
void cb_string_get(ModContext*, void*, UiControlValue* value) {
    value->string_value = g_cb_string;
}
void cb_string_set(ModContext*, void*, const UiControlValue* value) {
    std::snprintf(g_cb_string, sizeof(g_cb_string), "%s",
        value->string_value != nullptr ? value->string_value : "");
}

void apply_pride_mode(bool on) {
    if (on && g_style == 0) {
        svc_ui->register_styles_file(mod_ctx, UI_SCOPE_WINDOW, "pride.rcss", &g_style);
    } else if (!on && g_style != 0) {
        svc_ui->unregister_styles(mod_ctx, g_style);
        g_style = 0;
    }
}

void on_pride_changed(
    ModContext*, ConfigVarHandle, const ConfigVarValue* value, const ConfigVarValue*, void*) {
    apply_pride_mode(value->bool_value);
}

void on_close_window_pressed(ModContext*, void*) {
    svc_ui->window_close(mod_ctx, g_test_window);
}

void on_close_top_pressed(ModContext*, void*) {
    svc_ui->close_top_document(mod_ctx);
}

void add_control(UiElementHandle pane, const UiControlDesc& desc) {
    svc_ui->pane_add_control(mod_ctx, pane, &desc, nullptr);
}

ModResult build_window_controls_tab(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    (void)right;  // help_rml and SELECT options render there automatically

    svc_ui->pane_add_section(mod_ctx, left, "Config");
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Pride Mode";
    control.help_rml = "Rainbow-animates window borders and tab highlights everywhere. Bound to "
                       "<span class=\"tip\">prideMode</span>; persists in config.json.";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cfg_pride;
    add_control(left, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = "Test Int";
    control.help_rml = "Bound to <span class=\"tip\">testInt</span>.";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cfg_int;
    control.min = 0;
    control.max = 10000;
    control.step = 100;
    add_control(left, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_STRING;
    control.label = "Test String";
    control.help_rml = "Bound to <span class=\"tip\">testString</span>.";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cfg_string;
    control.max_length = 32;
    add_control(left, control);

    static const char* kChoices[] = {"Ordon", "Faron", "Eldin", "Lanayru"};
    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_SELECT;
    control.label = "Test Choice";
    control.help_rml = "Bound to <span class=\"tip\">testChoice</span> (option index).";
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cfg_choice;
    control.options = kChoices;
    control.option_count = 4;
    add_control(left, control);

    svc_ui->pane_add_section(mod_ctx, left, "Callback-bound");
    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "CB Toggle";
    control.get = cb_toggle_get;
    control.set = cb_toggle_set;
    add_control(left, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = "CB Number";
    control.get = cb_number_get;
    control.set = cb_number_set;
    control.min = -10;
    control.max = 10;
    control.suffix = " pts";
    add_control(left, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_STRING;
    control.label = "CB String";
    control.get = cb_string_get;
    control.set = cb_string_set;
    control.max_length = 32;
    add_control(left, control);

    svc_ui->pane_add_section(mod_ctx, left, "Actions");
    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_BUTTON;
    control.label = "Close (window_close)";
    control.on_pressed = on_close_window_pressed;
    add_control(left, control);

    control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_BUTTON;
    control.label = "Close (close_top_document)";
    control.on_pressed = on_close_top_pressed;
    add_control(left, control);
    return MOD_OK;
}

ModResult build_window_info_tab(
    ModContext*, UiWindowHandle, UiElementHandle left, UiElementHandle right, void*, ModError*) {
    (void)right;
    svc_ui->pane_add_section(mod_ctx, left, "Info");
    svc_ui->pane_add_text(mod_ctx, left,
        "Switching tabs rebuilt this content; stale handles from the previous build are "
        "rejected, not dereferenced.",
        nullptr);
    svc_ui->pane_add_text(mod_ctx, left, "updates: 0", &g_el_win_counter);
    return MOD_OK;
}

ModResult update_window_info_tab(ModContext*, void*, ModError*) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "updates: %d", ++g_window_updates);
    svc_ui->elem_set_text(mod_ctx, g_el_win_counter, buf);
    return MOD_OK;
}

void on_test_window_closed(ModContext*, UiWindowHandle, void*) {
    svc_log->info(mod_ctx, "mod_test: test window closed");
    g_test_window = 0;
}

void on_open_window(ModContext*, void*) {
    if (g_test_window != 0) {
        return;
    }
    UiTabDesc tabs[2] = {UI_TAB_DESC_INIT, UI_TAB_DESC_INIT};
    tabs[0].title = "Controls";
    tabs[0].build = build_window_controls_tab;
    tabs[1].title = "Info";
    tabs[1].build = build_window_info_tab;
    tabs[1].update = update_window_info_tab;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 2;
    desc.rcss = "window.mod-window tab-bar tab { color: #8fd; }";
    desc.on_closed = on_test_window_closed;
    svc_ui->window_push(mod_ctx, &desc, &g_test_window);
}

void on_dialog_proceed(ModContext*, UiDialogHandle, void*) {
    svc_log->info(mod_ctx, "mod_test: dialog Proceed pressed");
}

void on_dialog_dismissed(ModContext*, UiDialogHandle, void*) {
    svc_log->info(mod_ctx, "mod_test: dialog dismissed");
}

void on_open_dialog(ModContext*, void*) {
    UiDialogAction actions[2] = {
        {"Cancel", nullptr, nullptr, false},
        {"Proceed", on_dialog_proceed, nullptr, false},
    };
    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = "Test Dialog";
    desc.body_rml = "Danger variant with an <span class=\"tip\">RML body</span>. B dismisses.";
    desc.variant = UI_DIALOG_DANGER;
    desc.actions = actions;
    desc.action_count = 2;
    desc.on_dismiss = on_dialog_dismissed;
    svc_ui->dialog_push(mod_ctx, &desc, nullptr);
}

struct GfxTestPayload {
    uint32_t magic;
    uint32_t mode;  // 0 = scene (EFB) pass, 1 = 64x64 offscreen pass
};
constexpr uint32_t kGfxPayloadMagic = 0x47465854;  // "GFXT"

void on_gfx_draw(
    ModContext*, const GfxDrawContext* ctx, const void* payload, size_t payloadSize, void*) {
    // Render worker thread: validate the context and set flags, nothing else.
    if (payload == nullptr || payloadSize != sizeof(GfxTestPayload)) {
        return;
    }
    GfxTestPayload data;
    std::memcpy(&data, payload, sizeof(data));
    if (data.magic != kGfxPayloadMagic) {
        return;
    }
    const bool ctxOk = ctx != nullptr && ctx->device != nullptr && ctx->queue != nullptr &&
                       ctx->pass != nullptr && ctx->vertex_buffer != nullptr &&
                       ctx->uniform_buffer != nullptr && ctx->target_width > 0 &&
                       ctx->target_height > 0 && ctx->sample_count >= 1;
    if (data.mode == 0) {
        g_gfx_draw_ctx_ok.store(ctxOk, std::memory_order_release);
        g_gfx_draw_fired.store(true, std::memory_order_release);
    } else {
        g_gfx_offscreen_draw_ok.store(
            ctxOk && ctx->target_width == 64 && ctx->target_height == 64 && ctx->sample_count == 1,
            std::memory_order_release);
    }
}

// A minimal compute shader writing a 4x4 r32float storage texture; exercises the encoder-task
// path end-to-end under Dawn validation.
constexpr const char* kComputeShader = R"(
@group(0) @binding(0) var out_tex: texture_storage_2d<r32float, write>;
@compute @workgroup_size(4, 4)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    textureStore(out_tex, vec2i(id.xy), vec4f(1.0, 0.0, 0.0, 1.0));
}
)";

bool build_compute_resources(WGPUDevice device) {
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = {kComputeShader, WGPU_STRLEN};
    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgsl.chain;
    moduleDesc.label = {"mod_test compute", WGPU_STRLEN};
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    if (module == nullptr) {
        return false;
    }
    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.label = {"mod_test compute pipeline", WGPU_STRLEN};
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = {"cs_main", WGPU_STRLEN};
    g_gfx_compute_pipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);
    wgpuShaderModuleRelease(module);
    if (g_gfx_compute_pipeline == nullptr) {
        return false;
    }
    g_gfx_compute_bgl = wgpuComputePipelineGetBindGroupLayout(g_gfx_compute_pipeline, 0);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.label = {"mod_test compute target", WGPU_STRLEN};
    texDesc.usage = WGPUTextureUsage_StorageBinding;
    texDesc.size = {4, 4, 1};
    texDesc.format = WGPUTextureFormat_R32Float;
    g_gfx_compute_tex = wgpuDeviceCreateTexture(device, &texDesc);
    if (g_gfx_compute_tex == nullptr) {
        return false;
    }
    g_gfx_compute_view = wgpuTextureCreateView(g_gfx_compute_tex, nullptr);
    return g_gfx_compute_view != nullptr;
}

// Runs on the render worker thread with the frame's command encoder.
void on_gfx_compute(
    ModContext*, const GfxComputeContext* ctx, const void* payload, size_t payloadSize, void*) {
    bool ok = ctx != nullptr && ctx->device != nullptr && ctx->queue != nullptr &&
              ctx->encoder != nullptr && ctx->uniform_buffer != nullptr &&
              payloadSize == sizeof(GfxTestPayload) && g_gfx_compute_pipeline != nullptr;
    if (ok) {
        GfxTestPayload data;
        std::memcpy(&data, payload, sizeof(data));
        ok = data.magic == kGfxPayloadMagic;
    }
    if (ok) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.textureView = g_gfx_compute_view;
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = g_gfx_compute_bgl;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &entry;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx->device, &bindGroupDesc);
        if (bindGroup != nullptr) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(ctx->encoder, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, g_gfx_compute_pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
            wgpuBindGroupRelease(bindGroup);
        } else {
            ok = false;
        }
    }
    g_gfx_compute_ok.store(ok, std::memory_order_release);
    g_gfx_compute_fired.store(true, std::memory_order_release);
}

void on_gfx_stage(ModContext*, const GfxStageContext* stageCtx, void*) {
    if (g_gfx_stage_ran) {
        return;
    }
    g_gfx_stage_ran = true;
    g_gfx_stage_ok = stageCtx != nullptr && stageCtx->stage == GFX_STAGE_BEFORE_HUD;

    // Streaming pushes are valid at record time.
    const float verts[8] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
    GfxRange vertRange{0, 0};
    GfxRange uniformRange{0, 0};
    g_gfx_stage_ok =
        g_gfx_stage_ok &&
        svc_gfx->push_verts(mod_ctx, verts, sizeof(verts), 4, &vertRange) == MOD_OK &&
        vertRange.size == sizeof(verts) &&
        svc_gfx->push_uniform(mod_ctx, verts, sizeof(verts), &uniformRange) == MOD_OK &&
        uniformRange.size == sizeof(verts);

    // Scene snapshot with color + depth, then a custom draw into the continuation pass.
    GfxResolveDesc resolveDesc = GFX_RESOLVE_DESC_INIT;
    resolveDesc.depth = true;
    GfxResolvedTargets targets = GFX_RESOLVED_TARGETS_INIT;
    g_gfx_resolve_ok = svc_gfx->resolve_pass(mod_ctx, &resolveDesc, &targets) == MOD_OK &&
                       targets.color != nullptr && targets.depth != nullptr && targets.width > 0 &&
                       targets.height > 0;

    GfxTestPayload payload{kGfxPayloadMagic, 0};
    g_gfx_resolve_ok = g_gfx_resolve_ok && svc_gfx->push_draw(mod_ctx, g_gfx_draw_type, &payload,
                                               sizeof(payload)) == MOD_OK;

    // Offscreen pass round trip; nesting must be rejected while it is open, and so must
    // compute tasks (the encoder-task pass break only handles the EFB).
    g_gfx_create_ok = svc_gfx->create_pass(mod_ctx, 64, 64) == MOD_OK &&
                      svc_gfx->create_pass(mod_ctx, 32, 32) == MOD_UNAVAILABLE;
    GfxTestPayload computePayload{kGfxPayloadMagic, 2};
    g_gfx_compute_push_ok = svc_gfx->push_compute(mod_ctx, g_gfx_compute_type, &computePayload,
                                sizeof(computePayload)) == MOD_UNAVAILABLE;
    payload.mode = 1;
    g_gfx_create_ok = g_gfx_create_ok && svc_gfx->push_draw(mod_ctx, g_gfx_draw_type, &payload,
                                             sizeof(payload)) == MOD_OK;
    GfxResolveDesc colorOnly = GFX_RESOLVE_DESC_INIT;
    GfxResolvedTargets offscreenTargets = GFX_RESOLVED_TARGETS_INIT;
    g_gfx_create_ok = g_gfx_create_ok &&
                      svc_gfx->resolve_pass(mod_ctx, &colorOnly, &offscreenTargets) == MOD_OK &&
                      offscreenTargets.width == 64 && offscreenTargets.height == 64 &&
                      offscreenTargets.color != nullptr;

    // With the EFB pass active again, a real compute task records here and executes on the
    // frame encoder between the split scene passes.
    g_gfx_compute_push_ok =
        g_gfx_compute_push_ok && svc_gfx->push_compute(mod_ctx, g_gfx_compute_type, &computePayload,
                                     sizeof(computePayload)) == MOD_OK;
}

// Column-major (CameraInfo convention) helpers for the camera math self-check.
void camera_mat_mul(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + r] * b[c * 4 + k];
            }
            out[c * 4 + r] = sum;
        }
    }
}

void camera_mat_vec(const float m[16], const float v[4], float out[4]) {
    for (int r = 0; r < 4; ++r) {
        out[r] =
            m[0 * 4 + r] * v[0] + m[1 * 4 + r] * v[1] + m[2 * 4 + r] * v[2] + m[3 * 4 + r] * v[3];
    }
}

// Verifies the WebGPU-convention projection against its documented semantics:
// proj * inv ~= identity, and reversed-Z NDC depth 1/0 unprojects to the
// near/far planes in view space.
bool check_camera_math(const CameraInfo& cam) {
    if (!(cam.near_plane > 0.0f) || !(cam.far_plane > cam.near_plane)) {
        return false;
    }
    float identity[16];
    camera_mat_mul(cam.proj_from_view, cam.view_from_proj, identity);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            const float expected = c == r ? 1.0f : 0.0f;
            if (std::fabs(identity[c * 4 + r] - expected) > 1e-4f) {
                return false;
            }
        }
    }
    const float nearNdc[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const float farNdc[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float nearView[4];
    float farView[4];
    camera_mat_vec(cam.view_from_proj, nearNdc, nearView);
    camera_mat_vec(cam.view_from_proj, farNdc, farView);
    if (nearView[3] == 0.0f || farView[3] == 0.0f) {
        return false;
    }
    const float nearZ = nearView[2] / nearView[3];
    const float farZ = farView[2] / farView[3];
    return std::fabs(nearZ + cam.near_plane) <= cam.near_plane * 1e-3f &&
           std::fabs(farZ + cam.far_plane) <= cam.far_plane * 1e-2f;
}

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    UiControlDesc button = UI_CONTROL_DESC_INIT;
    button.kind = UI_CONTROL_BUTTON;
    button.label = "Open Test Window";
    button.on_pressed = on_open_window;
    add_control(panel, button);

    button = UI_CONTROL_DESC_INIT;
    button.kind = UI_CONTROL_BUTTON;
    button.label = "Open Test Dialog";
    button.on_pressed = on_open_dialog;
    add_control(panel, button);

    svc_ui->pane_add_section(mod_ctx, panel, "Hooks");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "pre-hook fired (posMove)", g_pre_fired, &g_el_pre_badge);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "post-hook fired (posMove)", g_post_fired, &g_el_post_badge);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "replace-hook fired (execute)", g_replace_fired, &g_el_replace_badge);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "arg_ref write + pre cancel (hold L)",
        g_arg_write_ok, &g_el_argwrite_badge);

    char countBuf[64];
    std::snprintf(countBuf, sizeof(countBuf), "pre cancels: %d", g_pre_cancel_count);
    svc_ui->pane_add_text(mod_ctx, panel, countBuf, &g_el_cancel_count);
    std::snprintf(countBuf, sizeof(countBuf), "post calls: %d", g_post_count);
    svc_ui->pane_add_text(mod_ctx, panel, countBuf, &g_el_post_count);

    svc_ui->pane_add_section(mod_ctx, panel, "Resources");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "ResourceService::load (hello.txt)", g_resource_ok, nullptr);
    if (g_resource_text[0] != '\0') {
        svc_ui->pane_add_text(mod_ctx, panel, g_resource_text, nullptr);
    }

    svc_ui->pane_add_section(mod_ctx, panel, "Dependencies");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "mod_test_dep initialized first", g_dep_order_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "deferred service import", g_dep_deferred_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Assets");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "OverlayService add/remove", g_overlay_add_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "overlay DVD read-back", g_overlay_read_ok, &g_el_overlay_read_badge);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "TextureService register data+file", g_texture_reg_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "asset services negative tests", g_asset_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Config");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "ConfigService register/get/set", g_config_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "ConfigService change callbacks", g_config_change_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "ConfigService negative tests", g_config_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Item checks");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "override + resolver + observer", g_item_check_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "ItemService negative tests", g_item_check_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "UI");
    svc_ui->pane_add_badge_row(mod_ctx, panel, "stack queries", g_ui_stack_ok, nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "scoped RCSS registered", g_ui_rcss_ok, nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "UI negative tests", g_ui_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Gfx");
    svc_ui->pane_add_badge_row(mod_ctx, panel, "device info", g_gfx_device_ok, nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "stage + resolve + offscreen",
        g_gfx_stage_ok && g_gfx_resolve_ok && g_gfx_create_ok, nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "draw callback (render worker)",
        g_gfx_draw_fired.load() && g_gfx_draw_ctx_ok.load() && g_gfx_offscreen_draw_ok.load(),
        nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "compute task (render worker)",
        g_gfx_compute_fired.load() && g_gfx_compute_ok.load() && g_gfx_compute_push_ok, nullptr);
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "gfx negative tests", g_gfx_neg_ok && g_gfx_compute_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Camera");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "get_camera + projection math", g_camera_checked && g_camera_ok, nullptr);
    svc_ui->pane_add_badge_row(mod_ctx, panel, "camera negative tests", g_camera_neg_ok, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "API Fields");
    svc_ui->pane_add_badge_row(
        mod_ctx, panel, "mod_dir non-empty", g_mod_dir_snippet[0] != '\0', nullptr);
    svc_ui->pane_add_text(mod_ctx, panel, g_mod_dir_snippet, nullptr);

    button = UI_CONTROL_DESC_INIT;
    button.kind = UI_CONTROL_BUTTON;
    button.label = "Reset Results";
    button.on_pressed = on_reset;
    add_control(panel, button);

    svc_ui->pane_add_text(mod_ctx, panel, "", &g_el_link_angle);
    return MOD_OK;
}

ModResult update_panel(ModContext*, void*, ModError*) {
    svc_ui->elem_set_badge(mod_ctx, g_el_pre_badge, g_pre_fired);
    svc_ui->elem_set_badge(mod_ctx, g_el_post_badge, g_post_fired);
    svc_ui->elem_set_badge(mod_ctx, g_el_replace_badge, g_replace_fired);
    svc_ui->elem_set_badge(mod_ctx, g_el_argwrite_badge, g_arg_write_ok);
    svc_ui->elem_set_badge(mod_ctx, g_el_overlay_read_badge, g_overlay_read_ok);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "pre cancels: %d", g_pre_cancel_count);
    svc_ui->elem_set_text(mod_ctx, g_el_cancel_count, buf);

    std::snprintf(buf, sizeof(buf), "post calls: %d", g_post_count);
    svc_ui->elem_set_text(mod_ctx, g_el_post_count, buf);

    if (g_seen_link) {
        std::snprintf(buf, sizeof(buf), "Link y angle: %d", g_last_link_y_angle);
    } else {
        std::snprintf(buf, sizeof(buf), "Link y angle: waiting");
    }
    svc_ui->elem_set_text(mod_ctx, g_el_link_angle, buf);
    return MOD_OK;
}

}  // namespace

// mod_test_dep optionally imports this, closing a dependency cycle the loader must break.
EXPORT_SERVICE(s_testMainService);

extern "C" {

// ItemService: value override + catch-all resolver chaining + observer + removal round trip,
// plus negative tests. Drives the game-side seam directly via dusk::mods::item_check with
// synthetic names (real sites like "Herding Goats Reward" only fire in gameplay).
bool item_check_test_resolver(
    ModContext*, const ItemCheckInfo* info, uint8_t* outItem, void*) {
    if (std::strcmp(info->name, "mod_test.check") != 0) {
        return false;
    }
    // Prove the chain: the value override applied before us is visible in current_item.
    *outItem = info->current_item + 1;
    return true;
}

void item_check_test_observer(ModContext*, const ItemCheckInfo* info, void*) {
    if (std::strcmp(info->name, "mod_test.check") == 0) {
        g_item_observed = info->current_item;
        g_item_observed_vanilla = info->vanilla_item;
    }
}

void test_item_checks() {
    using dusk::mods::item_check;
    bool ok = item_check("mod_test.check", 5, nullptr) == 5;  // nothing registered yet

    ok = ok && svc_item->set_check_override(mod_ctx, "mod_test.check", 42) == MOD_OK;
    ok = ok && item_check("mod_test.check", 5, nullptr) == 42;

    ItemCheckHandle resolver = 0;
    ok = ok && svc_item->set_check_resolver(
                   mod_ctx, nullptr, item_check_test_resolver, nullptr, &resolver) == MOD_OK &&
         resolver != 0;
    ItemCheckHandle observer = 0;
    ok = ok && svc_item->observe_checks(
                   mod_ctx, item_check_test_observer, nullptr, &observer) == MOD_OK &&
         observer != 0;

    // The catch-all resolver runs after the value override and sees it through current_item;
    // the observer sees the final item and the vanilla one.
    ok = ok && item_check("mod_test.check", 5, nullptr) == 43;
    ok = ok && g_item_observed == 43 && g_item_observed_vanilla == 5;
    // The resolver passes on names it does not recognize.
    ok = ok && item_check("mod_test.other", 7, nullptr) == 7;

    ok = ok && svc_item->clear_check_resolver(mod_ctx, resolver) == MOD_OK;
    ok = ok && svc_item->clear_check_override(mod_ctx, "mod_test.check") == MOD_OK;
    ok = ok && svc_item->unobserve_checks(mod_ctx, observer) == MOD_OK;
    ok = ok && item_check("mod_test.check", 5, nullptr) == 5;
    g_item_check_ok = ok;
    if (ok) {
        svc_log->info(mod_ctx, "ItemService check override/resolver/observer OK");
    } else {
        svc_log->error(mod_ctx, "ItemService check override/resolver/observer FAILED");
    }

    bool neg = svc_item->set_check_override(mod_ctx, "", 1) == MOD_INVALID_ARGUMENT;
    neg = neg && svc_item->set_check_override(mod_ctx, nullptr, 1) == MOD_INVALID_ARGUMENT;
    neg = neg && svc_item->set_check_resolver(mod_ctx, "x", nullptr, nullptr, nullptr) ==
                     MOD_INVALID_ARGUMENT;
    neg = neg && svc_item->clear_check_override(mod_ctx, "mod_test.never") == MOD_INVALID_ARGUMENT;
    neg = neg && svc_item->clear_check_resolver(mod_ctx, 0) == MOD_INVALID_ARGUMENT;
    neg = neg && svc_item->unobserve_checks(mod_ctx, ~UINT64_C(0)) == MOD_INVALID_ARGUMENT;
    g_item_check_neg_ok = neg;
    if (neg) {
        svc_log->info(mod_ctx, "ItemService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "ItemService negative tests FAILED");
    }
}

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, "mod_test initializing");
    svc_log->warn(mod_ctx, "LogService::warn smoke test");
    svc_log->error(mod_ctx, "LogService::error smoke test");
    if (sdk_mtx_exports_ok()) {
        svc_log->info(mod_ctx, "SDK mtx exports OK");
    } else {
        svc_log->error(mod_ctx, "SDK mtx exports FAILED");
    }

    g_dep_order_ok = svc_test_dep->initialized(mod_ctx) == 1;
    if (g_dep_order_ok) {
        svc_log->info(mod_ctx, "dependency ordering OK: mod_test_dep initialized first");
    } else {
        svc_log->error(mod_ctx, "dependency ordering FAILED: mod_test_dep not initialized");
    }
    g_dep_deferred_ok = svc_test_dep_deferred->magic(mod_ctx) == TEST_DEP_DEFERRED_MAGIC;
    if (g_dep_deferred_ok) {
        svc_log->info(mod_ctx, "deferred service import OK");
    } else {
        svc_log->error(mod_ctx, "deferred service import returned wrong value");
    }

    std::snprintf(
        g_mod_dir_snippet, sizeof(g_mod_dir_snippet), "%.60s", svc_host->mod_dir(mod_ctx));

    ResourceBuffer buffer = RESOURCE_BUFFER_INIT;
    ModResult loadResult = svc_resource->load(mod_ctx, "hello.txt", &buffer);
    if (loadResult == MOD_OK) {
        size_t copy =
            buffer.size < sizeof(g_resource_text) - 1 ? buffer.size : sizeof(g_resource_text) - 1;
        std::memcpy(g_resource_text, buffer.data, copy);
        g_resource_text[copy] = '\0';
        while (copy > 0 && g_resource_text[copy - 1] == '\n') {
            g_resource_text[--copy] = '\0';
        }
        svc_resource->free(mod_ctx, &buffer);
        g_resource_ok = true;

        char logBuf[320];
        std::snprintf(logBuf, sizeof(logBuf), "ResourceService::load OK: \"%s\"", g_resource_text);
        svc_log->info(mod_ctx, logBuf);
    } else {
        svc_log->error(mod_ctx, "ResourceService::load FAILED for hello.txt");
    }

    ResourceBuffer missing = RESOURCE_BUFFER_INIT;
    loadResult = svc_resource->load(mod_ctx, "does_not_exist.bin", &missing);
    if (loadResult == MOD_UNAVAILABLE) {
        svc_log->info(mod_ctx, "missing resource correctly returned MOD_UNAVAILABLE");
    } else if (loadResult == MOD_OK) {
        svc_log->error(mod_ctx, "missing resource unexpectedly returned data");
        svc_resource->free(mod_ctx, &missing);
    }

    // OverlayService: a memory-backed overlay (read back once the game loop runs), a
    // bundle-backed overlay, and a register/remove round trip.
    OverlayHandle overlayHandle = 0;
    ModResult overlayResult = svc_overlay->add_buffer(mod_ctx, "/mod_test_runtime.txt",
        kRuntimeOverlayData, sizeof(kRuntimeOverlayData) - 1, &overlayHandle);
    g_overlay_add_ok = overlayResult == MOD_OK && overlayHandle != 0;
    overlayResult = svc_overlay->add_file(mod_ctx, "/mod_test_file.txt", "res/hello.txt", nullptr);
    g_overlay_add_ok = g_overlay_add_ok && overlayResult == MOD_OK;

    OverlayHandle removeHandle = 0;
    overlayResult = svc_overlay->add_buffer(mod_ctx, "/mod_test_tmp.txt", "x", 1, &removeHandle);
    g_overlay_add_ok = g_overlay_add_ok && overlayResult == MOD_OK &&
                       svc_overlay->remove(mod_ctx, removeHandle) == MOD_OK;
    if (g_overlay_add_ok) {
        svc_log->info(mod_ctx, "OverlayService add/remove OK");
    } else {
        svc_log->error(mod_ctx, "OverlayService add/remove FAILED");
    }

    // TextureService: raw data with a pointer key, an encoded file from the bundle, and an
    // unregister round trip.
    TextureKey texKey = TEXTURE_KEY_INIT;
    texKey.kind = TEXTURE_KEY_POINTER;
    texKey.pointer = s_rawTexture;
    TextureData texData = TEXTURE_DATA_INIT;
    texData.data = s_rawTexture;
    texData.size = sizeof(s_rawTexture);
    texData.width = 8;
    texData.height = 8;
    texData.gx_format = GX_TF_RGBA8_PC;
    TextureReplacementHandle texHandle = 0;
    ModResult texResult = svc_texture->register_data(mod_ctx, &texKey, &texData, &texHandle);
    g_texture_reg_ok = texResult == MOD_OK && texHandle != 0;
    if (g_texture_reg_ok) {
        svc_log->info(mod_ctx, "TextureService register_data OK");
        g_texture_reg_ok = svc_texture->unregister(mod_ctx, texHandle) == MOD_OK;
    } else {
        svc_log->error(mod_ctx, "TextureService register_data FAILED");
    }

    texResult = svc_texture->register_file(mod_ctx, "res/tex1_16x16_$_6.png", nullptr);
    g_texture_reg_ok = g_texture_reg_ok && texResult == MOD_OK;
    if (texResult == MOD_OK) {
        svc_log->info(mod_ctx, "TextureService register_file OK");
    } else {
        svc_log->error(mod_ctx, "TextureService register_file FAILED");
    }

    // Negative tests: both services must reject bad paths, missing files and bogus handles.
    g_asset_neg_ok =
        svc_overlay->add_file(mod_ctx, "no_leading_slash.txt", "res/hello.txt", nullptr) ==
            MOD_INVALID_ARGUMENT &&
        svc_overlay->add_file(mod_ctx, "/x.txt", "res/does_not_exist.bin", nullptr) ==
            MOD_UNAVAILABLE &&
        svc_overlay->remove(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT &&
        svc_texture->register_file(mod_ctx, "res/hello.txt", nullptr) == MOD_INVALID_ARGUMENT &&
        svc_texture->register_file(mod_ctx, "res/tex1_4x4_$_6.png", nullptr) == MOD_UNAVAILABLE &&
        svc_texture->register_data(mod_ctx, &texKey, nullptr, nullptr) == MOD_INVALID_ARGUMENT &&
        svc_texture->unregister(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT;
    if (g_asset_neg_ok) {
        svc_log->info(mod_ctx, "asset services negative tests OK");
    } else {
        svc_log->error(mod_ctx, "asset services negative tests FAILED");
    }

    // ConfigService: registration + typed get/set round trips, an unregister/re-register cycle,
    // change callbacks, a persistence probe (asserted across runs), and negative tests.
    ConfigVarHandle cfgFloat = 0;
    ConfigVarDesc cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testFlag";
    cfgDesc.type = CONFIG_VAR_BOOL;
    cfgDesc.default_bool = true;
    g_config_ok =
        svc_config->register_var(mod_ctx, &cfgDesc, &g_cfg_flag) == MOD_OK && g_cfg_flag != 0;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testInt";
    cfgDesc.type = CONFIG_VAR_INT;
    cfgDesc.default_int = 42;
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &g_cfg_int) == MOD_OK;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testFloat";
    cfgDesc.type = CONFIG_VAR_FLOAT;
    cfgDesc.default_float = 1.5;
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &cfgFloat) == MOD_OK;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testString";
    cfgDesc.type = CONFIG_VAR_STRING;
    cfgDesc.default_string = "hello";
    g_config_ok =
        g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &g_cfg_string) == MOD_OK;

    // Backs the SELECT control in the UI test window (value = option index).
    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testChoice";
    cfgDesc.type = CONFIG_VAR_INT;
    g_config_ok =
        g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &g_cfg_choice) == MOD_OK;

    // Values persist across sessions, so assert the set/get round trip, not the defaults.
    bool cfgBoolValue = true;
    int64_t cfgIntValue = 0;
    double cfgFloatValue = 0.0;
    char cfgStringBuf[8] = {};
    size_t cfgStringLength = 0;
    g_config_ok =
        g_config_ok && svc_config->set_bool(mod_ctx, g_cfg_flag, false) == MOD_OK &&
        svc_config->get_bool(mod_ctx, g_cfg_flag, &cfgBoolValue) == MOD_OK && !cfgBoolValue &&
        svc_config->set_int(mod_ctx, g_cfg_int, 1234) == MOD_OK &&
        svc_config->get_int(mod_ctx, g_cfg_int, &cfgIntValue) == MOD_OK && cfgIntValue == 1234 &&
        svc_config->set_float(mod_ctx, cfgFloat, 2.25) == MOD_OK &&
        svc_config->get_float(mod_ctx, cfgFloat, &cfgFloatValue) == MOD_OK &&
        cfgFloatValue == 2.25 && svc_config->set_string(mod_ctx, g_cfg_string, "abc") == MOD_OK &&
        // Length query with a null buffer, then an exact-size read.
        svc_config->get_string(mod_ctx, g_cfg_string, nullptr, 0, &cfgStringLength) == MOD_OK &&
        cfgStringLength == 3 &&
        svc_config->get_string(mod_ctx, g_cfg_string, cfgStringBuf, 4, nullptr) == MOD_OK &&
        std::strcmp(cfgStringBuf, "abc") == 0;

    // Unregister releases the name for a later registration (the reload path in miniature).
    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testTemp";
    cfgDesc.type = CONFIG_VAR_BOOL;
    ConfigVarHandle cfgTemp = 0;
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &cfgTemp) == MOD_OK &&
                  svc_config->unregister_var(mod_ctx, cfgTemp) == MOD_OK &&
                  svc_config->register_var(mod_ctx, &cfgDesc, &cfgTemp) == MOD_OK;
    if (g_config_ok) {
        svc_log->info(mod_ctx, "ConfigService register/get/set OK");
    } else {
        svc_log->error(mod_ctx, "ConfigService register/get/set FAILED");
    }

    // Own writes fire the callback synchronously; unsubscribing stops delivery.
    ConfigSubscriptionHandle cfgSub = 0;
    g_config_change_ok =
        svc_config->subscribe(
            mod_ctx, g_cfg_int, on_config_changed, &g_config_change_count, &cfgSub) == MOD_OK &&
        cfgSub != 0 && svc_config->set_int(mod_ctx, g_cfg_int, 5678) == MOD_OK &&
        g_config_change_count == 1 && g_config_prev_value == 1234 && g_config_curr_value == 5678 &&
        // Writes that don't change the value don't notify.
        svc_config->set_int(mod_ctx, g_cfg_int, 5678) == MOD_OK && g_config_change_count == 1 &&
        svc_config->unsubscribe(mod_ctx, cfgSub) == MOD_OK &&
        svc_config->set_int(mod_ctx, g_cfg_int, 42) == MOD_OK && g_config_change_count == 1;
    if (g_config_change_ok) {
        svc_log->info(mod_ctx, "ConfigService change callbacks OK");
    } else {
        char dbgBuf[128];
        std::snprintf(dbgBuf, sizeof(dbgBuf),
            "ConfigService change callbacks FAILED (count=%d prev=%lld sub=%llu)",
            g_config_change_count, static_cast<long long>(g_config_prev_value),
            static_cast<unsigned long long>(cfgSub));
        svc_log->error(mod_ctx, dbgBuf);
    }

    // Increments once per session; a second run must log the value the first run stored.
    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "persistCounter";
    cfgDesc.type = CONFIG_VAR_INT;
    ConfigVarHandle cfgPersist = 0;
    int64_t persistCount = 0;
    if (svc_config->register_var(mod_ctx, &cfgDesc, &cfgPersist) == MOD_OK &&
        svc_config->get_int(mod_ctx, cfgPersist, &persistCount) == MOD_OK)
    {
        char logBuf[64];
        std::snprintf(logBuf, sizeof(logBuf), "ConfigService persisted counter = %lld",
            static_cast<long long>(persistCount));
        svc_log->info(mod_ctx, logBuf);
        svc_config->set_int(mod_ctx, cfgPersist, persistCount + 1);
    } else {
        svc_log->error(mod_ctx, "ConfigService persistence probe FAILED");
    }

    // Negative tests: bad fragments, reserved/duplicate names, type mismatches, small buffers
    // and bogus handles must all be rejected.
    ConfigVarDesc cfgBadDesc = CONFIG_VAR_DESC_INIT;
    cfgBadDesc.name = "bad.name";
    ConfigVarDesc cfgEmptyDesc = CONFIG_VAR_DESC_INIT;
    cfgEmptyDesc.name = "";
    ConfigVarDesc cfgReservedDesc = CONFIG_VAR_DESC_INIT;
    cfgReservedDesc.name = "enabled";  // collides with the loader's mod.<id>.enabled
    ConfigVarDesc cfgDupDesc = CONFIG_VAR_DESC_INIT;
    cfgDupDesc.name = "testFlag";
    char cfgTinyBuf[2];
    g_config_neg_ok =
        svc_config->register_var(mod_ctx, nullptr, nullptr) == MOD_INVALID_ARGUMENT &&
        svc_config->register_var(mod_ctx, &cfgBadDesc, nullptr) == MOD_INVALID_ARGUMENT &&
        svc_config->register_var(mod_ctx, &cfgEmptyDesc, nullptr) == MOD_INVALID_ARGUMENT &&
        svc_config->register_var(mod_ctx, &cfgReservedDesc, nullptr) == MOD_CONFLICT &&
        svc_config->register_var(mod_ctx, &cfgDupDesc, nullptr) == MOD_CONFLICT &&
        svc_config->get_int(mod_ctx, g_cfg_flag, &cfgIntValue) == MOD_INVALID_ARGUMENT &&
        svc_config->get_string(mod_ctx, g_cfg_string, cfgTinyBuf, sizeof(cfgTinyBuf), nullptr) ==
            MOD_INVALID_ARGUMENT &&
        svc_config->set_bool(mod_ctx, UINT64_C(0xdead), true) == MOD_INVALID_ARGUMENT &&
        svc_config->unregister_var(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT &&
        svc_config->unsubscribe(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT;
    if (g_config_neg_ok) {
        svc_log->info(mod_ctx, "ConfigService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "ConfigService negative tests FAILED");
    }

    test_item_checks();

    ModResult result = dusk::mods::hook_add_pre<&daAlink_c::posMove>(svc_hook, on_pos_move_pre);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register pre hook");
    }
    result = dusk::mods::hook_add_post<&daAlink_c::posMove>(svc_hook, on_pos_move_post);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register post hook");
    }
    result = dusk::mods::hook_set_replace<&daAlink_c::execute>(svc_hook, on_execute_replace);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register replace hook");
    }
    result = dusk::mods::hook_add_pre<&fapGm_Execute>(svc_hook, on_game_loop_pre);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register game loop hook");
    }

    // HookService v1.1: by-name symbol resolution through the build-keyed manifest.
    {
        void* addr = nullptr;
        uint32_t flags = 0;
        ModResult r = svc_hook->resolve(mod_ctx, "GXSetProjection", &addr, &flags);
        if (r == MOD_UNSUPPORTED) {
            svc_log->warn(mod_ctx, "HookService resolve: no symbol manifest (skipped)");
        } else {
            bool ok = r == MOD_OK && addr != nullptr && (flags & HOOK_SYMBOL_CODE) != 0;
            void* dummy = nullptr;
            ok = ok && svc_hook->resolve(mod_ctx, "dusk_no_such_symbol_xyzzy", &dummy, nullptr) ==
                           MOD_UNAVAILABLE;
            ok = ok && svc_hook->resolve(mod_ctx, nullptr, &dummy, nullptr) == MOD_INVALID_ARGUMENT;
            ok = ok && svc_hook->resolve(mod_ctx, "GXSetProjection", nullptr, nullptr) ==
                           MOD_INVALID_ARGUMENT;
            if (ok) {
                svc_log->info(mod_ctx, "HookService resolve OK");
            } else {
                svc_log->error(mod_ctx, "HookService resolve FAILED");
            }
        }
    }

    if (svc_game->abi_tag != nullptr &&
        (svc_game->build_id_len == 0 || svc_game->build_id != nullptr))
    {
        char gcBuf[96];
        std::snprintf(gcBuf, sizeof(gcBuf), "GameService OK: epoch %u, abi %s, build id len %u",
            static_cast<unsigned>(svc_game->header.major_version), svc_game->abi_tag,
            static_cast<unsigned>(svc_game->build_id_len));
        svc_log->info(mod_ctx, gcBuf);
    } else {
        svc_log->error(mod_ctx, "GameService FAILED: invalid payload");
    }

    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = build_panel;
    panelDesc.update = update_panel;
    result = svc_ui->register_mods_panel(mod_ctx, &panelDesc);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register UI panel");
    }

    // Stack queries work at init (don't assert the visibility state itself: it depends on
    // which host documents are open when mods initialize).
    bool anyVisible = false;
    g_ui_stack_ok = svc_ui->is_any_document_visible(mod_ctx, &anyVisible) == MOD_OK &&
                    svc_ui->is_any_document_visible(mod_ctx, nullptr) == MOD_INVALID_ARGUMENT;
    if (g_ui_stack_ok) {
        svc_log->info(mod_ctx, "UiService stack queries OK");
    } else {
        svc_log->error(mod_ctx, "UiService stack queries FAILED");
    }

    // Scoped styles: register/unregister round trips from a string and from a bundle file.
    // The persistent visual demo is Pride Mode below, driven by its cvar.
    UiStyleHandle tempStyle = 0;
    UiStyleHandle tempFileStyle = 0;
    g_ui_rcss_ok =
        svc_ui->register_styles(mod_ctx, UI_SCOPE_OVERLAY, "toast { }", &tempStyle) == MOD_OK &&
        tempStyle != 0 && svc_ui->unregister_styles(mod_ctx, tempStyle) == MOD_OK &&
        svc_ui->register_styles_file(
            mod_ctx, UI_SCOPE_GRAPHICS_TUNER, "pride.rcss", &tempFileStyle) == MOD_OK &&
        svc_ui->unregister_styles(mod_ctx, tempFileStyle) == MOD_OK;
    if (g_ui_rcss_ok) {
        svc_log->info(mod_ctx, "UiService styles registered OK");
    } else {
        svc_log->error(mod_ctx, "UiService styles FAILED");
    }

    // Pride Mode: cvar-backed toggle in the panel; the change callback registers or
    // unregisters the animated style, restyling every open window live. Values applied
    // from config.json at registration are silent, so apply the starting state by hand.
    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "prideMode";
    cfgDesc.type = CONFIG_VAR_BOOL;
    if (svc_config->register_var(mod_ctx, &cfgDesc, &g_cfg_pride) == MOD_OK) {
        svc_config->subscribe(mod_ctx, g_cfg_pride, on_pride_changed, nullptr, nullptr);
        bool prideOn = false;
        svc_config->get_bool(mod_ctx, g_cfg_pride, &prideOn);
        apply_pride_mode(prideOn);
    }

    // Negative tests: fabricated/stale handles, out-of-range enums, malformed descs and
    // missing files must be rejected. Expected host error lines: stale text handle, stale
    // dialog handle, stale style handle, missing styles file.
    UiControlDesc badSelect = UI_CONTROL_DESC_INIT;
    badSelect.kind = UI_CONTROL_SELECT;
    badSelect.label = "bad";
    badSelect.get = cb_number_get;
    badSelect.set = cb_number_set;                 // SELECT without options is invalid
    UiWindowDesc badWindow = UI_WINDOW_DESC_INIT;  // no tabs
    const uint64_t bogus = (UINT64_C(1) << 32) | UINT64_C(0xdead);
    g_ui_neg_ok =
        svc_ui->elem_set_text(mod_ctx, bogus, "x") == MOD_INVALID_ARGUMENT &&
        svc_ui->dialog_close(mod_ctx, bogus) == MOD_INVALID_ARGUMENT &&
        svc_ui->unregister_styles(mod_ctx, bogus) == MOD_INVALID_ARGUMENT &&
        svc_ui->register_styles(mod_ctx, static_cast<UiStyleScope>(99), "div { }", nullptr) ==
            MOD_INVALID_ARGUMENT &&
        svc_ui->register_styles_file(mod_ctx, UI_SCOPE_WINDOW, "does_not_exist.rcss", nullptr) ==
            MOD_UNAVAILABLE &&
        svc_ui->pane_add_control(mod_ctx, bogus, &badSelect, nullptr) == MOD_INVALID_ARGUMENT &&
        svc_ui->window_push(mod_ctx, &badWindow, nullptr) == MOD_INVALID_ARGUMENT;
    if (g_ui_neg_ok) {
        svc_log->info(mod_ctx, "UiService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "UiService negative tests FAILED");
    }

    // GfxService: device info + proc lookup, draw type/stage hook registration round trips, and
    // negatives. ModLoader::init runs before the frame loop, so no render pass is active here:
    // the pass-scoped calls must fail with MOD_UNAVAILABLE. The pass-scoped positive tests run
    // in the BEFORE_HUD stage callback and are reported from mod_update.
    GfxDeviceInfo gfxInfo = GFX_DEVICE_INFO_INIT;
    g_gfx_device_ok = svc_gfx->get_device_info(mod_ctx, &gfxInfo) == MOD_OK &&
                      gfxInfo.device != nullptr && gfxInfo.queue != nullptr &&
                      gfxInfo.color_format != WGPUTextureFormat_Undefined &&
                      gfxInfo.sample_count >= 1 &&
                      svc_gfx->get_proc_address(mod_ctx, "wgpuDeviceCreateBuffer") != nullptr;
    if (g_gfx_device_ok) {
        svc_log->info(mod_ctx, "GfxService device info OK");
    } else {
        svc_log->error(mod_ctx, "GfxService device info FAILED");
    }

    GfxDrawTypeDesc gfxDrawDesc = GFX_DRAW_TYPE_DESC_INIT;
    gfxDrawDesc.label = "mod_test draw";
    gfxDrawDesc.draw = on_gfx_draw;
    ModResult gfxResult = svc_gfx->register_draw_type(mod_ctx, &gfxDrawDesc, &g_gfx_draw_type);
    if (gfxResult != MOD_OK) {
        return require_ok(gfxResult, error, "failed to register gfx draw type");
    }
    GfxStageHookDesc gfxStageDesc = GFX_STAGE_HOOK_DESC_INIT;
    gfxStageDesc.callback = on_gfx_stage;
    gfxResult = svc_gfx->register_stage_hook(
        mod_ctx, GFX_STAGE_BEFORE_HUD, &gfxStageDesc, &g_gfx_stage_hook);
    if (gfxResult != MOD_OK) {
        return require_ok(gfxResult, error, "failed to register gfx stage hook");
    }

    // Unregister releases the slot for reuse (generational handles, the reload path in
    // miniature) — for both draw types and stage hooks.
    GfxDrawTypeHandle gfxTempDraw = 0;
    GfxStageHookHandle gfxTempStage = 0;
    bool gfxCycleOk = svc_gfx->register_draw_type(mod_ctx, &gfxDrawDesc, &gfxTempDraw) == MOD_OK &&
                      svc_gfx->unregister_draw_type(mod_ctx, gfxTempDraw) == MOD_OK &&
                      svc_gfx->register_draw_type(mod_ctx, &gfxDrawDesc, &gfxTempDraw) == MOD_OK &&
                      svc_gfx->unregister_draw_type(mod_ctx, gfxTempDraw) == MOD_OK &&
                      svc_gfx->register_stage_hook(
                          mod_ctx, GFX_STAGE_AFTER_HUD, &gfxStageDesc, &gfxTempStage) == MOD_OK &&
                      svc_gfx->unregister_stage_hook(mod_ctx, gfxTempStage) == MOD_OK;

    // Negative tests. Expected host error lines: game-owned/no-pass rejections come back as
    // MOD_UNAVAILABLE with aurora::gfx warnings; the stale handles log dusk::mods::gfx errors.
    GfxTestPayload gfxPayload{kGfxPayloadMagic, 0};
    uint8_t gfxBigPayload[GFX_INLINE_DRAW_PAYLOAD_SIZE + 1] = {};
    GfxResolveDesc gfxResolveDesc = GFX_RESOLVE_DESC_INIT;
    GfxResolvedTargets gfxTargets = GFX_RESOLVED_TARGETS_INIT;
    GfxDrawTypeDesc gfxBadDesc = GFX_DRAW_TYPE_DESC_INIT;  // draw callback left null
    const uint64_t gfxBogus = (UINT64_C(1) << 32) | UINT64_C(0xdead);
    g_gfx_neg_ok =
        gfxCycleOk &&
        svc_gfx->push_draw(mod_ctx, g_gfx_draw_type, &gfxPayload, sizeof(gfxPayload)) ==
            MOD_UNAVAILABLE &&
        svc_gfx->resolve_pass(mod_ctx, &gfxResolveDesc, &gfxTargets) == MOD_UNAVAILABLE &&
        svc_gfx->create_pass(mod_ctx, 64, 64) == MOD_UNAVAILABLE &&
        svc_gfx->push_draw(mod_ctx, g_gfx_draw_type, gfxBigPayload, sizeof(gfxBigPayload)) ==
            MOD_INVALID_ARGUMENT &&
        svc_gfx->register_draw_type(mod_ctx, &gfxBadDesc, &gfxTempDraw) == MOD_INVALID_ARGUMENT &&
        svc_gfx->register_stage_hook(mod_ctx, static_cast<GfxStage>(99), &gfxStageDesc,
            &gfxTempStage) == MOD_INVALID_ARGUMENT &&
        svc_gfx->resolve_pass(mod_ctx, nullptr, &gfxTargets) == MOD_INVALID_ARGUMENT &&
        svc_gfx->unregister_draw_type(mod_ctx, gfxBogus) == MOD_INVALID_ARGUMENT &&
        svc_gfx->unregister_stage_hook(mod_ctx, gfxBogus) == MOD_INVALID_ARGUMENT;
    if (g_gfx_neg_ok) {
        svc_log->info(mod_ctx, "GfxService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "GfxService negative tests FAILED");
    }

    // Compute/encoder tasks (gfx minor 1): resources + registration at init, the positive
    // push runs in the stage callback (the true no-pass window is right here).
    if (!build_compute_resources(gfxInfo.device)) {
        return dusk::mods::set_error(error, MOD_ERROR, "failed to create compute test resources");
    }
    GfxComputeTypeDesc computeDesc = GFX_COMPUTE_TYPE_DESC_INIT;
    computeDesc.label = "mod_test compute";
    computeDesc.callback = on_gfx_compute;
    gfxResult = svc_gfx->register_compute_type(mod_ctx, &computeDesc, &g_gfx_compute_type);
    if (gfxResult != MOD_OK) {
        return require_ok(gfxResult, error, "failed to register gfx compute type");
    }
    GfxComputeTypeHandle gfxTempCompute = 0;
    GfxComputeTypeDesc gfxBadComputeDesc = GFX_COMPUTE_TYPE_DESC_INIT;  // callback left null
    GfxTestPayload gfxComputePayload{kGfxPayloadMagic, 2};
    g_gfx_compute_neg_ok =
        svc_gfx->register_compute_type(mod_ctx, &computeDesc, &gfxTempCompute) == MOD_OK &&
        svc_gfx->unregister_compute_type(mod_ctx, gfxTempCompute) == MOD_OK &&
        svc_gfx->push_compute(mod_ctx, g_gfx_compute_type, &gfxComputePayload,
            sizeof(gfxComputePayload)) == MOD_UNAVAILABLE &&
        svc_gfx->register_compute_type(mod_ctx, &gfxBadComputeDesc, &gfxTempCompute) ==
            MOD_INVALID_ARGUMENT &&
        svc_gfx->push_compute(mod_ctx, g_gfx_compute_type, gfxBigPayload, sizeof(gfxBigPayload)) ==
            MOD_INVALID_ARGUMENT &&
        svc_gfx->unregister_compute_type(mod_ctx, gfxBogus) == MOD_INVALID_ARGUMENT;
    if (g_gfx_compute_neg_ok) {
        svc_log->info(mod_ctx, "GfxService compute negative tests OK");
    } else {
        svc_log->error(mod_ctx, "GfxService compute negative tests FAILED");
    }

    // CameraService: no camera exists before the frame loop, so get_camera must be
    // MOD_UNAVAILABLE here. The positive check (matrix self-consistency against a live
    // camera) polls from mod_update until the first camera frame.
    CameraInfo camInfo = CAMERA_INFO_INIT;
    CameraInfo camSmall = CAMERA_INFO_INIT;
    camSmall.struct_size = 4;
    g_camera_neg_ok = svc_camera->get_camera(mod_ctx, &camInfo) == MOD_UNAVAILABLE &&
                      svc_camera->get_camera(mod_ctx, &camSmall) == MOD_INVALID_ARGUMENT &&
                      svc_camera->get_camera(mod_ctx, nullptr) == MOD_INVALID_ARGUMENT;
    if (g_camera_neg_ok) {
        svc_log->info(mod_ctx, "CameraService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "CameraService negative tests FAILED");
    }

    g_initialized = 1;
    svc_log->info(mod_ctx, "mod_test ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    ++g_ticks;

    // The gfx stage callback runs during the painter, the draw callbacks on the render worker;
    // report their (headless-checkable) results once from here.
    if (!g_gfx_logged_stage && g_gfx_stage_ran) {
        g_gfx_logged_stage = true;
        if (g_gfx_stage_ok) {
            svc_log->info(mod_ctx, "GfxService stage hook fired OK");
        } else {
            svc_log->error(mod_ctx, "GfxService stage hook FAILED");
        }
        if (g_gfx_resolve_ok) {
            svc_log->info(mod_ctx, "GfxService resolve_pass OK");
        } else {
            svc_log->error(mod_ctx, "GfxService resolve_pass FAILED");
        }
        if (g_gfx_create_ok) {
            svc_log->info(mod_ctx, "GfxService create_pass round trip OK");
        } else {
            svc_log->error(mod_ctx, "GfxService create_pass round trip FAILED");
        }
    }
    if (!g_camera_checked) {
        CameraInfo cam = CAMERA_INFO_INIT;
        if (svc_camera->get_camera(mod_ctx, &cam) == MOD_OK) {
            g_camera_checked = true;
            g_camera_ok = check_camera_math(cam);
            g_camera_near = cam.near_plane;
            g_camera_far = cam.far_plane;
            char camBuf[96];
            if (g_camera_ok) {
                std::snprintf(camBuf, sizeof(camBuf),
                    "CameraService get_camera OK (near=%.1f far=%.1f)", cam.near_plane,
                    cam.far_plane);
                svc_log->info(mod_ctx, camBuf);
            } else {
                svc_log->error(mod_ctx, "CameraService get_camera math check FAILED");
            }
        }
    }
    if (!g_gfx_logged_draw && g_gfx_draw_fired.load(std::memory_order_acquire)) {
        // The offscreen draw encodes in the same frame but may trail by a poll; give it a
        // moment before reporting.
        const bool offscreenOk = g_gfx_offscreen_draw_ok.load(std::memory_order_acquire);
        if (offscreenOk || ++g_gfx_draw_wait_ticks > 120) {
            g_gfx_logged_draw = true;
            if (g_gfx_draw_ctx_ok.load(std::memory_order_acquire) && offscreenOk) {
                svc_log->info(mod_ctx, "GfxService draw callback fired OK");
            } else {
                svc_log->error(mod_ctx, "GfxService draw callback FAILED");
            }
        }
    }
    if (!g_gfx_logged_compute && g_gfx_compute_fired.load(std::memory_order_acquire)) {
        g_gfx_logged_compute = true;
        if (g_gfx_compute_ok.load(std::memory_order_acquire) && g_gfx_compute_push_ok) {
            svc_log->info(mod_ctx, "GfxService compute task fired OK");
        } else {
            svc_log->error(mod_ctx, "GfxService compute task FAILED");
        }
    }
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    char logBuf[96];
    std::snprintf(logBuf, sizeof(logBuf), "mod_test unloaded after %d ticks", g_ticks);
    svc_log->info(mod_ctx, logBuf);

    g_el_pre_badge = g_el_post_badge = g_el_replace_badge = 0;
    g_el_argwrite_badge = g_el_cancel_count = g_el_post_count = 0;
    g_el_link_angle = g_el_overlay_read_badge = 0;
    g_cfg_flag = g_cfg_int = g_cfg_string = g_cfg_choice = g_cfg_pride = 0;
    g_style = g_test_window = g_el_win_counter = 0;
    g_gfx_draw_type = g_gfx_stage_hook = 0;
    g_gfx_compute_type = 0;
    if (g_gfx_compute_view != nullptr) {
        wgpuTextureViewRelease(g_gfx_compute_view);
        g_gfx_compute_view = nullptr;
    }
    if (g_gfx_compute_tex != nullptr) {
        wgpuTextureRelease(g_gfx_compute_tex);
        g_gfx_compute_tex = nullptr;
    }
    if (g_gfx_compute_bgl != nullptr) {
        wgpuBindGroupLayoutRelease(g_gfx_compute_bgl);
        g_gfx_compute_bgl = nullptr;
    }
    if (g_gfx_compute_pipeline != nullptr) {
        wgpuComputePipelineRelease(g_gfx_compute_pipeline);
        g_gfx_compute_pipeline = nullptr;
    }
    return MOD_OK;
}
}
