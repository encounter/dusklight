#include "ui.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dusk/logging.h"

#include "../game/randomizer_context.hpp"
#include "config_store.hpp"

extern "C" MOD_EXPORT ModContext* mod_ctx;

// UiService port of the branch's rando UI: the seed-generation flow
// (rando_seed_generation.cpp — worker thread + status dialog) is ported 1:1; the
// 1,165-line hand-built settings window (rando_config.cpp) is re-expressed
// data-driven as one SELECT control per generator setting. The branch's curated
// grouping/panel layout and the ImGui debug menu are tracked in PORTING.md.

namespace randomizer::ui {

namespace {

const UiService* s_ui = nullptr;
ConfigVarHandle s_pending_seed_var{};
UiMenuTabHandle s_menu_tab{};

UiElementHandle s_active_seed_row{};

// --- seed generation (port of rando_seed_generation.cpp) --------------------------

enum class SeedGenerateStatus {
    Ready,
    Generating,
    Success,
    Error,
};

std::atomic<SeedGenerateStatus> s_gen_status{SeedGenerateStatus::Ready};
std::string s_gen_message{};
UiDialogHandle s_gen_dialog{};

void start_seed_generation() {
    if (GenerateAndWriteSeed(s_gen_message)) {
        s_gen_status.store(SeedGenerateStatus::Success);
    } else {
        s_gen_status.store(SeedGenerateStatus::Error);
    }
    DuskLog.debug("{}", s_gen_message);
}

void on_generate_pressed(ModContext*, void*) {
    if (s_gen_status.load() != SeedGenerateStatus::Ready) {
        return;
    }
    TryCreateRandomSeed();

    s_gen_status.store(SeedGenerateStatus::Generating);
    std::thread{start_seed_generation}.detach();

    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = "Randomizer";
    desc.body_rml = "Generating Seed...";
    desc.icon = "verifying";
    UiDialogAction cancel{"Close", nullptr, nullptr, false};
    desc.actions = &cancel;
    desc.action_count = 1;
    if (s_ui->dialog_push(mod_ctx, &desc, &s_gen_dialog) != MOD_OK) {
        s_gen_dialog = 0;
    }
}

std::string escape_rml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        default: out += c; break;
        }
    }
    return out;
}

// --- settings tab ------------------------------------------------------------------

// Per-control state for the data-driven settings list; rebuilt with every tab build.
struct SettingBinding {
    seedgen::settings::Setting* setting;
    std::vector<std::string> option_storage;
    std::vector<const char*> options;
    std::string help;
};

std::vector<std::unique_ptr<SettingBinding>> s_bindings;

void setting_get(ModContext*, void* user_data, UiControlValue* out_value) {
    auto* binding = static_cast<SettingBinding*>(user_data);
    out_value->int_value = binding->setting->GetCurrentOptionIndex();
}

void setting_set(ModContext*, void* user_data, const UiControlValue* value) {
    auto* binding = static_cast<SettingBinding*>(user_data);
    if (binding->setting->GetCurrentOptionIndex() == value->int_value) {
        return;
    }
    binding->setting->SetCurrentOption(static_cast<int>(value->int_value));
    SaveRandomizerConfig();
}

ModResult build_settings_tab(ModContext* ctx, UiWindowHandle, UiElementHandle left_pane,
    UiElementHandle, void*, ModError*) {
    s_bindings.clear();

    auto* all_info = seedgen::settings::GetAllSettingsInfo();
    for (const auto& [name, info] : *all_info) {
        auto* setting = FindSetting(name);
        if (setting == nullptr || info->GetOptions().empty()) {
            continue;
        }

        auto binding = std::make_unique<SettingBinding>();
        binding->setting = setting;
        for (const auto& option : info->GetOptions()) {
            binding->option_storage.push_back(option);
        }
        for (const auto& option : binding->option_storage) {
            binding->options.push_back(option.c_str());
        }
        const auto& descriptions = info->GetDescriptions();
        if (!descriptions.empty()) {
            binding->help = descriptions.front();
        }

        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_SELECT;
        desc.label = name.c_str();
        desc.help_rml = binding->help.empty() ? nullptr : binding->help.c_str();
        desc.binding = UI_BINDING_CALLBACKS;
        desc.get = setting_get;
        desc.set = setting_set;
        desc.user_data = binding.get();
        desc.options = binding->options.data();
        desc.option_count = binding->options.size();
        s_ui->pane_add_control(ctx, left_pane, &desc, nullptr);

        s_bindings.push_back(std::move(binding));
    }
    return MOD_OK;
}

