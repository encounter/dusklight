#include "saves_window.hpp"

#include "aurora/lib/window.hpp"
#include "bool_button.hpp"
#include "borealis/file_select.hpp"
#include "borealis/io.hpp"
#include "button.hpp"
#include "context_menu.hpp"
#include "dusk/data.hpp"
#include "dusk/game_mode.hpp"
#include "dusk/main.h"
#include "dusk/mod_loader.hpp"
#include "dusk/save_manager.hpp"
#include "dusk/settings.h"
#include "format.hpp"
#include "icon_button.hpp"
#include "modal.hpp"
#include "pane.hpp"
#include "prelaunch.hpp"
#include "ui.hpp"
#include "window.hpp"

#include <SDL3/SDL_misc.h>
#include <fmt/format.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <set>

namespace dusk::ui {
namespace {

using save_manager::Artifact;
using save_manager::ModDataAction;
using save_manager::Result;
using save_manager::SaveIdentity;
using save_manager::Storage;

struct ModeOption {
    std::string saveName;
    std::string label;
};

struct Context {
    SaveIdentity identity;
    Storage storage;
};

struct ImportItem {
    Context context;
    Artifact artifact;
    std::vector<SaveIdentity> rawImageIdentities;
};

std::string s_selectedSaveName;
std::atomic_uint64_t s_refreshGeneration = 1;
std::deque<std::string> s_pendingImports;
bool s_importActive = false;

void process_next_import();

void finish_import_flow() {
    s_importActive = false;
    process_next_import();
}

std::vector<ModeOption> mode_options() {
    std::vector<ModeOption> modes;
    std::set<std::string> seen;
    const auto& registered = gamemode::getGameModeManager().getRegisteredGameModes();
    const auto add_mode = [&](const gamemode::GameMode& mode) {
        if (seen.insert(mode.getSaveName()).second) {
            modes.push_back({.saveName = mode.getSaveName(), .label = mode.getFullName()});
        }
    };
    if (const auto vanilla = registered.find(gamemode::kVanillaGameModeId);
        vanilla != registered.end())
    {
        add_mode(vanilla->second);
    }
    for (const auto& [id, mode] : registered) {
        if (id != gamemode::kVanillaGameModeId) {
            add_mode(mode);
        }
    }
    return modes;
}

void ensure_selected_mode() {
    const auto modes = mode_options();
    if (std::ranges::none_of(
            modes, [](const ModeOption& mode) { return mode.saveName == s_selectedSaveName; }))
    {
        const auto* current = gamemode::getGameModeManager().getCurrentGameMode();
        s_selectedSaveName =
            current != nullptr ? current->getSaveName() : gamemode::kDefaultGameModeSaveName;
    }
}

std::string selected_mode_label() {
    ensure_selected_mode();
    for (const auto& mode : mode_options()) {
        if (mode.saveName == s_selectedSaveName) {
            return mode.label;
        }
    }
    return "Vanilla";
}

std::string mode_label(std::string_view saveName) {
    for (const auto& mode : mode_options()) {
        if (mode.saveName == saveName) {
            return mode.label;
        }
    }
    return std::string{saveName};
}

save_manager::ValueResult<Context> current_context() {
    ensure_selected_mode();
    const auto& state = prelaunch_state();
    if (data::is_data_path_restart_pending()) {
        return {
            {.message = "Restart before managing saves in the newly selected data folder."}, {}};
    }
    if (!state.activeDiscPath.empty() && state.configuredDiscPath != state.activeDiscPath) {
        return {{.message = "Restart before managing saves for the newly selected disc."}, {}};
    }
    auto identity = save_manager::identity_for_disc(state.configuredDiscInfo, s_selectedSaveName);
    if (!state.configuredDiscCanLaunch || !identity) {
        return {{.message = "Save management is available for a configured GameCube disc."}, {}};
    }
    const auto preferredKind = getSettings().backend.cardFileType.getValue() == 0 ?
                                   save_manager::StorageKind::RawImage :
                                   save_manager::StorageKind::GciDirectory;
    auto storage = save_manager::resolve_storage(identity->game, preferredKind);
    if (!storage) {
        return {storage.result, {}};
    }
    return {{.ok = true},
        Context{.identity = std::move(*identity), .storage = std::move(storage.value)}};
}

void dismiss_modal(Modal& modal) {
    mDoAud_seStartMenu(kSoundWindowClose);
    modal.pop();
}

void show_message(
    std::string title, std::string body, bool error = false, std::function<void()> onClose = {}) {
    auto* host = top_document();
    if (host == nullptr) {
        if (onClose) {
            onClose();
        }
        return;
    }
    const auto close = [onClose = std::move(onClose)](Modal& modal) {
        dismiss_modal(modal);
        if (onClose) {
            onClose();
        }
    };
    host->push(std::make_unique<Modal>(Modal::Props{
        .title = std::move(title),
        .bodyText = std::move(body),
        .actions = {{.label = "OK", .onPressed = close}},
        .onDismiss = close,
        .icon = error ? "warning" : "",
    }));
}

void show_result(std::string successMessage, const Result& result) {
    if (result) {
        ++s_refreshGeneration;
        show_message("Save Files", std::move(successMessage));
    } else {
        show_message("Save Files", result.message, true);
    }
}

bool installed_mod(std::string_view id) {
    return std::ranges::any_of(mods::ModLoader::instance().mods(),
        [id](const mods::LoadedMod& mod) { return mod.metadata.id == id; });
}

void export_artifact(save_manager::ExportArtifact artifact, std::string pattern) {
    borealis::file_select::export_file(
        {
            .parentWindow = aurora::window::get_sdl_window(),
            .sourceLocation = borealis::io::fs_path_to_string(artifact.path),
            .suggestedName = artifact.suggestedName,
            .filters = {{"Save file", std::move(pattern)}},
        },
        [artifact = std::move(artifact)](borealis::file_select::Result result) {
            save_manager::remove_temporary_export(artifact);
            if (result.status != borealis::file_select::Status::Selected &&
                result.status != borealis::file_select::Status::Canceled)
            {
                show_message("Export Failed",
                    result.message.empty() ? "The save file could not be exported." :
                                             result.message,
                    true);
            }
        });
}

void begin_export(bool includeModData) {
    auto context = current_context();
    if (!context) {
        show_message("Export Failed", context.result.message, true);
        return;
    }
    auto artifact =
        save_manager::build_export(context.value.storage, context.value.identity, includeModData);
    if (!artifact) {
        show_message("Export Failed", artifact.result.message, true);
        return;
    }
    export_artifact(std::move(artifact.value), includeModData ? "dusksave" : "gci");
}

void begin_raw_export() {
    auto context = current_context();
    if (!context) {
        show_message("Export Failed", context.result.message, true);
        return;
    }
    auto artifact = save_manager::raw_card_export(context.value.storage);
    if (!artifact) {
        show_message("Export Failed", artifact.result.message, true);
        return;
    }
    export_artifact(std::move(artifact.value), "raw");
}

void perform_import(std::shared_ptr<std::vector<ImportItem>> items, size_t selectedIndex,
    bool importAll, ModDataAction modDataAction, Modal& modal) {
    modal.pop();
    const bool replacingRawImage = !(*items)[selectedIndex].rawImageIdentities.empty();
    Result result{.ok = true};
    size_t importedCount = 0;
    for (size_t i = 0; i < items->size(); ++i) {
        if (!importAll && i != selectedIndex) {
            continue;
        }
        const auto& item = (*items)[i];
        if (!item.rawImageIdentities.empty()) {
            result = save_manager::import_raw_image(
                item.context.storage, item.rawImageIdentities, item.artifact, modDataAction);
        } else {
            result = save_manager::import_artifact(
                item.context.storage, item.context.identity, item.artifact, modDataAction);
        }
        if (!result) {
            break;
        }
        ++importedCount;
    }
    if (importedCount != 0) {
        ++s_refreshGeneration;
    }
    std::string message;
    if (result) {
        message =
            replacingRawImage  ? "The memory card image was imported." :
            importedCount == 1 ? fmt::format("The {} save was imported.", selected_mode_label()) :
                                 fmt::format("{} saves were imported.", importedCount);
    } else if (importedCount != 0) {
        message = fmt::format("{} save{} imported before the operation stopped: {}", importedCount,
            importedCount == 1 ? " was" : "s were", result.message);
    } else {
        message = result.message;
    }
    show_message("Save Files", std::move(message), !result, &finish_import_flow);
}

void confirm_import(Artifact artifact) {
    const bool hasBundledModData = artifact.kind == save_manager::ArtifactKind::DuskSave;
    if (!artifact.header.saveName.empty()) {
        const auto modes = mode_options();
        if (std::ranges::any_of(modes,
                [&](const ModeOption& mode) { return mode.saveName == artifact.header.saveName; }))
        {
            s_selectedSaveName = artifact.header.saveName;
        }
    }
    auto context = current_context();
    if (!context) {
        show_message("Import Failed", context.result.message, true, &finish_import_flow);
        return;
    }

    const bool rawToGci = artifact.kind == save_manager::ArtifactKind::Raw &&
                          context.value.storage.kind == save_manager::StorageKind::GciDirectory;
    auto items = std::make_shared<std::vector<ImportItem>>();
    size_t selectedIndex = 0;
    if (rawToGci) {
        std::vector<SaveIdentity> identities;
        for (const auto& mode : mode_options()) {
            if (auto identity = save_manager::identity_for_disc(
                    prelaunch_state().configuredDiscInfo, mode.saveName))
            {
                identities.push_back(std::move(*identity));
            }
        }
        auto extracted = save_manager::extract_raw_saves(artifact, identities);
        if (!extracted) {
            show_message("Import Failed", extracted.result.message, true, &finish_import_flow);
            return;
        }
        for (auto& extractedArtifact : extracted.value) {
            auto identity = save_manager::identity_for_disc(
                prelaunch_state().configuredDiscInfo, extractedArtifact.header.saveName);
            if (identity) {
                items->push_back({.context = {.identity = std::move(*identity),
                                      .storage = context.value.storage},
                    .artifact = std::move(extractedArtifact)});
            }
        }
        if (items->empty()) {
            show_message("Import Failed",
                "The card image does not contain a save for a registered Dusklight mode.", true,
                &finish_import_flow);
            return;
        }
        const auto selected = std::ranges::find_if(*items, [](const ImportItem& item) {
            return item.context.identity.saveName == s_selectedSaveName;
        });
        if (selected == items->end()) {
            s_selectedSaveName = items->front().context.identity.saveName;
        } else {
            selectedIndex = static_cast<size_t>(selected - items->begin());
        }
    } else {
        ImportItem item{.context = context.value, .artifact = std::move(artifact)};
        if (item.artifact.kind == save_manager::ArtifactKind::Raw &&
            item.context.storage.kind == save_manager::StorageKind::RawImage)
        {
            for (const auto& mode : mode_options()) {
                if (auto identity = save_manager::identity_for_disc(
                        prelaunch_state().configuredDiscInfo, mode.saveName))
                {
                    item.rawImageIdentities.push_back(std::move(*identity));
                }
            }
        }
        items->push_back(std::move(item));
    }

    const bool replacingRawImage =
        items->front().artifact.kind == save_manager::ArtifactKind::Raw &&
        items->front().context.storage.kind == save_manager::StorageKind::RawImage;
    const bool canImportAll = items->size() > 1;
    std::string body;
    if (replacingRawImage) {
        body = "Replace the entire memory card image? Every save on the current card will be "
               "replaced. Existing saves for registered Dusklight modes and their mod data will "
               "be backed up first.";
    } else if (canImportAll) {
        body = fmt::format("The card image contains {} registered mode saves:<br/>", items->size());
    } else {
        body = fmt::format("Replace the existing <b>{}</b> save? A backup will be made first.",
            escape(selected_mode_label()));
    }
    if (canImportAll) {
        for (const auto& item : *items) {
            body += fmt::format("{}<br/>", escape(mode_label(item.context.identity.saveName)));
        }
        body += fmt::format("<br/>Import the selected <b>{}</b> save or all of them? Each existing "
                            "save will be backed up first.",
            escape(selected_mode_label()));
    }
    if (!items->front().artifact.declaredMods.empty()) {
        body += "<br/><br/>Mod data in this save:<br/>";
        for (const auto& mod : items->front().artifact.declaredMods) {
            body += fmt::format("{} {}<br/>", escape(mod.id), escape(mod.version));
        }
    }

    const auto cancel = [](Modal& modal) {
        dismiss_modal(modal);
        finish_import_flow();
    };
    struct ImportOptions {
        bool all = false;
        bool keepModData = false;
    };
    auto options = std::make_shared<ImportOptions>();
    auto modal = std::make_unique<Modal>(Modal::Props{
        .title = "Import Save",
        .bodyRml = std::move(body),
        .actions =
            {
                {.label = "Cancel", .onPressed = cancel},
                {.label = "Import",
                    .onPressed =
                        [items, selectedIndex, options, hasBundledModData](Modal& modal) {
                            const auto modDataAction = hasBundledModData ? ModDataAction::Replace :
                                                       options->keepModData ? ModDataAction::Keep :
                                                                              ModDataAction::Clear;
                            perform_import(
                                items, selectedIndex, options->all, modDataAction, modal);
                        }},
            },
        .onDismiss = cancel,
        .icon = "warning",
    });
    if (canImportAll) {
        modal->content_pane().add_child<BoolButton>(BoolButton::Props{
            .key = "Import all modes",
            .getValue = [options] { return options->all; },
            .setValue = [options](bool value) { options->all = value; },
        });
    }
    if (!hasBundledModData) {
        auto& pane = modal->content_pane();
        pane.add_child<BoolButton>(BoolButton::Props{
            .key = "Keep existing mod data",
            .getValue = [options] { return options->keepModData; },
            .setValue = [options](bool value) { options->keepModData = value; },
        });
        pane.add_text("Leave this off unless the mod data belongs to the imported save.");
    }
    if (auto* host = top_document()) {
        host->push(std::move(modal));
    } else {
        finish_import_flow();
    }
}

void import_dialog_callback(borealis::file_select::Result result) {
    if (result.status == borealis::file_select::Status::Canceled) {
        return;
    }
    if (result.status != borealis::file_select::Status::Selected || result.locations.empty()) {
        show_message("Import Failed",
            result.message.empty() ? "The save file picker could not be opened." : result.message,
            true);
        return;
    }
    import_save_location(std::move(result.locations.front()));
}

void process_next_import() {
    if (s_importActive || s_pendingImports.empty()) {
        return;
    }
    s_importActive = true;
    std::string location = std::move(s_pendingImports.front());
    s_pendingImports.pop_front();
    auto artifact = save_manager::read_artifact(location);
    if (!artifact) {
        show_message("Import Failed", artifact.result.message, true, &finish_import_flow);
        return;
    }
    confirm_import(std::move(artifact.value));
}

void begin_import() {
    borealis::file_select::open_file(
        {
            .parentWindow = aurora::window::get_sdl_window(),
            .filters = {{"Save files", "gci;raw;dusksave"}},
        },
        &import_dialog_callback);
}

void begin_delete() {
    auto context = current_context();
    if (!context) {
        show_message("Delete Failed", context.result.message, true);
        return;
    }
    if (auto* host = top_document()) {
        host->push(std::make_unique<Modal>(Modal::Props{
            .title = "Delete Save",
            .bodyRml = fmt::format("Delete the <b>{}</b> save and its mod data? A backup will be "
                                   "made first.",
                escape(selected_mode_label())),
            .actions =
                {
                    {.label = "Cancel", .onPressed = &dismiss_modal},
                    {.label = "Delete",
                        .onPressed =
                            [context = context.value](Modal& modal) {
                                modal.pop();
                                show_result("The save was deleted.",
                                    save_manager::delete_save(context.storage, context.identity));
                            }},
                },
            .onDismiss = &dismiss_modal,
            .icon = "warning",
        }));
    }
}

class BackupsWindow final : public Window {
public:
    BackupsWindow() : Window{Props{.tabBar = false, .styleSheets = {"res/rml/saves.rcss"}}} {
        mRoot->SetClass("saves", true);
        mRoot->SetClass("backups", true);
        set_content([this](Rml::Element* content) { build(content); });
    }

private:
    static std::string display_name(const SaveIdentity& identity, const std::string& name) {
        const std::string prefix =
            save_manager::card_file_stem(identity.maker, identity.game, identity.saveName) + "-";
        if (!name.starts_with(prefix) || name.size() < prefix.size() + 16) {
            return name;
        }
        const std::string_view timestamp{name.data() + prefix.size(), 16};
        if (timestamp[8] != 'T' || timestamp[15] != 'Z') {
            return name;
        }
        return fmt::format("{}-{}-{} {}:{}:{} UTC", timestamp.substr(0, 4), timestamp.substr(4, 2),
            timestamp.substr(6, 2), timestamp.substr(9, 2), timestamp.substr(11, 2),
            timestamp.substr(13, 2));
    }

