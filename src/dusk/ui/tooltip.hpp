#pragma once

#include "event.hpp"

namespace dusk::ui {

// A plain-text hover/focus label. Destroy before its anchor and document.
class Tooltip {
public:
    Tooltip(Rml::Element* anchor, const Rml::String& label);
    ~Tooltip();

    Tooltip(const Tooltip&) = delete;
    Tooltip& operator=(const Tooltip&) = delete;

    // Call after updating the anchor's visibility and disabled state each frame.
    void update();

private:
    Rml::Element* mAnchor;
    Rml::Element* mRoot;
    bool mFollowsFocus;
    ScopedEventListener mMouseMove;
    ScopedEventListener mMouseDown;
    ScopedEventListener mKeyDown;
};

}  // namespace dusk::ui
