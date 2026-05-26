#pragma once

#include "document.hpp"

#include <array>

union SDL_Event;

namespace dusk::ui {

class TouchControls final : public Document {
public:
    TouchControls();
    ~TouchControls() override;

    void show() override;
    void hide(bool close) override;
    void update() override;
    void handle_event(const SDL_Event& event) noexcept;

    enum class FixedControl {
        A,
        B,
        X,
        Y,
        L,
        RTrigger,
        Z,
        FirstPerson,
        Items,
        Collections,
        Map,
    };

private:
    struct StickTouch {
        long long id = 0;
        Rml::Vector2f start;
        Rml::Vector2f current;
        bool active = false;
    };

    struct FixedTouch {
        long long id = 0;
        FixedControl control = FixedControl::A;
        bool active = false;
    };

    void set_fixed_control(FixedControl control, bool pressed);
    void set_control_visual(FixedControl control, bool pressed) noexcept;
    void sync_l_lock_state() noexcept;
    void clear_virtual_input() noexcept;
    void sync_virtual_input() noexcept;
    void sync_visibility() noexcept;
    void sync_safe_area() noexcept;
    void sync_visual_state() noexcept;
    void handle_touch_down(const SDL_Event& event) noexcept;
    void handle_touch_motion(const SDL_Event& event) noexcept;
    void handle_touch_up(const SDL_Event& event) noexcept;
    bool control_at_point(Rml::Vector2f position, FixedControl& control) const noexcept;
    bool release_fixed_touch(long long id) noexcept;

    Rml::Element* mRoot = nullptr;
    Rml::Element* mMoveStick = nullptr;
    Rml::Element* mMoveKnob = nullptr;
    Rml::Element* mLTarget = nullptr;
    StickTouch mMoveTouch;
    StickTouch mCameraTouch;
    std::array<FixedTouch, 10> mFixedTouches{};
    Insets mSafeInsets;
    int mObservedTouches = 0;
    u16 mButtonMask = 0;
    bool mLPressed = false;
    bool mLLatched = false;
    bool mLReleasePending = false;
    bool mRTriggerHeld = false;
    bool mFirstPersonHeld = false;
    bool mWantsVirtualPad = false;
    bool mWasSuppressed = true;
};

void handle_touch_controls_event(const SDL_Event& event) noexcept;

}  // namespace dusk::ui