    void build(Rml::Element* content) {
        auto& listPane = add_child<Pane>(content, Pane::Type::Controlled);
        listPane.root()->SetClass("save-list", true);
        auto& detailPane = add_child<Pane>(content, Pane::Type::Uncontrolled);
        detailPane.root()->SetClass("save-detail", true);

        auto context = current_context();
        if (!context) {
            listPane.add_section("Backups");
            detailPane.add_text(context.result.message);
            return;
        }
        auto backups = save_manager::list_backups(context.value.storage, context.value.identity);
        if (!backups) {
            listPane.add_section("Backups");
            detailPane.add_text(backups.result.message);
            return;
        }

        listPane.add_section("Backups");
        if (backups.value.empty()) {
            detailPane.add_rml(fmt::format(
                R"(<div class="save-title">{} Backups</div>)"
                R"(<div class="save-subtitle">Recovery copies are created automatically</div>)",
                escape(selected_mode_label())));
            detailPane.add_section("No Backups Yet");
            detailPane
                .add_text(
                    "A backup will appear here before a save is imported, restored, or deleted.")
                ->SetClass("save-help", true);
            return;
        }
        if (std::ranges::none_of(backups.value,
                [this](const save_manager::BackupInfo& backup) {
                    return backup.path == mSelectedPath;
                }))
        {
            mSelectedPath = backups.value.front().path;
        }
        for (const auto& backup : backups.value) {
            const std::string label = display_name(context.value.identity, backup.name);
            auto& entry = listPane.add_group_button({
                .text = label,
                .isSelected = [this, path = backup.path] { return mSelectedPath == path; },
            });
            listPane.register_control(
                entry, detailPane, [this, context = context.value, backup, label](Pane& pane) {
                    mSelectedPath = backup.path;
                    build_detail(pane, context, backup, label);
                });
        }
        const auto selected =
            std::ranges::find(backups.value, mSelectedPath, &save_manager::BackupInfo::path);
        const auto& backup = selected == backups.value.end() ? backups.value.front() : *selected;
        build_detail(
            detailPane, context.value, backup, display_name(context.value.identity, backup.name));
    }

