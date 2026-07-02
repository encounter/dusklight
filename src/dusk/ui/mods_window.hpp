#pragma once

#include "window.hpp"

#include <vector>

#include "dusk/mod_loader.hpp"

namespace dusk::ui {

class ModsWindow : public Window {
public:
    ModsWindow();
    void update() override;

private:
    struct ModSnapshot {
        LoadedMod* mod;
        bool active;
        bool load_failed;
        bool enabled;
        bool suspended;
    };
    std::vector<ModSnapshot> mSnapshot;
    LoadedMod* mActiveMod = nullptr;
};

}  // namespace dusk::ui
