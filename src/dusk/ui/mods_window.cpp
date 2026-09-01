#include "mods_window.hpp"

#include "dusk/mod_loader.hpp"
#include "dusk/mods/svc/ui.hpp"
#include "fmt/format.h"
#include "logs_window.hpp"
#include "mod_browser.hpp"
#include "mod_texture_provider.hpp"
#include "mods/svc/http.h"
#include "pane.hpp"

#include <borealis/http.hpp>

#include "Z2AudioLib/Z2SeMgr.h"
#include "m_Do/m_Do_audio.h"

#include <memory>
#include <ranges>
#include <string>
#include <string_view>

namespace dusk::ui {
namespace {

struct ModStatus {
    const char* badgeClass = "";
    const char* text = "";
};

bool mod_enabled(const mods::LoadedMod& mod) {
    return mod.cvarIsEnabled != nullptr && mod.cvarIsEnabled->getValue();
}

ModStatus mod_status(const mods::LoadedMod& mod) {
    if (mod.loadFailed) {
        return {"failed", "Failed"};
    }
    if (mod.active) {
        return {"active", "Active"};
    }
    if (mod.suspendedByProvider) {
        return {"suspended", "Suspended"};
    }
    return {"", "Disabled"};
}

bool mod_uses_network(const mods::LoadedMod& mod) {
    return std::ranges::any_of(
        mod.manifestInfo.imports, [](const mods::ModManifestInfo::Import& serviceImport) {
            return serviceImport.id == HTTP_SERVICE_ID;
        });
}

// Truncates to at most maxBytes without splitting a UTF-8 sequence.
std::string snippet(std::string_view text, size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return std::string{text};
    }
    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return std::string{text.substr(0, end)} + "...";
}

class ModListEntry : public FluentComponent<ModListEntry> {
public:
    ModListEntry(Rml::Element* parent, const mods::LoadedMod& mod)
        : FluentComponent{append(parent, "mod-entry")} {
        Rml::Element* icon = nullptr;
        if (!mod.metadata.iconPath.empty()) {
            icon = append(mRoot, "img");
            icon->SetAttribute("src", mod_image_source(mod, mod.metadata.iconPath));
        } else {
            icon = append(mRoot, "icon");
            icon->SetClass("placeholder", true);
        }
        icon->SetClass("mod-icon", true);

        const auto status = mod_status(mod);
        auto* info = append(mRoot, "mod-entry-info");
        auto* name = append(info, "mod-entry-name");
        append_text(append(name, "mod-entry-name-text"), mod.metadata.name);
        append_text(append(name, "mod-entry-version"), "v" + mod.metadata.version);
        auto* sub = append(info, "mod-entry-sub");
        append_text(sub, mod.metadata.author + " - ");
        auto* statusElement = append(sub, "mod-entry-status");
        if (status.badgeClass[0] != '\0') {
            statusElement->SetClass(status.badgeClass, true);
        }
        append_text(statusElement, status.text);
        if (mod_uses_network(mod)) {
            append_text(append(sub, "mod-entry-network"), "Network");
        }
        append_text(append(info, "mod-entry-desc"), snippet(mod.metadata.description, 90));
        mRoot->SetClass("inactive", !mod.active);
        mRoot->SetClass("failed", mod.loadFailed);

        on_nav_command([this](Rml::Event&, NavCommand cmd) {
            if (cmd == NavCommand::Confirm) {
                mRoot->DispatchEvent(Rml::EventId::Submit, {});
                return true;
            }
            return false;
        });
    }
};

class ModDetailHeader : public FluentComponent<ModDetailHeader> {
public:
    ModDetailHeader(
        Rml::Element* parent, const mods::LoadedMod& mod, std::function<void()> onShowLogs)
        : FluentComponent{append(parent, "mod-header")} {
        const bool hasBanner = !mod.metadata.bannerPath.empty();
        mRoot->SetClass(hasBanner ? "has-banner" : "no-banner", true);
        if (hasBanner) {
            mRoot->SetProperty("decorator", fmt::format(R"(image("{}" cover center top))",
                                                mod_image_source(mod, mod.metadata.bannerPath)));
        }

        auto* actions = append(mRoot, "mod-actions");
        const std::string modId = mod.metadata.id;
        if (mod_enabled(mod)) {
            if (!mod.inPlace) {
                make_button(actions, "Reload").on_pressed([modId] {
                    mods::ModLoader::instance().request_reload(modId);
                });
            }
            make_button(actions, "Disable").on_pressed([modId] {
                mods::ModLoader::instance().request_disable(modId);
            });
        } else {
            make_button(actions, "Enable").on_pressed([modId] {
                mods::ModLoader::instance().request_enable(modId);
            });
        }
        make_button(actions, "Logs").on_pressed(std::move(onShowLogs));

        listen(Rml::EventId::Keydown, [this](Rml::Event& event) {
            const auto cmd = map_nav_event(event);
            if (cmd != NavCommand::Left && cmd != NavCommand::Right) {
                return;
            }
            int index = -1;
            for (int i = 0; i < static_cast<int>(mButtons.size()); ++i) {
                if (mButtons[i]->contains(event.GetTargetElement())) {
                    index = i;
                    break;
                }
            }
            if (index == -1) {
                return;
            }
            const int next = index + (cmd == NavCommand::Right ? 1 : -1);
            if (next >= 0 && next < static_cast<int>(mButtons.size()) && mButtons[next]->focus()) {
                mDoAud_seStartMenu(kSoundItemFocus);
                event.StopPropagation();
            }
        });
    }

