// Tests every feature of the Dusklight mod API. Results are shown in the mod tab.

#include "d/actor/d_a_alink.h"
#include "dolphin/dvd.h"
#include "f_ap/f_ap_game.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.h"
#include "mods/svc/host.h"
#include "mods/svc/log.h"
#include "mods/svc/overlay.h"
#include "mods/svc/resource.h"
#include "mods/svc/texture.h"
#include "mods/svc/ui.h"
#include "test_services.h"

DEFINE_MOD();
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(TextureService, svc_texture);
IMPORT_SERVICE(ConfigService, svc_config);
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
int g_config_change_count = 0;
int64_t g_config_prev_value = -1;

void on_config_changed(ModContext*, ConfigVarHandle, const ConfigVarValue* previous, void* userData) {
    ++*static_cast<int*>(userData);
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

ElemHandle g_el_pre_badge = nullptr;
ElemHandle g_el_post_badge = nullptr;
ElemHandle g_el_replace_badge = nullptr;
ElemHandle g_el_argwrite_badge = nullptr;
ElemHandle g_el_cancel_count = nullptr;
ElemHandle g_el_post_count = nullptr;
ElemHandle g_el_link_angle = nullptr;
ElemHandle g_el_overlay_read_badge = nullptr;

ModResult require_ok(const ModResult result, ModError* error, const char* message) {
    if (result != MOD_OK) {
        return dusk::mods::set_error(error, result, message);
    }
    return MOD_OK;
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
            g_overlay_read_ok = read == expected && std::memcmp(buf, kRuntimeOverlayData, expected) == 0;
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

ModResult build_panel(ModContext*, PanelHandle panel, void*, ModError*) {
    ElemHandle unused = nullptr;

    svc_ui->panel_add_section(mod_ctx, panel, "Hooks");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "pre-hook fired (posMove)", g_pre_fired, &g_el_pre_badge);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "post-hook fired (posMove)", g_post_fired, &g_el_post_badge);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "replace-hook fired (execute)", g_replace_fired, &g_el_replace_badge);
    svc_ui->panel_add_badge_row(mod_ctx, panel, "arg_ref write + pre cancel (hold L)",
        g_arg_write_ok, &g_el_argwrite_badge);

    char countBuf[64];
    std::snprintf(countBuf, sizeof(countBuf), "pre cancels: %d", g_pre_cancel_count);
    svc_ui->panel_add_dyn_text(mod_ctx, panel, countBuf, &g_el_cancel_count);
    std::snprintf(countBuf, sizeof(countBuf), "post calls: %d", g_post_count);
    svc_ui->panel_add_dyn_text(mod_ctx, panel, countBuf, &g_el_post_count);

    svc_ui->panel_add_section(mod_ctx, panel, "Resources");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "ResourceService::load (hello.txt)", g_resource_ok, &unused);
    if (g_resource_text[0] != '\0') {
        svc_ui->panel_add_dyn_text(mod_ctx, panel, g_resource_text, &unused);
    }

    svc_ui->panel_add_section(mod_ctx, panel, "Dependencies");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "mod_test_dep initialized first", g_dep_order_ok, &unused);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "deferred service import", g_dep_deferred_ok, &unused);

    svc_ui->panel_add_section(mod_ctx, panel, "Assets");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "OverlayService add/remove", g_overlay_add_ok, &unused);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "overlay DVD read-back", g_overlay_read_ok, &g_el_overlay_read_badge);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "TextureService register data+file", g_texture_reg_ok, &unused);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "asset services negative tests", g_asset_neg_ok, &unused);

    svc_ui->panel_add_section(mod_ctx, panel, "Config");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "ConfigService register/get/set", g_config_ok, &unused);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "ConfigService change callbacks", g_config_change_ok, &unused);
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "ConfigService negative tests", g_config_neg_ok, &unused);

    svc_ui->panel_add_section(mod_ctx, panel, "API Fields");
    svc_ui->panel_add_badge_row(
        mod_ctx, panel, "mod_dir non-empty", g_mod_dir_snippet[0] != '\0', &unused);
    svc_ui->panel_add_dyn_text(mod_ctx, panel, g_mod_dir_snippet, &unused);

    svc_ui->panel_add_section(mod_ctx, panel, "Actions");
    svc_ui->panel_add_button(mod_ctx, panel, "Reset Results", on_reset, nullptr);

    svc_ui->panel_add_dyn_text(mod_ctx, panel, "", &g_el_link_angle);
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

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, "mod_test initializing");
    svc_log->warn(mod_ctx, "LogService::warn smoke test");
    svc_log->error(mod_ctx, "LogService::error smoke test");

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
    ConfigVarHandle cfgFlag = 0, cfgInt = 0, cfgFloat = 0, cfgString = 0;
    ConfigVarDesc cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testFlag";
    cfgDesc.type = CONFIG_VAR_BOOL;
    cfgDesc.default_bool = 1;
    g_config_ok = svc_config->register_var(mod_ctx, &cfgDesc, &cfgFlag) == MOD_OK && cfgFlag != 0;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testInt";
    cfgDesc.type = CONFIG_VAR_INT;
    cfgDesc.default_int = 42;
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &cfgInt) == MOD_OK;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testFloat";
    cfgDesc.type = CONFIG_VAR_FLOAT;
    cfgDesc.default_float = 1.5;
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &cfgFloat) == MOD_OK;

    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testString";
    cfgDesc.type = CONFIG_VAR_STRING;
    cfgDesc.default_string = "hello";
    g_config_ok = g_config_ok && svc_config->register_var(mod_ctx, &cfgDesc, &cfgString) == MOD_OK;

    // Values persist across sessions, so assert the set/get round trip, not the defaults.
    uint32_t cfgBoolValue = 1;
    int64_t cfgIntValue = 0;
    double cfgFloatValue = 0.0;
    char cfgStringBuf[8] = {};
    size_t cfgStringLength = 0;
    g_config_ok = g_config_ok &&
        svc_config->set_bool(mod_ctx, cfgFlag, 0) == MOD_OK &&
        svc_config->get_bool(mod_ctx, cfgFlag, &cfgBoolValue) == MOD_OK && cfgBoolValue == 0 &&
        svc_config->set_int(mod_ctx, cfgInt, 1234) == MOD_OK &&
        svc_config->get_int(mod_ctx, cfgInt, &cfgIntValue) == MOD_OK && cfgIntValue == 1234 &&
        svc_config->set_float(mod_ctx, cfgFloat, 2.25) == MOD_OK &&
        svc_config->get_float(mod_ctx, cfgFloat, &cfgFloatValue) == MOD_OK &&
        cfgFloatValue == 2.25 &&
        svc_config->set_string(mod_ctx, cfgString, "abc") == MOD_OK &&
        // Length query with a null buffer, then an exact-size read.
        svc_config->get_string(mod_ctx, cfgString, nullptr, 0, &cfgStringLength) == MOD_OK &&
        cfgStringLength == 3 &&
        svc_config->get_string(mod_ctx, cfgString, cfgStringBuf, 4, nullptr) == MOD_OK &&
        std::strcmp(cfgStringBuf, "abc") == 0;

    // Unregister releases the name for a later registration (the reload path in miniature).
    cfgDesc = CONFIG_VAR_DESC_INIT;
    cfgDesc.name = "testTemp";
    cfgDesc.type = CONFIG_VAR_BOOL;
    ConfigVarHandle cfgTemp = 0;
    g_config_ok = g_config_ok &&
        svc_config->register_var(mod_ctx, &cfgDesc, &cfgTemp) == MOD_OK &&
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
            mod_ctx, cfgInt, on_config_changed, &g_config_change_count, &cfgSub) == MOD_OK &&
        cfgSub != 0 &&
        svc_config->set_int(mod_ctx, cfgInt, 5678) == MOD_OK && g_config_change_count == 1 &&
        g_config_prev_value == 1234 &&
        // Writes that don't change the value don't notify.
        svc_config->set_int(mod_ctx, cfgInt, 5678) == MOD_OK && g_config_change_count == 1 &&
        svc_config->unsubscribe(mod_ctx, cfgSub) == MOD_OK &&
        svc_config->set_int(mod_ctx, cfgInt, 42) == MOD_OK && g_config_change_count == 1;
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
        svc_config->get_int(mod_ctx, cfgFlag, &cfgIntValue) == MOD_INVALID_ARGUMENT &&
        svc_config->get_string(mod_ctx, cfgString, cfgTinyBuf, sizeof(cfgTinyBuf), nullptr) ==
            MOD_INVALID_ARGUMENT &&
        svc_config->set_bool(mod_ctx, UINT64_C(0xdead), 1) == MOD_INVALID_ARGUMENT &&
        svc_config->unregister_var(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT &&
        svc_config->unsubscribe(mod_ctx, UINT64_C(0xdead)) == MOD_INVALID_ARGUMENT;
    if (g_config_neg_ok) {
        svc_log->info(mod_ctx, "ConfigService negative tests OK");
    } else {
        svc_log->error(mod_ctx, "ConfigService negative tests FAILED");
    }

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

    UiTab tab = UI_TAB_INIT;
    tab.build = build_panel;
    tab.update = update_panel;
    result = svc_ui->register_tab(mod_ctx, &tab);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register UI tab");
    }

    g_initialized = 1;
    svc_log->info(mod_ctx, "mod_test ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    ++g_ticks;
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    char logBuf[96];
    std::snprintf(logBuf, sizeof(logBuf), "mod_test unloaded after %d ticks", g_ticks);
    svc_log->info(mod_ctx, logBuf);

    g_el_pre_badge = g_el_post_badge = g_el_replace_badge = nullptr;
    g_el_argwrite_badge = g_el_cancel_count = g_el_post_count = nullptr;
    g_el_link_angle = g_el_overlay_read_badge = nullptr;
    return MOD_OK;
}
}
