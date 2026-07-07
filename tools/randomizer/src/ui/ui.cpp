#include "ui.hpp"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "dusk/logging.h"

#include "../game/randomizer_context.hpp"
#include "../paths.hpp"
#include "config_store.hpp"

extern "C" MOD_EXPORT ModContext* mod_ctx;

// UiService port of the branch's rando UI: the seed-generation flow
// (rando_seed_generation.cpp — worker thread + status dialog) is ported 1:1; the
// 1,165-line hand-built settings window (rando_config.cpp) is re-expressed
// data-driven as one SELECT control per generator setting; the file-select play-type
// flow (origin's selectDataPlayTypeMove + FileSelectRandomizerWindow) is re-expressed
// as a SaveService new-save gate pushing a dialog + window. The branch's curated
// grouping/panel layout and the ImGui debug menu are tracked in PORTING.md.

namespace randomizer::ui {

namespace {

const UiService* s_ui = nullptr;
const SaveService* s_save = nullptr;
const ConfigService* s_config = nullptr;
ConfigVarHandle s_pending_seed_var{};
UiMenuTabHandle s_menu_tab{};
SaveGateHandle s_new_save_gate{};

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

// --- new-save play-type gate ---------------------------------------------------------

// Port of origin's file-select flow (selectDataPlayTypeMove + FileSelectRandomizerWindow):
// opening an empty file asks Vanilla vs Randomizer; Randomizer opens a window to pick one
// of the generated seeds and start. Starting writes the pending-seed config var that
// on_new_save consumes; dismissing the dialog or closing the window without starting
// cancels the gate, backing out to file select.

std::vector<std::string> s_gate_seed_storage;
std::vector<const char*> s_gate_seed_options;
size_t s_gate_selected_seed = 0;
bool s_gate_started = false;
UiWindowHandle s_gate_window{};

void gate_seed_get(ModContext*, void*, UiControlValue* out_value) {
    out_value->int_value = static_cast<int64_t>(s_gate_selected_seed);
}

void gate_seed_set(ModContext*, void*, const UiControlValue* value) {
    if (value->int_value >= 0 &&
        static_cast<size_t>(value->int_value) < s_gate_seed_storage.size()) {
        s_gate_selected_seed = static_cast<size_t>(value->int_value);
    }
}

bool gate_start_disabled(ModContext*, void*) {
    return s_gate_selected_seed == 0 || s_gate_selected_seed >= s_gate_seed_storage.size();
}

void on_gate_start_pressed(ModContext* ctx, void*) {
    if (gate_start_disabled(ctx, nullptr)) {
        return;
    }
    s_config->set_string(
        ctx, s_pending_seed_var, s_gate_seed_storage[s_gate_selected_seed].c_str());
    s_gate_started = true;
    s_ui->window_close(ctx, s_gate_window);
}

ModResult build_gate_play_tab(ModContext* ctx, UiWindowHandle, UiElementHandle left_pane,
    UiElementHandle, void*, ModError*) {
    s_gate_seed_storage.clear();
    s_gate_seed_storage.push_back("None");
    std::error_code ec;
    for (const auto& entry :
        std::filesystem::directory_iterator{paths::GetRandomizerSeedsPath(), ec}) {
        if (entry.is_directory()) {
            s_gate_seed_storage.push_back(entry.path().filename().string());
        }
    }
    s_gate_seed_options.clear();
    for (const auto& hash : s_gate_seed_storage) {
        s_gate_seed_options.push_back(hash.c_str());
    }

    // Preselect the pending seed when it still exists (tab rebuilds re-enumerate).
    s_gate_selected_seed = 0;
    size_t length = 0;
    if (s_config->get_string(ctx, s_pending_seed_var, nullptr, 0, &length) == MOD_OK &&
        length > 0)
    {
        std::string pending(length, '\0');
        if (s_config->get_string(ctx, s_pending_seed_var, pending.data(), length + 1, nullptr) ==
            MOD_OK)
        {
            for (size_t i = 1; i < s_gate_seed_storage.size(); ++i) {
                if (s_gate_seed_storage[i] == pending) {
                    s_gate_selected_seed = i;
                    break;
                }
            }
        }
    }

    if (s_gate_seed_storage.size() == 1) {
        s_ui->pane_add_text(ctx, left_pane,
            "No seeds generated! You can generate a seed from the Seed Management tab.",
            nullptr);
    }

    {
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_SELECT;
        desc.label = "Selected Seed";
        desc.help_rml = "Choose which seed you want to play.";
        desc.binding = UI_BINDING_CALLBACKS;
        desc.get = gate_seed_get;
        desc.set = gate_seed_set;
        desc.options = s_gate_seed_options.data();
        desc.option_count = s_gate_seed_options.size();
        s_ui->pane_add_control(ctx, left_pane, &desc, nullptr);
    }

    {
        UiControlDesc desc = UI_CONTROL_DESC_INIT;
        desc.kind = UI_CONTROL_BUTTON;
        desc.label = "Start Randomizer";
        desc.help_rml = "Creates this save with the selected seed active.";
        desc.on_pressed = on_gate_start_pressed;
        desc.is_disabled = gate_start_disabled;
        s_ui->pane_add_control(ctx, left_pane, &desc, nullptr);
    }

    return MOD_OK;
}

void on_gate_window_closed(ModContext* ctx, UiWindowHandle, void*) {
    s_gate_window = 0;
    s_save->complete_new_save_gate(ctx, s_gate_started);
    s_gate_started = false;
}

void push_gate_window(ModContext* ctx) {
    s_gate_started = false;
    UiTabDesc tabs[3]{};
    tabs[0].struct_size = sizeof(UiTabDesc);
    tabs[0].title = "Play";
    tabs[0].build = build_gate_play_tab;
    tabs[1].struct_size = sizeof(UiTabDesc);
    tabs[1].title = "Seed Management";
    tabs[1].build = build_seed_tab;
    tabs[1].update = update_seed_tab;
    tabs[2].struct_size = sizeof(UiTabDesc);
    tabs[2].title = "Settings";
    tabs[2].build = build_settings_tab;

    UiWindowDesc desc = UI_WINDOW_DESC_INIT;
    desc.tabs = tabs;
    desc.tab_count = 3;
    desc.on_closed = on_gate_window_closed;
    if (s_ui->window_push(ctx, &desc, &s_gate_window) != MOD_OK) {
        s_gate_window = 0;
        s_save->complete_new_save_gate(ctx, false);
    }
}

void on_gate_vanilla(ModContext* ctx, UiDialogHandle, void*) {
    // A vanilla file: clear the pending seed so on_new_save leaves the save untouched.
    s_config->set_string(ctx, s_pending_seed_var, "");
    s_save->complete_new_save_gate(ctx, true);
}

void on_gate_randomizer(ModContext* ctx, UiDialogHandle, void*) {
    push_gate_window(ctx);
}

void on_gate_dismiss(ModContext* ctx, UiDialogHandle, void*) {
    s_save->complete_new_save_gate(ctx, false);
}

void on_new_save_gate(ModContext* ctx, uint32_t, void*) {
    UiDialogAction actions[2]{};
    actions[0].label = "Vanilla";
    actions[0].on_pressed = on_gate_vanilla;
    actions[1].label = "Randomizer";
    actions[1].on_pressed = on_gate_randomizer;

    UiDialogDesc desc = UI_DIALOG_DESC_INIT;
    desc.title = "Play Type";
    desc.body_rml = "What mode would you like to play?";
    desc.icon = "question-mark";
    desc.actions = actions;
    desc.action_count = 2;
    desc.on_dismiss = on_gate_dismiss;
    UiDialogHandle dialog{};
    if (s_ui->dialog_push(ctx, &desc, &dialog) != MOD_OK) {
        // Never block save creation on a UI failure.
        s_save->complete_new_save_gate(ctx, true);
    }
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

ModResult initialize(const UiService* ui, const SaveService* save, const ConfigService* config,
    ConfigVarHandle pending_seed_var) {
    s_ui = ui;
    s_save = save;
    s_config = config;
    s_pending_seed_var = pending_seed_var;

    UiMenuTabDesc desc = UI_MENU_TAB_DESC_INIT;
    desc.label = "Randomizer";
    desc.on_selected = on_menu_tab_selected;
    const ModResult res = s_ui->register_menu_tab(mod_ctx, &desc, &s_menu_tab);
    if (res != MOD_OK) {
        return res;
    }

    return s_save->register_new_save_gate(mod_ctx, on_new_save_gate, nullptr, &s_new_save_gate);
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
