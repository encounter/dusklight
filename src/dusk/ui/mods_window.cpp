#include "mods_window.hpp"

#include "dusk/mod_loader.hpp"
#include "dusk/mods/loader/loader.hpp"
#include "fmt/format.h"
#include "pane.hpp"

namespace dusk::ui {
namespace {

Rml::String build_mod_detail_rml(const dusk::LoadedMod& mod) {
    const char* statusClass;
    const char* statusText;
    if (mod.load_failed) {
        statusClass = "locked";
        statusText = "Failed";
    } else if (mod.active) {
        statusClass = "unlocked";
        statusText = "Active";
    } else {
        statusClass = "";
        statusText = "Disabled";
    }

    Rml::String failureRow;
    if (mod.load_failed && !mod.failure_reason.empty()) {
        failureRow = fmt::format(
            R"(<div class="mod-info-row">)"
            R"(<span class="mod-info-label">Failure</span>)"
            R"(<span class="mod-info-value">{}</span>)"
            R"(</div>)",
            escape(mod.failure_reason));
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
        mod.mod_path
    );
}

}  // namespace

ModsWindow::ModsWindow() {
    const auto& mods = dusk::ModLoader::instance().mods();

    if (mods.empty()) {
        add_tab("Mods", [this](Rml::Element* content) {
            auto& pane = add_child<Pane>(content, Pane::Type::Uncontrolled);
            pane.add_text("No mods installed.");
            pane.finalize();
        });
        return;
    }

    for (ModIndex i = 0; i < mods.size(); ++i) {
        mSnapshot.push_back({mods[i].active, mods[i].load_failed});

        add_tab(mods[i].metadata.name, [this, i](Rml::Element* content) {
            mActiveModIndex = static_cast<int>(i);

            auto curMods = dusk::ModLoader::instance().mods();
            if (i >= curMods.size()) {
                return;
            }
            auto& mod = curMods[i];

            auto& pane = add_child<Pane>(content, Pane::Type::Uncontrolled);

            pane.add_section("Details");
            pane.add_rml(build_mod_detail_rml(mod));

            if (!mod.metadata.description.empty()) {
                pane.add_section("Description");
                pane.add_text(mod.metadata.description);
            }

            for (const auto& cb : mod.ui_tabs) {
                if (cb.tab.build == nullptr) {
                    continue;
                }
                ModError error = MOD_ERROR_INIT;
                const auto result =
                    cb.tab.build(cb.context, static_cast<void*>(pane.root()), cb.tab.userdata,
                        &error);
                if (result != MOD_OK) {
                    dusk::mods::loader::fail_mod(
                        mod, result,
                        error.message[0] != '\0' ? error.message : "mod UI build failed");
                    break;
                }
            }

            pane.finalize();
        });
    }
}

void ModsWindow::update() {
    const auto& mods = dusk::ModLoader::instance().mods();

    bool dirty = mods.size() != mSnapshot.size();
    if (!dirty) {
        for (ModIndex i = 0; i < mods.size(); ++i) {
            if (mods[i].active != mSnapshot[i].active ||
                mods[i].load_failed != mSnapshot[i].load_failed)
            {
                dirty = true;
                break;
            }
        }
    }

    if (dirty) {
        mSnapshot.clear();
        for (const auto& mod : mods) {
            mSnapshot.push_back({mod.active, mod.load_failed});
        }
        refresh_active_tab();
    }

    if (mActiveModIndex >= 0 && static_cast<size_t>(mActiveModIndex) < mods.size()) {
        auto& mod = mods[mActiveModIndex];
        for (const auto& cb : mod.ui_tabs) {
            if (cb.tab.update == nullptr) {
                continue;
            }
            ModError error = MOD_ERROR_INIT;
            const auto result = cb.tab.update(cb.context, cb.tab.userdata, &error);
            if (result != MOD_OK) {
                dusk::mods::loader::fail_mod(
                    mod, result,
                    error.message[0] != '\0' ? error.message : "mod UI update failed");
                break;
            }
        }
    }

    Window::update();
}

}  // namespace dusk::ui
