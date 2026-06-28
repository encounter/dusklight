// Tests every feature of the Dusklight mod API. Results are shown in the mod tab.

#include "d/actor/d_a_alink.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/host.h"
#include "mods/svc/log.h"
#include "mods/svc/resource.h"
#include "mods/svc/ui.h"

DEFINE_MOD();
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE(UiService, svc_ui);
IMPORT_SERVICE(HookService, svc_hook);

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

ElemHandle g_el_pre_badge = nullptr;
ElemHandle g_el_post_badge = nullptr;
ElemHandle g_el_replace_badge = nullptr;
ElemHandle g_el_argwrite_badge = nullptr;
ElemHandle g_el_cancel_count = nullptr;
ElemHandle g_el_post_count = nullptr;
ElemHandle g_el_link_angle = nullptr;

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

    SERVICE_CALL(svc_ui, panel_add_section, panel, "Hooks");
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "pre-hook fired (posMove)", g_pre_fired,
        &g_el_pre_badge);
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "post-hook fired (posMove)", g_post_fired,
        &g_el_post_badge);
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "replace-hook fired (execute)",
        g_replace_fired, &g_el_replace_badge);
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "arg_ref write + pre cancel (hold L)",
        g_arg_write_ok, &g_el_argwrite_badge);

    char countBuf[64];
    std::snprintf(countBuf, sizeof(countBuf), "pre cancels: %d", g_pre_cancel_count);
    SERVICE_CALL(svc_ui, panel_add_dyn_text, panel, countBuf, &g_el_cancel_count);
    std::snprintf(countBuf, sizeof(countBuf), "post calls: %d", g_post_count);
    SERVICE_CALL(svc_ui, panel_add_dyn_text, panel, countBuf, &g_el_post_count);

    SERVICE_CALL(svc_ui, panel_add_section, panel, "Resources");
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "ResourceService::load (hello.txt)",
        g_resource_ok, &unused);
    if (g_resource_text[0] != '\0') {
        SERVICE_CALL(svc_ui, panel_add_dyn_text, panel, g_resource_text, &unused);
    }

    SERVICE_CALL(svc_ui, panel_add_section, panel, "API Fields");
    SERVICE_CALL(svc_ui, panel_add_badge_row, panel, "mod_dir non-empty",
        g_mod_dir_snippet[0] != '\0', &unused);
    SERVICE_CALL(svc_ui, panel_add_dyn_text, panel, g_mod_dir_snippet, &unused);

    SERVICE_CALL(svc_ui, panel_add_section, panel, "Actions");
    SERVICE_CALL(svc_ui, panel_add_button, panel, "Reset Results", on_reset, nullptr);

    SERVICE_CALL(svc_ui, panel_add_dyn_text, panel, "", &g_el_link_angle);
    return MOD_OK;
}

ModResult update_panel(ModContext*, void*, ModError*) {
    SERVICE_CALL(svc_ui, elem_set_badge, g_el_pre_badge, g_pre_fired);
    SERVICE_CALL(svc_ui, elem_set_badge, g_el_post_badge, g_post_fired);
    SERVICE_CALL(svc_ui, elem_set_badge, g_el_replace_badge, g_replace_fired);
    SERVICE_CALL(svc_ui, elem_set_badge, g_el_argwrite_badge, g_arg_write_ok);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "pre cancels: %d", g_pre_cancel_count);
    SERVICE_CALL(svc_ui, elem_set_text, g_el_cancel_count, buf);

    std::snprintf(buf, sizeof(buf), "post calls: %d", g_post_count);
    SERVICE_CALL(svc_ui, elem_set_text, g_el_post_count, buf);

    if (g_seen_link) {
        std::snprintf(buf, sizeof(buf), "Link y angle: %d", g_last_link_y_angle);
    } else {
        std::snprintf(buf, sizeof(buf), "Link y angle: waiting");
    }
    SERVICE_CALL(svc_ui, elem_set_text, g_el_link_angle, buf);
    return MOD_OK;
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    SERVICE_CALL(svc_log, info, "mod_test initializing");
    SERVICE_CALL(svc_log, warn, "LogService::warn smoke test");
    SERVICE_CALL(svc_log, error, "LogService::error smoke test");

    std::snprintf(
        g_mod_dir_snippet, sizeof(g_mod_dir_snippet), "%.60s", SERVICE_CALL(svc_host, mod_dir));

    ResourceBuffer buffer = RESOURCE_BUFFER_INIT;
    ModResult loadResult = SERVICE_CALL(svc_resource, load, "hello.txt", &buffer);
    if (loadResult == MOD_OK) {
        size_t copy =
            buffer.size < sizeof(g_resource_text) - 1 ? buffer.size : sizeof(g_resource_text) - 1;
        std::memcpy(g_resource_text, buffer.data, copy);
        g_resource_text[copy] = '\0';
        while (copy > 0 && g_resource_text[copy - 1] == '\n') {
            g_resource_text[--copy] = '\0';
        }
        SERVICE_CALL(svc_resource, free, &buffer);
        g_resource_ok = true;

        char logBuf[320];
        std::snprintf(logBuf, sizeof(logBuf), "ResourceService::load OK: \"%s\"", g_resource_text);
        SERVICE_CALL(svc_log, info, logBuf);
    } else {
        SERVICE_CALL(svc_log, error, "ResourceService::load FAILED for hello.txt");
    }

    ResourceBuffer missing = RESOURCE_BUFFER_INIT;
    loadResult = SERVICE_CALL(svc_resource, load, "does_not_exist.bin", &missing);
    if (loadResult == MOD_UNAVAILABLE) {
        SERVICE_CALL(svc_log, info, "missing resource correctly returned MOD_UNAVAILABLE");
    } else if (loadResult == MOD_OK) {
        SERVICE_CALL(svc_log, error, "missing resource unexpectedly returned data");
        SERVICE_CALL(svc_resource, free, &missing);
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

    UiTab tab = UI_TAB_INIT;
    tab.build = build_panel;
    tab.update = update_panel;
    result = SERVICE_CALL(svc_ui, register_tab, &tab);
    if (result != MOD_OK) {
        return require_ok(result, error, "failed to register UI tab");
    }

    SERVICE_CALL(svc_log, info, "mod_test ready");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    ++g_ticks;
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    char logBuf[96];
    std::snprintf(logBuf, sizeof(logBuf), "mod_test unloaded after %d ticks", g_ticks);
    SERVICE_CALL(svc_log, info, logBuf);

    g_el_pre_badge = g_el_post_badge = g_el_replace_badge = nullptr;
    g_el_argwrite_badge = g_el_cancel_count = g_el_post_count = nullptr;
    g_el_link_angle = nullptr;
    return MOD_OK;
}
}