    void build_detail(
        Pane& pane, Context context, save_manager::BackupInfo backup, const std::string& label) {
        pane.add_rml(fmt::format(
            R"(<div class="save-title">Backup</div><div class="save-subtitle">{}</div>)",
            escape(selected_mode_label())));
        pane.add_section(label);
        pane.add_text(backup.name)->SetClass("save-path", true);
        pane.add_button("Restore This Backup...").on_pressed([this, context, path = backup.path] {
            if (auto* host = top_document()) {
                host->push(std::make_unique<Modal>(Modal::Props{
                    .title = "Restore Backup",
                    .bodyRml = "Replace the current save with this backup? The current save "
                               "will be backed up first.",
                    .actions =
                        {
                            {.label = "Cancel", .onPressed = &dismiss_modal},
                            {.label = "Restore",
                                .onPressed =
                                    [this, context, path](Modal& modal) {
                                        modal.pop();
                                        const Result result = save_manager::restore_backup(
                                            context.storage, context.identity, path);
                                        if (result) {
                                            ++s_refreshGeneration;
                                            rebuild_content();
                                        }
                                        show_message("Restore Backup",
                                            result ? "The backup was restored." : result.message,
                                            !result);
                                    }},
                        },
                    .onDismiss = &dismiss_modal,
                    .icon = "warning",
                }));
            }
        });
        auto& deleteButton = pane.add_button("Delete This Backup...");
        deleteButton.root()->SetClass("danger", true);
        deleteButton.on_pressed(
            [this, storage = context.storage, path = backup.path, name = backup.name] {
                if (auto* host = top_document()) {
                    host->push(std::make_unique<Modal>(Modal::Props{
                        .title = "Delete Backup",
                        .bodyRml = fmt::format("Delete <b>{}</b>?", escape(name)),
                        .actions =
                            {
                                {.label = "Cancel", .onPressed = &dismiss_modal},
                                {.label = "Delete",
                                    .onPressed =
                                        [this, storage, path](Modal& modal) {
                                            modal.pop();
                                            const Result result =
                                                save_manager::delete_backup(storage, path);
                                            if (result) {
                                                mSelectedPath.clear();
                                                rebuild_content();
                                            } else {
                                                show_message("Delete Backup", result.message, true);
                                            }
                                        }},
                            },
                        .onDismiss = &dismiss_modal,
                        .icon = "warning",
                    }));
                }
            });
    }