    bool focus() override {
        for (auto* button : mButtons) {
            if (button->focus()) {
                return true;
            }
        }
        return false;
    }

private:
    Button& make_button(Rml::Element* parent, Rml::String text) {
        auto button = std::make_unique<Button>(parent, std::move(text));
        Button& ref = *button;
        mChildren.emplace_back(std::move(button));
        mButtons.push_back(&ref);
        return ref;
    }

    std::vector<Button*> mButtons;
};

}  // namespace

ModsWindow::ModsWindow() : Window{Props{.tabBar = false, .styleSheets = {"res/rml/mods.rcss"}}} {
    mRoot->SetClass("mods", true);

    for (auto& trackedMod : mods::ModLoader::instance().mods()) {
        mSnapshot.push_back({
            .mod = &trackedMod,
            .active = trackedMod.active,
            .loadFailed = trackedMod.loadFailed,
            .enabled = mod_enabled(trackedMod),
            .suspended = trackedMod.suspendedByProvider,
            .cacheGeneration = trackedMod.cacheGeneration,
        });
    }

    set_content([this](Rml::Element* content) { build_content(content); });
}

void ModsWindow::build_content(Rml::Element* content) {
    mEntries.clear();
    mEntryMods.clear();
    mBrowserEntry = nullptr;

    auto& listPane = add_child<Pane>(content, Pane::Type::Controlled);
    listPane.root()->SetClass("mod-list", true);
    auto& detailPane = add_child<Pane>(content, Pane::Type::Uncontrolled);
    detailPane.root()->SetClass("mod-detail", true);

    if (borealis::http::available()) {
        auto& browse = listPane.add_button("Browse online mods");
        mBrowserEntry = &browse;
        browse.root()->SetClass("mod-browser-entry", true);
        browse.on_pressed([this] { push(std::make_unique<ModBrowser>()); });
        listPane.register_control(browse, detailPane, [this](Pane& pane) {
            mBrowserSelected = true;
            mSelectedMod = nullptr;
            build_browser_detail(pane);
            mark_current_entry();
        });
    }

    if (mods::ModLoader::instance().mods().empty()) {
        listPane.add_text("No mods installed.");
        if (borealis::http::available()) {
            mBrowserSelected = true;
            build_browser_detail(detailPane);
        }
        mark_current_entry();
        return;
    }

    for (auto& trackedMod : mods::ModLoader::instance().mods()) {
        auto& entry = listPane.add_child<ModListEntry>(trackedMod);
        mEntries.push_back(&entry);
        mEntryMods.push_back(&trackedMod);
        listPane.register_control(entry, detailPane, [this, tracked = &trackedMod](Pane& pane) {
            mBrowserSelected = false;
            mSelectedMod = tracked;
            pane.clear();
            build_detail(pane, *tracked);
            mark_current_entry();
        });
    }

    if (mBrowserSelected && mBrowserEntry != nullptr) {
        mSelectedMod = nullptr;
        build_browser_detail(detailPane);
    } else if (mSelectedMod == nullptr) {
        mSelectedMod = mEntryMods.front();
        build_detail(detailPane, *mSelectedMod);
    } else {
        build_detail(detailPane, *mSelectedMod);
    }
    mark_current_entry();
}

void ModsWindow::build_browser_detail(Pane& pane) {
    pane.root()->RemoveAttribute("mod-id");
    append_text(append(pane.root(), "mod-title"), "Browse Mods");
    pane.add_text("Find published Dusklight mods in the online catalog.");
}

