#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

namespace dusk::mods::svc {
namespace {

// Validation of the tagged control descriptor: required fields per kind/binding. Value
// translation and cvar wiring live in loader/ui.cpp.
bool valid_control_desc(const UiControlDesc& desc) {
    if (desc.struct_size < sizeof(UiControlDesc) || desc.label == nullptr) {
        return false;
    }
    switch (desc.kind) {
    case UI_CONTROL_BUTTON:
        return desc.on_pressed != nullptr;
    case UI_CONTROL_TOGGLE:
    case UI_CONTROL_NUMBER:
    case UI_CONTROL_STRING:
    case UI_CONTROL_SELECT:
        break;
    default:
        return false;
    }
    if (desc.kind == UI_CONTROL_SELECT) {
        if (desc.options == nullptr || desc.option_count == 0) {
            return false;
        }
        for (size_t i = 0; i < desc.option_count; ++i) {
            if (desc.options[i] == nullptr) {
                return false;
            }
        }
    }
    switch (desc.binding) {
    case UI_BINDING_CALLBACKS:
        return desc.get != nullptr && desc.set != nullptr;
    case UI_BINDING_CONFIG_VAR:
        return desc.config_var != 0;
    default:
        return false;
    }
}

ModResult ui_register_mods_panel(ModContext* context, const UiModsPanelDesc* desc) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(UiModsPanelDesc) ||
        desc->build == nullptr)
    {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_register_mods_panel(*mod, *desc);
}

ModResult ui_pane_add_section(ModContext* context, UiElementHandle pane, const char* title) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0 || title == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_section(*mod, pane, title);
}

ModResult ui_pane_add_text(
    ModContext* context, UiElementHandle pane, const char* text, UiElementHandle* outElem) {
    if (outElem != nullptr) {
        *outElem = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0 || text == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_text(*mod, pane, text, outElem);
}

ModResult ui_pane_add_rml(
    ModContext* context, UiElementHandle pane, const char* rml, UiElementHandle* outElem) {
    if (outElem != nullptr) {
        *outElem = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0 || rml == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_rml(*mod, pane, rml, outElem);
}

ModResult ui_pane_add_badge_row(ModContext* context, UiElementHandle pane, const char* label,
    bool ok, UiElementHandle* outElem) {
    if (outElem != nullptr) {
        *outElem = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0 || label == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_badge_row(*mod, pane, label, ok, outElem);
}

ModResult ui_pane_add_progress(
    ModContext* context, UiElementHandle pane, float value, UiElementHandle* outElem) {
    if (outElem != nullptr) {
        *outElem = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_progress(*mod, pane, value, outElem);
}

ModResult ui_pane_add_control(ModContext* context, UiElementHandle pane, const UiControlDesc* desc,
    UiElementHandle* outElem) {
    if (outElem != nullptr) {
        *outElem = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || pane == 0 || desc == nullptr || !valid_control_desc(*desc)) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_pane_add_control(*mod, pane, *desc, outElem);
}

ModResult ui_elem_set_text(ModContext* context, UiElementHandle elem, const char* text) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || elem == 0 || text == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_elem_set_text(*mod, elem, text);
}

ModResult ui_elem_set_rml(ModContext* context, UiElementHandle elem, const char* rml) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || elem == 0 || rml == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_elem_set_rml(*mod, elem, rml);
}

ModResult ui_elem_set_badge(ModContext* context, UiElementHandle elem, bool ok) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || elem == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_elem_set_badge(*mod, elem, ok);
}

ModResult ui_elem_set_progress(ModContext* context, UiElementHandle elem, float value) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || elem == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_elem_set_progress(*mod, elem, value);
}

ModResult ui_window_push(
    ModContext* context, const UiWindowDesc* desc, UiWindowHandle* outWindow) {
    if (outWindow != nullptr) {
        *outWindow = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(UiWindowDesc) ||
        desc->tabs == nullptr || desc->tab_count == 0)
    {
        return MOD_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < desc->tab_count; ++i) {
        const UiTabDesc& tab = desc->tabs[i];
        if (tab.struct_size < sizeof(UiTabDesc) || tab.title == nullptr || tab.build == nullptr) {
            return MOD_INVALID_ARGUMENT;
        }
    }
    uint64_t handle = 0;
    const auto result = mods::ui_window_push(*mod, *desc, handle);
    if (result == MOD_OK && outWindow != nullptr) {
        *outWindow = handle;
    }
    return result;
}

ModResult ui_window_close(ModContext* context, UiWindowHandle window) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || window == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_window_close(*mod, window);
}

ModResult ui_dialog_push(
    ModContext* context, const UiDialogDesc* desc, UiDialogHandle* outDialog) {
    if (outDialog != nullptr) {
        *outDialog = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(UiDialogDesc) ||
        desc->title == nullptr || desc->body_rml == nullptr || desc->actions == nullptr ||
        desc->action_count == 0 || desc->variant > UI_DIALOG_DANGER)
    {
        return MOD_INVALID_ARGUMENT;
    }
    for (size_t i = 0; i < desc->action_count; ++i) {
        if (desc->actions[i].label == nullptr) {
            return MOD_INVALID_ARGUMENT;
        }
    }
    uint64_t handle = 0;
    const auto result = mods::ui_dialog_push(*mod, *desc, handle);
    if (result == MOD_OK && outDialog != nullptr) {
        *outDialog = handle;
    }
    return result;
}