    std::filesystem::path mSelectedPath;
};

void open_backups() {
    if (auto* host = top_document()) {
        host->push(std::make_unique<BackupsWindow>());
    }
}

void open_save_folder() {
    auto context = current_context();
    if (!context) {
        show_message("Open Save Folder", context.result.message, true);
        return;
    }
    const auto folder = context.value.storage.kind == save_manager::StorageKind::GciDirectory ?
                            context.value.storage.path :
                            context.value.storage.path.parent_path();
    const std::string url = "file://" + borealis::io::fs_path_to_generic_string(folder);
    if (!SDL_OpenURL(url.c_str())) {
        show_message("Open Save Folder", SDL_GetError(), true);
    }
}

void confirm_delete_mod_data(Context context, std::string id) {
    if (auto* host = top_document()) {
        host->push(std::make_unique<Modal>(Modal::Props{
            .title = "Delete Mod Data",
            .bodyRml = fmt::format("Delete saved data for <b>{}</b>? If a game save exists, a "
                                   "backup will be made first.",
                escape(id)),
            .actions =
                {
                    {.label = "Cancel", .onPressed = &dismiss_modal},
                    {.label = "Delete",
                        .onPressed =
                            [context = std::move(context), id = std::move(id)](Modal& modal) {
                                modal.pop();
                                show_result("The mod data was deleted.",
                                    save_manager::delete_mod_data(
                                        context.storage, context.identity, id));
                            }},
                },
            .onDismiss = &dismiss_modal,
            .icon = "warning",
        }));
    }
}

class ModDataRow final : public Component {
public:
    ModDataRow(Rml::Element* parent, const Context& context, const save_manager::ModFileInfo& mod)
        : Component{append(parent, "save-mod")} {
        auto* info = append(mRoot, "save-mod-info");
        append_text(append(info, "b"), mod.id);
        append_text(
            append(info, "small"), fmt::format("{} · {}", format_bytes(mod.size),
                                       installed_mod(mod.id) ? "Installed" : "Not installed"));
        mDelete = &add_child<IconButton>(IconButton::Props{
            .icon = "delete",
            .label = fmt::format("Delete {} data", mod.id),
            .isDisabled = [] { return borealis::file_select::busy(); },
        });
        mDelete->root()->SetClass("danger", true);
        mDelete->on_pressed([context, id = mod.id] { confirm_delete_mod_data(context, id); });
    }

