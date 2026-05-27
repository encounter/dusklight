#pragma once

#include "document.hpp"

#include <array>

namespace dusk::ui {

class TouchControls final : public Document {
public:
    TouchControls();
    ~TouchControls() override;

    void show() override;
    void hide(bool close) override;
    void update() override;

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
        SDL_FingerID id = 0;
        Rml::Vector2f start;
        Rml::Vector2f current;
        bool active = false;
    };
    struct FixedTouch {
        SDL_FingerID id = 0;
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
    void handle_touch_down(Rml::Event& event) noexcept;
    void handle_touch_motion(Rml::Event& event) noexcept;
    void handle_touch_up(Rml::Event& event) noexcept;
    bool release_fixed_touch(SDL_FingerID id) noexcept;

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
    bool mManualLLatched = false;
    bool mLReleasePending = false;
    bool mRTriggerHeld = false;
    bool mFirstPersonHeld = false;
    bool mWantsVirtualPad = false;
    bool mWasSuppressed = true;
    clock::time_point mLPressStartTime{};
    clock::time_point mLastLTapTime{};
};

}  // namespace dusk::ui