ModResult ui_dialog_close(ModContext* context, UiDialogHandle dialog) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || dialog == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_dialog_close(*mod, dialog);
}

ModResult ui_is_any_document_visible(ModContext* context, bool* outVisible) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || outVisible == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    *outVisible = mods::ui_any_document_visible();
    return MOD_OK;
}

ModResult ui_focus_top_document(ModContext* context) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    mods::ui_focus_top_document();
    return MOD_OK;
}

ModResult ui_close_top_document(ModContext* context) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_close_top_document(*mod);
}

ModResult ui_register_styles(
    ModContext* context, UiStyleScope scope, const char* rcss, UiStyleHandle* outStyle) {
    if (outStyle != nullptr) {
        *outStyle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || rcss == nullptr || scope > UI_SCOPE_GRAPHICS_TUNER) {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = mods::ui_register_styles(*mod, scope, rcss, handle);
    if (result == MOD_OK && outStyle != nullptr) {
        *outStyle = handle;
    }
    return result;
}

ModResult ui_register_styles_file(
    ModContext* context, UiStyleScope scope, const char* path, UiStyleHandle* outStyle) {
    if (outStyle != nullptr) {
        *outStyle = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || path == nullptr || !is_safe_resource_path(path) ||
        scope > UI_SCOPE_GRAPHICS_TUNER)
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = mods::ui_register_styles_file(*mod, scope, path, handle);
    if (result == MOD_OK && outStyle != nullptr) {
        *outStyle = handle;
    }
    return result;
}

ModResult ui_unregister_styles(ModContext* context, UiStyleHandle style) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || style == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_unregister_styles(*mod, style);
}

ModResult ui_register_menu_tab(
    ModContext* context, const UiMenuTabDesc* desc, UiMenuTabHandle* outTab) {
    if (outTab != nullptr) {
        *outTab = 0;
    }
    auto* mod = mod_from_context(context);
    if (mod == nullptr || desc == nullptr || desc->struct_size < sizeof(UiMenuTabDesc) ||
        desc->label == nullptr || desc->label[0] == '\0' || desc->on_selected == nullptr)
    {
        return MOD_INVALID_ARGUMENT;
    }
    uint64_t handle = 0;
    const auto result = mods::ui_register_menu_tab(*mod, *desc, handle);
    if (result == MOD_OK && outTab != nullptr) {
        *outTab = handle;
    }
    return result;
}

ModResult ui_unregister_menu_tab(ModContext* context, UiMenuTabHandle tab) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || tab == 0) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_unregister_menu_tab(*mod, tab);
}

ModResult ui_dialog_set_body(ModContext* context, UiDialogHandle dialog, const char* bodyRml) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || dialog == 0 || bodyRml == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_dialog_set_body(*mod, dialog, bodyRml);
}

ModResult ui_dialog_set_icon(ModContext* context, UiDialogHandle dialog, const char* icon) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || dialog == 0 || icon == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_dialog_set_icon(*mod, dialog, icon);
}

ModResult ui_dialog_add_action(
    ModContext* context, UiDialogHandle dialog, const UiDialogAction* action) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || dialog == 0 || action == nullptr || action->label == nullptr) {
        return MOD_INVALID_ARGUMENT;
    }
    return mods::ui_dialog_add_action(*mod, dialog, *action);
}

constexpr UiService s_uiService{
    .header = SERVICE_HEADER(UiService, UI_SERVICE_MAJOR, UI_SERVICE_MINOR),
    .register_mods_panel = ui_register_mods_panel,
    .pane_add_section = ui_pane_add_section,
    .pane_add_text = ui_pane_add_text,
    .pane_add_rml = ui_pane_add_rml,
    .pane_add_badge_row = ui_pane_add_badge_row,
    .pane_add_progress = ui_pane_add_progress,
    .pane_add_control = ui_pane_add_control,
    .elem_set_text = ui_elem_set_text,
    .elem_set_rml = ui_elem_set_rml,
    .elem_set_badge = ui_elem_set_badge,
    .elem_set_progress = ui_elem_set_progress,
    .window_push = ui_window_push,
    .window_close = ui_window_close,
    .dialog_push = ui_dialog_push,
    .dialog_close = ui_dialog_close,
    .is_any_document_visible = ui_is_any_document_visible,
    .focus_top_document = ui_focus_top_document,
    .close_top_document = ui_close_top_document,
    .register_styles = ui_register_styles,
    .register_styles_file = ui_register_styles_file,
    .unregister_styles = ui_unregister_styles,
    .register_menu_tab = ui_register_menu_tab,
    .unregister_menu_tab = ui_unregister_menu_tab,
    .dialog_set_body = ui_dialog_set_body,
    .dialog_set_icon = ui_dialog_set_icon,
    .dialog_add_action = ui_dialog_add_action,
};

}  // namespace

const UiService& ui_service() {
    return s_uiService;
}

}  // namespace dusk::mods::svc