    bool focus() override { return mDelete->focus(); }

private:
    IconButton* mDelete = nullptr;
};

void build_save_detail(Pane& pane) {
    const std::string modeLabel = selected_mode_label();
    auto context = current_context();
    if (!context) {
        pane.add_rml(fmt::format(
            R"(<div class="save-title">{}</div><div class="save-subtitle">Unavailable</div>)",
            escape(modeLabel)));
        pane.add_section("Save File");
        pane.add_text(context.result.message)->SetClass("save-help", true);
        return;
    }

    const auto& storage = context.value.storage;
    const std::string storageLabel =
        storage.kind == save_manager::StorageKind::GciDirectory ? "GCI folder" : "Raw memory card";
    pane.add_rml(fmt::format(
        R"(<div class="save-title">{}</div><div class="save-subtitle">{} · Card A</div>)",
        escape(modeLabel), storageLabel));

    auto info = save_manager::inspect_save(storage, context.value.identity);
    pane.add_section("Save File");
    if (!info) {
        pane.add_text(info.result.message)->SetClass("save-help", true);
    } else if (info.value.present) {
        pane.add_rml(fmt::format(
            R"(<div class="save-overview present"><div class="save-overview-title">Save present</div>)"
            R"(<div class="save-overview-meta">{} · Modified {}</div></div>)",
            escape(format_bytes(info.value.size)),
            escape(save_manager::format_gc_time(info.value.modifiedTime))));
    } else {
        pane.add_rml(
            R"(<div class="save-overview empty"><div class="save-overview-title">No save yet</div>)"
            R"(<div class="save-overview-meta">Import a save or start this mode to create one.</div></div>)");
    }

    const bool savePresent = info && info.value.present;
    pane.add_section("Transfer");
    pane.add_text(
            "Export a portable Dusklight archive with mod data, or a standard GCI for other tools.")
        ->SetClass("save-help", true);
    auto& exportButton = pane.add_button(ControlledButton::Props{
        .text = "Export Save...",
        .isDisabled = [savePresent] { return !savePresent || borealis::file_select::busy(); },
    });
    exportButton.on_pressed([anchor = exportButton.root()] {
        push_document(
            std::make_unique<ContextMenu>(anchor, std::vector<ContextMenu::Item>{
                                                      {.text = "Save + mod data (.dusksave)",
                                                          .icon = "folder_open",
                                                          .onPressed = [] { begin_export(true); }},
                                                      {.text = "Save only (.gci)",
                                                          .icon = "description",
                                                          .onPressed = [] { begin_export(false); }},
                                                  }));
    });
    pane.add_button(ControlledButton::Props{
                        .text = "Import Save",
                        .isDisabled = [] { return borealis::file_select::busy(); },
                    })
        .on_pressed(&begin_import);

    if (info && !info.value.mods.empty()) {
        pane.add_section(fmt::format("Mod Data ({})", info.value.mods.size()));
        pane.add_text("Mod data is stored beside the game save and included in Dusklight archives.")
            ->SetClass("save-help", true);
        for (const auto& mod : info.value.mods) {
            pane.add_child<ModDataRow>(context.value, mod);
        }
    }

    pane.add_section("Storage and Recovery");
    pane.add_button("View Backups").on_pressed(&open_backups);
    if (storage.kind == save_manager::StorageKind::RawImage) {
        pane.add_button(ControlledButton::Props{
                            .text = "Export Full Card Image (.raw)",
                            .isDisabled =
                                [path = storage.path] {
                                    std::error_code ec;
                                    return borealis::file_select::busy() ||
                                           !std::filesystem::is_regular_file(path, ec);
                                },
                        })
            .on_pressed(&begin_raw_export);
    }
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS)
    pane.add_button("Open Save Folder").on_pressed(&open_save_folder);
#endif
    pane.add_rml(fmt::format(R"(<div class="save-path">{}</div>)",
        escape(borealis::io::fs_path_to_generic_string(storage.path))));

