#pragma once

#include "pane.hpp"
#include "window.hpp"

#include <climits>

namespace dusk::ui {

// Neutral description of a mod-provided input control; the mod loader
// translates the C ABI descriptor (and its cvar binding) into these
// std::functions so this layer stays free of mod API types.
struct ModControlSpec {
    enum class Kind : u8 {
        Button,
        Toggle,
        Number,
        String,
        Select,
    };

    Kind kind = Kind::Button;
    Rml::String label;
    // Contextual help RML shown in the help pane while the control is focused
    Rml::String helpRml;
    std::function<void()> onPressed;
    std::function<bool()> getBool;
    std::function<void(bool)> setBool;
    // Number value, or the selected option index for Select
    std::function<int()> getInt;
    std::function<void(int)> setInt;
    std::function<Rml::String()> getString;
    std::function<void(Rml::String)> setString;
    std::function<bool()> isDisabled;
    std::function<bool()> isModified;
    int min = 0;
    int max = INT_MAX;
    int step = 1;
    Rml::String prefix;
    Rml::String suffix;
    std::vector<Rml::String> options;
    int maxLength = -1;
};

// Builds the control in `pane`. When helpPane is given, helpRml (and Select
// options) render there while the control is focused or hovered; Select
// requires a help pane. Returns the control, owned by `pane`.
Component* build_mod_control(Pane& pane, Pane* helpPane, ModControlSpec spec);

// A mod-owned tabbed two-pane window pushed onto the document stack.
class ModWindow : public Window {
public:
    struct Tab {
        Rml::String title;
        // Receives the freshly created left (controlled) and right
        // (uncontrolled) panes on every tab activation
        std::function<void(ModWindow&, Pane& left, Pane& right)> build;
        // Called every frame while this tab is active
        std::function<void()> update;
    };
    struct Desc {
        std::vector<Tab> tabs;
        Rml::String rcss;
        // Fired from the destructor, whatever the close reason
        std::function<void()> onDestroyed;
        // Whether closing should show the document below again (true when that
        // document was visible at push time, mirroring Document::push/pop)
        bool showTopOnClose = true;
    };

    explicit ModWindow(Desc desc);
    ~ModWindow() override;

    void update() override;
    // Marks the document closed without the close animation (mod teardown)
    void force_close() { Document::hide(true); }

protected:
    bool consume_close_request() override;

private:
    Desc mDesc;
    int mActiveTab = -1;
};

}  // namespace dusk::ui
