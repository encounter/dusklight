#include "row.hpp"

#include "m_Do/m_Do_audio.h"

#include <algorithm>
#include <cmath>

namespace dusk::ui {

Row::Row(Rml::Element* parent, Props props) : FluentComponent{append(parent, "ui-row")} {
    const char* align = "flex-start";
    switch (props.align) {
    case Align::Center:
        align = "center";
        break;
    case Align::End:
        align = "flex-end";
        break;
    case Align::SpaceBetween:
        align = "space-between";
        break;
    default:
        break;
    }
    mRoot->SetProperty("justify-content", align);
    mRoot->SetProperty("flex-wrap", props.wrap ? "wrap" : "nowrap");
    listen(Rml::EventId::Keydown, [this](Rml::Event& event) {
        const auto cmd = map_nav_event(event);
        if (cmd != NavCommand::Left && cmd != NavCommand::Right) {
            return;
        }
        const int step = cmd == NavCommand::Left ? -1 : 1;
        for (int i = 0; i < static_cast<int>(mChildren.size()); ++i) {
            if (!mChildren[i]->contains(event.GetTargetElement())) {
                continue;
            }
            for (i += step; i >= 0 && i < static_cast<int>(mChildren.size()); i += step) {
                if (mChildren[i]->focus_from(cmd)) {
                    mDoAud_seStartMenu(kSoundItemFocus);
                    event.StopPropagation();
                    return;
                }
            }
            return;
        }
    });
}

bool Row::focus() {
    if (disabled() || !mRoot->IsVisible(true)) {
        return false;
    }
    if (mSelected != nullptr && mSelected->root()->IsVisible(true) && mSelected->focus()) {
        return true;
    }
    for (const auto& child : mChildren) {
        if (child->root()->IsVisible(true) && child->focus()) {
            return true;
        }
    }
    return false;
}

bool Row::focus_from(NavCommand direction) {
    if (disabled() || !mRoot->IsVisible(true)) {
        return false;
    }
    std::vector<Component*> candidates;
    for (const auto& child : mChildren) {
        candidates.push_back(child.get());
    }
    if (direction == NavCommand::Left) {
        std::reverse(candidates.begin(), candidates.end());
    } else if (direction == NavCommand::Up || direction == NavCommand::Down) {
        // Keep the horizontal position when moving between rows of controls.
        auto* focused = mRoot->GetContext()->GetFocusElement();
        if (focused != nullptr) {
            const float x = focused->GetAbsoluteOffset().x + focused->GetBox().GetSize().x * 0.5f;
            std::stable_sort(candidates.begin(), candidates.end(), [x](Component* a, Component* b) {
                const auto distance = [x](Component* c) {
                    return std::abs(c->root()->GetAbsoluteOffset().x +
                                    c->root()->GetBox().GetSize().x * 0.5f - x);
                };
                return distance(a) < distance(b);
            });
        }
    }
    for (auto* child : candidates) {
        if (child->root()->IsVisible(true) && child->focus_from(direction)) {
            return true;
        }
    }
    return false;
}

bool Row::selected() const {
    return std::any_of(mChildren.begin(), mChildren.end(),
                       [](const auto& child) { return child->selected(); });
}

void Row::set_selected(bool value) {
    auto* focused = mRoot->GetContext()->GetFocusElement();
    if (value && contains(focused)) {
        for (const auto& child : mChildren) {
            if (child->contains(focused)) {
                mSelected = child.get();
            }
        }
    }
    for (const auto& child : mChildren) {
        child->set_selected(value && child.get() == mSelected);
    }
    if (!value) {
        mSelected = nullptr;
    }
}

}  // namespace dusk::ui
