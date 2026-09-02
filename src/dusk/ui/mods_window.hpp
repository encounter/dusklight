#pragma once

#include "window.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include "dusk/mod_loader.hpp"

namespace dusk::ui {

class Pane;

class ModsWindow : public Window {
public:
    ModsWindow();
    void update() override;

private:
    struct ModSnapshot {
        mods::LoadedMod* mod = nullptr;
        bool active = false;
        bool loadFailed = false;
        bool enabled = false;
        bool suspended = false;
        u32 cacheGeneration = 0;
    };

    void build_content(Rml::Element* content);
    void build_detail(Pane& pane, mods::LoadedMod& mod);
    void confirm_uninstall(const mods::LoadedMod& mod);
    void refresh_snapshot();
    void mark_current_entry();

    std::vector<ModSnapshot> mSnapshot;
    std::vector<Component*> mEntries;
    std::vector<mods::LoadedMod*> mEntryMods;
    Component* mBrowserEntry = nullptr;
    mods::LoadedMod* mSelectedMod = nullptr;
    std::string mSelectedModId;
    uint64_t mLoaderGeneration = 0;
    size_t mQueueItemCount = 0;
    bool mBrowserSelected = false;
};

}  // namespace dusk::ui