    if (savePresent) {
        pane.add_section("Danger Zone");
        auto& deleteButton = pane.add_button(ControlledButton::Props{
            .text = "Delete Save...",
            .isDisabled = [] { return borealis::file_select::busy(); },
        });
        deleteButton.root()->SetClass("danger", true);
        deleteButton.on_pressed(&begin_delete);
    }
}

}  // namespace

SavesWindow::SavesWindow() : Window{Props{.tabBar = false, .styleSheets = {"res/rml/saves.rcss"}}} {
    mRoot->SetClass("saves", true);
    set_content([this](Rml::Element* content) { build_content(content); });
}

void SavesWindow::build_content(Rml::Element* content) {
    ensure_selected_mode();
    mSaveName = s_selectedSaveName;
    mGeneration = s_refreshGeneration.load();

    auto& listPane = add_child<Pane>(content, Pane::Type::Controlled);
    listPane.root()->SetClass("save-list", true);
    auto& detailPane = add_child<Pane>(content, Pane::Type::Uncontrolled);
    detailPane.root()->SetClass("save-detail", true);

    listPane.add_section("Save Files");
    const auto modes = mode_options();
    if (modes.empty()) {
        listPane.add_text("No game modes are registered.");
        build_save_detail(detailPane);
        return;
    }
    for (const auto& mode : modes) {
        auto& entry = listPane.add_group_button({
            .text = mode.label,
            .isSelected = [saveName = mode.saveName] { return s_selectedSaveName == saveName; },
            .isDisabled = [] { return borealis::file_select::busy(); },
        });
        listPane.register_control(entry, detailPane, [this, saveName = mode.saveName](Pane& pane) {
            if (s_selectedSaveName != saveName) {
                s_selectedSaveName = saveName;
                mSaveName = saveName;
                mDoAud_seStartMenu(kSoundItemChange);
            }
            build_save_detail(pane);
        });
    }
    build_save_detail(detailPane);
}

