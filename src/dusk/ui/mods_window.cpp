#include "mods_window.hpp"

#include "dusk/mod_loader.hpp"
#include "dusk/mods/loader/loader.hpp"
#include "fmt/format.h"
#include "pane.hpp"

namespace dusk::ui {
namespace {

Rml::String build_mod_detail_rml(const mods::LoadedMod& mod) {
    const char* statusClass;
    const char* statusText;
    if (mod.loadFailed) {
        statusClass = "locked";
        statusText = "Failed";
    } else if (mod.active) {
        statusClass = "unlocked";
        statusText = "Active";
    } else if (mod.suspendedByProvider) {
        statusClass = "locked";
        statusText = "Suspended";
    } else {
        statusClass = "";
        statusText = "Disabled";
    }

    Rml::String failureRow;
    if (mod.loadFailed && !mod.failureReason.empty()) {
        failureRow = fmt::format(
            R"(<div class="mod-info-row">)"
            R"(<span class="mod-info-label">Failure</span>)"
            R"(<span class="mod-info-value">{}</span>)"
            R"(</div>)",
            escape(mod.failureReason));
    } else if (mod.suspendedByProvider) {
        std::string providers;
        for (const auto& edge : mod.dependencies) {
            if (edge.required && !edge.mod->active) {
                if (!providers.empty()) {
                    providers += ", ";
                }
                providers += edge.mod->metadata.name;
            }
        }
        failureRow = fmt::format(
            R"(<div class="mod-info-row">)"
            R"(<span class="mod-info-label">Waiting on</span>)"
            R"(<span class="mod-info-value">{}</span>)"
            R"(</div>)",
            escape(providers));
    }

    return fmt::format(
        R"(<div class="mod-info-row">)"
        R"(<span class="mod-info-label">Version</span>)"
        R"(<span class="mod-info-value">{}</span>)"
        R"(</div>)"
        R"(<div class="mod-info-row">)"
        R"(<span class="mod-info-label">Author</span>)"
        R"(<span class="mod-info-value">{}</span>)"
        R"(</div>)"
        R"(<div class="mod-info-row">)"
        R"(<span class="mod-info-label">Status</span>)"
        R"(<span class="achievement-badge {}">{}</span>)"
        R"(</div>)"
        R"({})"
        R"(<div class="mod-info-row">)"
        R"(<span class="mod-info-label">Path</span>)"
        R"(<span class="mod-info-value mod-path">{}</span>)"
        R"(</div>)",
        mod.metadata.version,
        mod.metadata.author,
        statusClass, statusText,
        failureRow,
        mod.modPath
    );
}

}  // namespace

ModsWindow::ModsWindow() {
    const auto& mods = mods::ModLoader::instance().mods();

    if (mods.empty()) {
        add_tab("Mods", [this](Rml::Element* content) {
            auto& pane = add_child<Pane>(content, Pane::Type::Uncontrolled);
            pane.add_text("No mods installed.");
            pane.finalize();
        });
        return;
    }

    // Tabs hold stable LoadedMod pointers, not indices: a reload that changes a mod's
    // imports/exports reorders m_mods mid-session.
    for (auto& trackedMod : mods) {
        mSnapshot.push_back({&trackedMod, trackedMod.active, trackedMod.loadFailed,
            trackedMod.cvarIsEnabled->getValue(), trackedMod.suspendedByProvider});

        add_tab(trackedMod.metadata.name, [this, tracked = &trackedMod](Rml::Element* content) {
            mActiveMod = tracked;
            auto& mod = *tracked;

            auto& pane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            pane.add_section("Details");
            pane.add_rml(build_mod_detail_rml(mod));

            if (!mod.metadata.description.empty()) {
                pane.add_section("Description");
                pane.add_text(mod.metadata.description);
            }

            pane.add_section("Actions");
            const std::string modId = mod.metadata.id;
            if (mod.cvarIsEnabled->getValue()) {
                pane.add_button("Disable").on_pressed(
                    [modId] { mods::ModLoader::instance().request_disable(modId); });
            } else {
                pane.add_button("Enable").on_pressed(
                    [modId] { mods::ModLoader::instance().request_enable(modId); });
            }
            pane.add_button("Reload").on_pressed(
                [modId] { mods::ModLoader::instance().request_reload(modId); });

            std::string activeDependents;
            for (const auto& edge : mod.dependents) {
                if (edge.mod->active) {
                    if (!activeDependents.empty()) {
                        activeDependents += ", ";
                    }
                    activeDependents += edge.mod->metadata.name;
                }
            }
            if (mod.active && !activeDependents.empty()) {
                pane.add_text(
                    fmt::format("Disabling or reloading also restarts: {}", activeDependents));
            }

            for (const auto& cb : mod.uiTabs) {
                if (cb.tab.build == nullptr) {
                    continue;
                }
                ModError error = MOD_ERROR_INIT;
                const auto result = cb.tab.build(
                    cb.context, static_cast<void*>(pane.root()), cb.tab.userdata, &error);
                if (result != MOD_OK) {
                    mods::fail_mod(mod, result,
                        error.message[0] != '\0' ? error.message : "mod UI build failed");
                    break;
                }
            }

            pane.finalize();
        });
    }
}

void ModsWindow::update() {
    bool dirty = false;
    for (auto& snapshot : mSnapshot) {
        const auto& mod = *snapshot.mod;
        if (mod.active != snapshot.active || mod.loadFailed != snapshot.load_failed ||
            mod.cvarIsEnabled->getValue() != snapshot.enabled ||
            mod.suspendedByProvider != snapshot.suspended)
        {
            snapshot.active = mod.active;
            snapshot.load_failed = mod.loadFailed;
            snapshot.enabled = mod.cvarIsEnabled->getValue();
            snapshot.suspended = mod.suspendedByProvider;
            dirty = true;
        }
    }
    if (dirty) {
        refresh_active_tab();
    }

    if (mActiveMod != nullptr) {
        auto& mod = *mActiveMod;
        for (const auto& cb : mod.uiTabs) {
            if (cb.tab.update == nullptr) {
                continue;
            }
            ModError error = MOD_ERROR_INIT;
            const auto result = cb.tab.update(cb.context, cb.tab.userdata, &error);
            if (result != MOD_OK) {
                mods::fail_mod(
                    mod, result, error.message[0] != '\0' ? error.message : "mod UI update failed");
                break;
            }
        }
    }

    Window::update();
}

}  // namespace dusk::ui