void ModsWindow::build_detail(Pane& pane, mods::LoadedMod& mod) {
    pane.root()->SetAttribute("mod-id", mod.metadata.id);
    pane.add_child<ModDetailHeader>(
        mod, [this, id = mod.metadata.id] { push(std::make_unique<LogsWindow>(id)); });

    auto* title = append(pane.root(), "mod-title");
    append_text(title, mod.metadata.name + " ");
    append_text(append(title, "mod-title-version"), "v" + mod.metadata.version);
    if (mod.loadFailed || mod.suspendedByProvider) {
        const auto status = mod_status(mod);
        append_text(title, "\u00a0");
        auto* badge = append(title, "status-badge");
        badge->SetClass(status.badgeClass, true);
        append_text(badge, status.text);
    }
    if (mod_uses_network(mod)) {
        append_text(title, "\u00a0");
        auto* badge = append(title, "status-badge");
        badge->SetClass("network", true);
        append_text(badge, "Network");
    }
    append_text(append(pane.root(), "mod-author"), "by " + mod.metadata.author);

    if (mod.loadFailed && !mod.failureReason.empty()) {
        auto* row = append(pane.root(), "mod-info-row");
        auto* label = append(row, "mod-info-label");
        label->SetClass("failed", true);
        append_text(label, "Reason");
        append_text(append(row, "mod-info-value"), mod.failureReason);
    } else if (mod.suspendedByProvider) {
        std::string providers;
        for (const auto& edge : mod.dependencies) {
            if (edge.required && edge.mod != nullptr && !edge.mod->active) {
                if (!providers.empty()) {
                    providers += ", ";
                }
                providers += edge.mod->metadata.name;
            }
        }
        auto* row = append(pane.root(), "mod-info-row");
        append_text(append(row, "mod-info-label"), "Waiting on");
        append_text(append(row, "mod-info-value"), providers);
    }

    std::string activeDependents;
    for (const auto& edge : mod.dependents) {
        if (edge.mod != nullptr && edge.mod->active) {
            if (!activeDependents.empty()) {
                activeDependents += ", ";
            }
            activeDependents += edge.mod->metadata.name;
        }
    }
    if (mod.active && !activeDependents.empty()) {
        append_text(append(pane.root(), "mod-restart-note"),
            fmt::format("Disabling or reloading also restarts: {}", activeDependents));
    }

    if (!mod.metadata.description.empty()) {
        auto* description = append(pane.root(), "mod-description");
        append_text(description, mod.metadata.description);
    }

    if (mod.active) {
        mods::svc::ui_build_mods_panels(mod, pane);
    }
}

void ModsWindow::mark_current_entry() {
    if (mBrowserEntry != nullptr) {
        mBrowserEntry->root()->SetClass("current", mBrowserSelected);
    }
    for (size_t i = 0; i < mEntries.size(); ++i) {
        mEntries[i]->root()->SetClass("current", mEntryMods[i] == mSelectedMod);
    }
}

void ModsWindow::update() {
    bool dirty = false;
    for (auto& snapshot : mSnapshot) {
        const auto& mod = *snapshot.mod;
        if (mod.active != snapshot.active || mod.loadFailed != snapshot.loadFailed ||
            mod_enabled(mod) != snapshot.enabled || mod.suspendedByProvider != snapshot.suspended ||
            mod.cacheGeneration != snapshot.cacheGeneration)
        {
            snapshot.active = mod.active;
            snapshot.loadFailed = mod.loadFailed;
            snapshot.enabled = mod_enabled(mod);
            snapshot.suspended = mod.suspendedByProvider;
            snapshot.cacheGeneration = mod.cacheGeneration;
            dirty = true;
        }
    }
    if (dirty) {
        auto* focused = mDocument != nullptr ? mDocument->GetFocusLeafNode() : nullptr;
        bool hadContentFocus = false;
        for (auto* node = focused; node != nullptr; node = node->GetParentNode()) {
            if (node == mContentRoot) {
                hadContentFocus = true;
                break;
            }
        }
        rebuild_content();
        if (hadContentFocus) {
            if (mBrowserSelected && mBrowserEntry != nullptr) {
                mBrowserEntry->focus();
            } else {
                for (size_t i = 0; i < mEntryMods.size(); ++i) {
                    if (mEntryMods[i] == mSelectedMod) {
                        mEntries[i]->focus();
                        break;
                    }
                }
            }
        }
    }

    if (mSelectedMod != nullptr && mSelectedMod->active) {
        mods::svc::ui_update_mods_panels(*mSelectedMod);
    }

    Window::update();
}

}  // namespace dusk::ui