void SavesWindow::update() {
    ensure_selected_mode();
    if (mSaveName != s_selectedSaveName || mGeneration != s_refreshGeneration.load()) {
        rebuild_content();
    }
    Window::update();
}

void add_save_files_control(Pane& leftPane, Pane& rightPane) {
    leftPane.register_control(
        leftPane.add_select_button({
            .key = "Save Files",
            .getValue = [] { return selected_mode_label(); },
            .isDisabled =
                [] {
                    const auto& state = prelaunch_state();
                    return !state.configuredDiscCanLaunch || data::is_data_path_restart_pending() ||
                           (!state.activeDiscPath.empty() &&
                               state.configuredDiscPath != state.activeDiscPath) ||
                           state.configuredDiscInfo.platform != iso::Platform::GameCube;
                },
        }),
        rightPane, [](Pane& pane) {
            pane.add_section("Save Files");
            pane.add_text(
                    "Import, export, back up, and remove saves for each registered game mode.")
                ->SetClass("save-help", true);
            pane.add_button("Open Save Manager").on_pressed([] {
                if (auto* host = top_document()) {
                    host->push(std::make_unique<SavesWindow>());
                }
            });
        });
}

void import_save_location(std::string location) {
    if (!is_prelaunch_open()) {
        push_toast({
            .type = "warning",
            .title = "Save Import",
            .content = "Return to the main menu before importing save files.",
            .duration = std::chrono::seconds{4},
        });
        return;
    }
    s_pendingImports.push_back(std::move(location));
    process_next_import();
}

}  // namespace dusk::ui