// --- seed tab -----------------------------------------------------------------------

ModResult build_seed_tab(ModContext* ctx, UiWindowHandle, UiElementHandle left_pane,
    UiElementHandle, void*, ModError*) {
    s_ui->pane_add_section(ctx, left_pane, "Seed");

    const auto& hash = randomizer_GetContext().mHash;
    s_ui->pane_add_text(ctx, left_pane,
        hash.empty() ? "Active seed: none" : ("Active seed: " + hash).c_str(),
        &s_active_seed_row);

    {
        // Pending seed for the next new save (the seed hash to play).
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_STRING;
        desc.label = "Seed for New Saves";
        desc.help_rml = "Hash of a generated seed to activate when a new save file is created. "
                        "Generated seeds live in the randomizer/seeds data directory.";
        desc.binding = UI_BINDING_CONFIG_VAR;
        desc.config_var = s_pending_seed_var;
        s_ui->pane_add_control(ctx, left_pane, &desc, nullptr);
    }

    {
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_BUTTON;
        desc.label = "Generate Seed";
        desc.help_rml = "Generates a new seed from the current settings.";
        desc.on_pressed = on_generate_pressed;
        s_ui->pane_add_control(ctx, left_pane, &desc, nullptr);
    }

    return MOD_OK;
}

ModResult update_seed_tab(ModContext* ctx, void*, ModError*) {
    if (s_active_seed_row != 0) {
        const auto& hash = randomizer_GetContext().mHash;
        s_ui->elem_set_text(ctx, s_active_seed_row,
            hash.empty() ? "Active seed: none" : ("Active seed: " + hash).c_str());
    }
    return MOD_OK;
}

// --- window / menu tab ----------------------------------------------------------------

void on_menu_tab_selected(ModContext* ctx, void*) {
    UiTabDesc tabs[2]{};
    tabs[0].struct_size = sizeof(UiTabDesc);
    tabs[0].title = "Seed";
    tabs[0].build = build_seed_tab;
    tabs[0].update = update_seed_tab;
    tabs[1].struct_size = sizeof(UiTabDesc);
    tabs[1].title = "Settings";
    tabs[1].build = build_settings_tab;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 2;
    UiWindowHandle window{};
    s_ui->window_push(ctx, &desc, &window);
}

}  // namespace

ModResult initialize(const UiService* ui, ConfigVarHandle pending_seed_var) {
    s_ui = ui;
    s_pending_seed_var = pending_seed_var;

    UiMenuTabDesc desc = UI_MENU_TAB_DESC_INIT;
    desc.label = "Randomizer";
    desc.on_selected = on_menu_tab_selected;
    return s_ui->register_menu_tab(mod_ctx, &desc, &s_menu_tab);
}

void update() {
    const auto status = s_gen_status.load();
    if (status != SeedGenerateStatus::Success && status != SeedGenerateStatus::Error) {
        return;
    }
    if (s_gen_dialog != 0) {
        s_ui->dialog_set_icon(mod_ctx, s_gen_dialog, status == SeedGenerateStatus::Success
                                                          ? "celebration"
                                                          : "error");
        s_ui->dialog_set_body(mod_ctx, s_gen_dialog, escape_rml(s_gen_message).c_str());
    }
    s_gen_status.store(SeedGenerateStatus::Ready);
}

}  // namespace randomizer::ui
