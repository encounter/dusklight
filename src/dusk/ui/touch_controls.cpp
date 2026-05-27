#include "touch_controls.hpp"

#include <aurora/rmlui.hpp>
#include <dolphin/pad.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_player.h"
#include "d/d_com_inf_game.h"
#include "dusk/action_bindings.h"
#include "dusk/settings.h"
#include "dusk/touch_camera.h"
#include "ui.hpp"

namespace dusk::ui {
namespace {

constexpr u32 kPort = PAD_CHAN0;
constexpr float kStickRadiusDp = 62.0f;
constexpr float kStickKnobRadiusDp = 24.0f;
constexpr float kAnalogZoneTopDp = 92.0f;
constexpr float kAnalogZoneBottomDp = 30.0f;
constexpr float kLeftZoneWidth = 0.46f;
constexpr float kRightZoneStart = 0.52f;
constexpr u8 kTriggerAnalog = 180;
constexpr u8 kLTargetAnalog = 180;
constexpr auto kLDoubleTapWindow = std::chrono::milliseconds(300);

const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/touch_controls.rcss" />
</head>
<body id="root">
    <touch-stick id="move-stick">
        <stick-ring />
        <stick-knob id="move-knob" />
    </touch-stick>

    <button id="l-target" class="touch-control trigger l-target"><span>L</span></button>
    <top-actions class="touch-control">
        <button id="first-person" class="utility first-person"><icon /></button>
        <separator />
        <button id="map" class="utility map"><icon /></button>
        <separator />
        <button id="items" class="utility items"><icon /></button>
        <separator />
        <button id="collections" class="utility collections"><icon /></button>
    </top-actions>

    <button id="r-trigger" class="touch-control trigger r-trigger"><span>R</span></button>
    <button id="z-trigger" class="touch-control trigger z-trigger"><span>Z</span></button>

    <face-cluster>
        <button id="button-y" class="touch-control face y"><span>Y</span></button>
        <button id="button-x" class="touch-control face x"><span>X</span></button>
        <button id="button-b" class="touch-control face b"><span>B</span></button>
        <button id="button-a" class="touch-control face a"><span>A</span></button>
    </face-cluster>
</body>
</rml>
)RML";

SDL_FingerID touch_id(const Rml::Event& event) noexcept {
    return event.GetParameter<SDL_FingerID>("finger_id", 0);
}

Rml::Vector2f touch_position(const Rml::Event& event) noexcept {
    return {
        event.GetParameter("x", 0.0f),
        event.GetParameter("y", 0.0f),
    };
}

float dp_scale() noexcept {
    auto* context = aurora::rmlui::get_context();
    if (context == nullptr) {
        return 1.0f;
    }
    return std::max(context->GetDensityIndependentPixelRatio(), 1.0f);
}

s8 stick_value(float value) noexcept {
    return static_cast<s8>(std::clamp(std::lround(value * 127.0f), -127l, 127l));
}

u16 button_for_fixed_control(TouchControls::FixedControl control) noexcept {
    switch (control) {
    case TouchControls::FixedControl::A:
        return PAD_BUTTON_A;
    case TouchControls::FixedControl::B:
        return PAD_BUTTON_B;
    case TouchControls::FixedControl::X:
        return PAD_BUTTON_X;
    case TouchControls::FixedControl::Y:
        return PAD_BUTTON_Y;
    case TouchControls::FixedControl::Items:
        return PAD_BUTTON_UP;
    case TouchControls::FixedControl::Collections:
        return PAD_BUTTON_START;
    case TouchControls::FixedControl::Map:
        return PAD_BUTTON_RIGHT;
    case TouchControls::FixedControl::Z:
        return PAD_TRIGGER_Z;
    default:
        return 0;
    }
}

bool player_attention_locked() noexcept {
    const auto* player = daPy_getPlayerActorClass();
    return player != nullptr && (player->checkAttentionLock() || player->checkEnemyAttentionLock());
}

bool player_enemy_locked_on() noexcept {
    const auto* player = daPy_getPlayerActorClass();
    return player != nullptr && player->checkEnemyAttentionLock();
}

bool touch_aim_active() noexcept {
    daAlink_c* link = daAlink_getAlinkActorClass();
    return link != nullptr && link->checkGyroAimContext() &&
           dComIfGp_checkCameraAttentionStatus(link->field_0x317c, 0x10);
}

}  // namespace

TouchControls::TouchControls()
    : Document(kDocumentSource, true),
      mRoot(mDocument != nullptr ? mDocument->GetElementById("root") : nullptr),
      mMoveStick(mDocument != nullptr ? mDocument->GetElementById("move-stick") : nullptr),
      mMoveKnob(mDocument != nullptr ? mDocument->GetElementById("move-knob") : nullptr),
      mLTarget(mDocument != nullptr ? mDocument->GetElementById("l-target") : nullptr) {
    listen(mRoot, Rml::EventId::Transitionend, [this](Rml::Event& event) {
        if (event.GetTargetElement() == mRoot && !mRoot->HasAttribute("open") &&
            Document::visible())
        {
            Document::hide(mPendingClose);
        }
    });

    listen(
        mRoot, aurora::rmlui::TouchStartEvent,
        [this](Rml::Event& event) {
            if (!visible() || mWasSuppressed) {
                return;
            }
            mObservedTouches++;
            if (mObservedTouches >= 3) {
                clear_virtual_input();
                event.StopImmediatePropagation();
            }
        },
        true);
    listen(
        mRoot, aurora::rmlui::TouchEndEvent,
        [this](Rml::Event&) { mObservedTouches = std::max(0, mObservedTouches - 1); }, true);
    listen(
        mRoot, aurora::rmlui::TouchCancelEvent,
        [this](Rml::Event&) { mObservedTouches = std::max(0, mObservedTouches - 1); }, true);

    auto listenFixedControl = [this](const char* id, FixedControl control) {
        auto* element = mDocument != nullptr ? mDocument->GetElementById(id) : nullptr;
        if (element == nullptr) {
            return;
        }
        listen(element, aurora::rmlui::TouchStartEvent, [this, control](Rml::Event& event) {
            if (!visible() || mWasSuppressed) {
                return;
            }
            const auto id = touch_id(event);
            for (auto& touch : mFixedTouches) {
                if (!touch.active) {
                    touch = {
                        .id = id,
                        .control = control,
                        .active = true,
                    };
                    set_fixed_control(control, true);
                    event.StopPropagation();
                    return;
                }
            }
        });
        listen(element, aurora::rmlui::TouchEndEvent, [this](Rml::Event& event) {
            if (release_fixed_touch(touch_id(event))) {
                sync_virtual_input();
                event.StopPropagation();
            }
        });
        listen(element, aurora::rmlui::TouchCancelEvent, [this](Rml::Event& event) {
            if (release_fixed_touch(touch_id(event))) {
                sync_virtual_input();
                event.StopPropagation();
            }
        });
    };
    listenFixedControl("button-a", FixedControl::A);
    listenFixedControl("button-b", FixedControl::B);
    listenFixedControl("button-x", FixedControl::X);
    listenFixedControl("button-y", FixedControl::Y);
    listenFixedControl("l-target", FixedControl::L);
    listenFixedControl("r-trigger", FixedControl::RTrigger);
    listenFixedControl("z-trigger", FixedControl::Z);
    listenFixedControl("first-person", FixedControl::FirstPerson);
    listenFixedControl("items", FixedControl::Items);
    listenFixedControl("collections", FixedControl::Collections);
    listenFixedControl("map", FixedControl::Map);

    listen(mRoot, aurora::rmlui::TouchStartEvent,
        [this](Rml::Event& event) { handle_touch_down(event); });
    listen(mRoot, aurora::rmlui::TouchMoveEvent,
        [this](Rml::Event& event) { handle_touch_motion(event); });
    listen(
        mRoot, aurora::rmlui::TouchEndEvent, [this](Rml::Event& event) { handle_touch_up(event); });
    listen(mRoot, aurora::rmlui::TouchCancelEvent,
        [this](Rml::Event& event) { handle_touch_up(event); });
}

TouchControls::~TouchControls() {
    clear_virtual_input();
    clearAllVirtualActionBinds();
}

void TouchControls::show() {
    Document::show();
    if (mRoot != nullptr) {
        mRoot->SetAttribute("open", "");
    }
}

void TouchControls::hide(bool close) {
    clear_virtual_input();
    if (mRoot != nullptr) {
        mRoot->RemoveAttribute("open");
        mPendingClose = close;
    } else {
        Document::hide(close);
    }
}

void TouchControls::set_fixed_control(FixedControl control, bool pressed) {
    set_control_visual(control, pressed);
    const u16 button = button_for_fixed_control(control);
    if (button != 0) {
        if (pressed) {
            mButtonMask |= button;
        } else {
            mButtonMask &= ~button;
        }
    }

    switch (control) {
    case FixedControl::L:
        if (pressed && (mLLatched || mManualLLatched)) {
            mLLatched = false;
            mManualLLatched = false;
            mLPressed = false;
            mLReleasePending = true;
            mLPressStartTime = {};
            mLastLTapTime = {};
            set_control_visual(control, false);
        } else if (pressed) {
            const auto now = clock::now();
            if (!player_enemy_locked_on() && mLastLTapTime != clock::time_point{} &&
                now - mLastLTapTime <= kLDoubleTapWindow)
            {
                mManualLLatched = true;
                mLPressed = false;
                mLReleasePending = true;
                mLPressStartTime = {};
                mLastLTapTime = {};
            } else if (!mLReleasePending) {
                mLPressed = true;
                mLPressStartTime = now;
            }
        } else if (!mLReleasePending) {
            mLPressed = false;
        }
        if (!pressed) {
            const auto now = clock::now();
            if (!mLReleasePending) {
                const bool wasQuickTap = mLPressStartTime != clock::time_point{} &&
                                         now - mLPressStartTime <= kLDoubleTapWindow;
                mLastLTapTime = wasQuickTap ? now : clock::time_point{};
            }
            mLPressStartTime = {};
            mLReleasePending = false;
        }
        if (!pressed && !player_enemy_locked_on()) {
            mLLatched = false;
        }
        break;
    case FixedControl::RTrigger:
        mRTriggerHeld = pressed;
        break;
    case FixedControl::FirstPerson:
        mFirstPersonHeld = pressed;
        break;
    default:
        break;
    }
    sync_virtual_input();
}

void TouchControls::set_control_visual(FixedControl control, bool pressed) noexcept {
    const char* id = nullptr;
    switch (control) {
    case FixedControl::A:
        id = "button-a";
        break;
    case FixedControl::B:
        id = "button-b";
        break;
    case FixedControl::X:
        id = "button-x";
        break;
    case FixedControl::Y:
        id = "button-y";
        break;
    case FixedControl::L:
        id = "l-target";
        break;
    case FixedControl::RTrigger:
        id = "r-trigger";
        break;
    case FixedControl::Z:
        id = "z-trigger";
        break;
    case FixedControl::FirstPerson:
        id = "first-person";
        break;
    case FixedControl::Items:
        id = "items";
        break;
    case FixedControl::Collections:
        id = "collections";
        break;
    case FixedControl::Map:
        id = "map";
        break;
    }

    auto* element = mDocument != nullptr && id != nullptr ? mDocument->GetElementById(id) : nullptr;
    if (element != nullptr) {
        element->SetClass("pressed", pressed);
    }
}

void TouchControls::sync_l_lock_state() noexcept {
    if (player_enemy_locked_on()) {
        if (mLPressed) {
            mLLatched = true;
        }
    } else {
        mLLatched = false;
    }
}

void TouchControls::clear_virtual_input() noexcept {
    mMoveTouch = {};
    mCameraTouch = {};
    mFixedTouches = {};
    mButtonMask = 0;
    mLPressed = false;
    mLLatched = false;
    mManualLLatched = false;
    mLReleasePending = false;
    mLPressStartTime = {};
    mLastLTapTime = {};
    mRTriggerHeld = false;
    mFirstPersonHeld = false;
    mWantsVirtualPad = false;
    mObservedTouches = 0;
    PADClearVirtualStatus(kPort);
    touch_camera::clear();
    setVirtualActionBind(ActionBinds::FIRST_PERSON_CAMERA, kPort, false, false);
    if (mMoveStick != nullptr) {
        mMoveStick->SetClass("active", false);
    }
    if (mDocument != nullptr) {
        for (const char* id : {"button-a", "button-b", "button-x", "button-y", "l-target",
                 "r-trigger", "z-trigger", "first-person", "items", "collections", "map"})
        {
            if (auto* element = mDocument->GetElementById(id)) {
                element->SetClass("pressed", false);
            }
        }
    }
    sync_visual_state();
}

void TouchControls::sync_virtual_input() noexcept {
    if (mWasSuppressed || !getSettings().game.enableTouchControls) {
        clear_virtual_input();
        return;
    }

    sync_l_lock_state();
    const bool aimActive = touch_aim_active();

    if (aimActive && mMoveTouch.active) {
        if (!mCameraTouch.active) {
            mCameraTouch = mMoveTouch;
            mCameraTouch.start = mMoveTouch.current;
        }
        mMoveTouch = {};
    }

    PADStatus status{};
    status.err = PAD_ERR_NONE;
    status.button = mButtonMask;

    if (mLPressed || mLLatched || mManualLLatched) {
        status.button |= PAD_TRIGGER_L;
        status.triggerLeft = kLTargetAnalog;
    }
    if (mRTriggerHeld) {
        status.button |= PAD_TRIGGER_R;
        status.triggerRight = kTriggerAnalog;
    }

    const float stickRadius = kStickRadiusDp * dp_scale();
    if (mMoveTouch.active && stickRadius > 0.0f) {
        Rml::Vector2f delta = mMoveTouch.current - mMoveTouch.start;
        const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        const float scale = length > stickRadius ? stickRadius / length : 1.0f;
        delta *= scale;
        status.stickX = stick_value(delta.x / stickRadius);
        status.stickY = stick_value(-delta.y / stickRadius);

        if (mMoveStick != nullptr) {
            const float knobRadius = kStickKnobRadiusDp * dp_scale();
            mMoveStick->SetClass("active", true);
            mMoveStick->SetProperty(Rml::PropertyId::Left,
                Rml::Property(mMoveTouch.start.x - stickRadius, Rml::Unit::PX));
            mMoveStick->SetProperty(Rml::PropertyId::Top,
                Rml::Property(mMoveTouch.start.y - stickRadius, Rml::Unit::PX));
            if (mMoveKnob != nullptr) {
                mMoveKnob->SetProperty(Rml::PropertyId::Left,
                    Rml::Property(stickRadius + delta.x - knobRadius, Rml::Unit::PX));
                mMoveKnob->SetProperty(Rml::PropertyId::Top,
                    Rml::Property(stickRadius + delta.y - knobRadius, Rml::Unit::PX));
            }
        }
    } else if (mMoveStick != nullptr) {
        mMoveStick->SetClass("active", false);
    }

    mWantsVirtualPad = status.button != 0 || status.stickX != 0 || status.stickY != 0 ||
                       status.triggerLeft != 0 || status.triggerRight != 0;
    if (mWantsVirtualPad) {
        PADSetVirtualStatus(kPort, &status);
    } else {
        PADClearVirtualStatus(kPort);
    }

    setVirtualActionBind(
        ActionBinds::FIRST_PERSON_CAMERA, kPort, mFirstPersonHeld,
        mFirstPersonHeld && visible() && !mWasSuppressed);
    sync_visual_state();
}

void TouchControls::sync_visibility() noexcept {
    mWasSuppressed = any_document_visible();
    if (getSettings().game.enableTouchControls && !mWasSuppressed) {
        show();
    } else if (visible()) {
        hide(false);
    } else {
        clear_virtual_input();
    }
}

void TouchControls::sync_safe_area() noexcept {
    if (mRoot == nullptr || mDocument == nullptr) {
        return;
    }
    const auto insets = safe_area_insets(mDocument->GetContext());
    if (insets == mSafeInsets) {
        return;
    }
    mSafeInsets = insets;
    mRoot->SetProperty(Rml::PropertyId::PaddingTop, Rml::Property(insets.top, Rml::Unit::PX));
    mRoot->SetProperty(Rml::PropertyId::PaddingRight, Rml::Property(insets.right, Rml::Unit::PX));
    mRoot->SetProperty(Rml::PropertyId::PaddingBottom, Rml::Property(insets.bottom, Rml::Unit::PX));
    mRoot->SetProperty(Rml::PropertyId::PaddingLeft, Rml::Property(insets.left, Rml::Unit::PX));
}

void TouchControls::sync_visual_state() noexcept {
    if (mLTarget != nullptr) {
        mLTarget->SetClass(
            "active", mLPressed || mLLatched || mManualLLatched || player_attention_locked());
    }
}

void TouchControls::update() {
    sync_visibility();
    sync_safe_area();
    sync_virtual_input();
}

bool TouchControls::release_fixed_touch(SDL_FingerID id) noexcept {
    for (auto& touch : mFixedTouches) {
        if (touch.active && touch.id == id) {
            set_fixed_control(touch.control, false);
            touch = {};
            return true;
        }
    }
    return false;
}

void TouchControls::handle_touch_down(Rml::Event& event) noexcept {
    if (!visible() || mWasSuppressed) {
        return;
    }

    const auto position = touch_position(event);
    auto* context = aurora::rmlui::get_context();
    if (context == nullptr) {
        return;
    }

    const auto id = touch_id(event);
    if (touch_aim_active()) {
        if (!mCameraTouch.active) {
            mCameraTouch = {
                .id = id,
                .start = position,
                .current = position,
                .active = true,
            };
        }
        sync_virtual_input();
        return;
    }

    const auto dimensions = context->GetDimensions();
    const float top = mSafeInsets.top + kAnalogZoneTopDp * dp_scale();
    const float bottom =
        static_cast<float>(dimensions.y) - mSafeInsets.bottom - kAnalogZoneBottomDp * dp_scale();
    if (position.y < top || position.y > bottom) {
        return;
    }

    const auto width = static_cast<float>(dimensions.x);
    if (!mMoveTouch.active && position.x < width * kLeftZoneWidth) {
        mMoveTouch = {
            .id = id,
            .start = position,
            .current = position,
            .active = true,
        };
    } else if (!mCameraTouch.active && position.x > width * kRightZoneStart) {
        mCameraTouch = {
            .id = id,
            .start = position,
            .current = position,
            .active = true,
        };
    }
    sync_virtual_input();
}

void TouchControls::handle_touch_motion(Rml::Event& event) noexcept {
    if (!visible() || mWasSuppressed) {
        return;
    }

    const auto id = touch_id(event);
    const auto position = touch_position(event);
    if (mMoveTouch.active && mMoveTouch.id == id) {
        mMoveTouch.current = position;
    }
    if (mCameraTouch.active && mCameraTouch.id == id) {
        const Rml::Vector2f delta = position - mCameraTouch.current;
        mCameraTouch.current = position;
        const float scale = dp_scale();
        touch_camera::add_delta(delta.x / scale, delta.y / scale);
    }
    sync_virtual_input();
}

void TouchControls::handle_touch_up(Rml::Event& event) noexcept {
    if (!visible() || mWasSuppressed) {
        return;
    }

    const auto id = touch_id(event);
    if (release_fixed_touch(id)) {
        sync_virtual_input();
        return;
    }
    if (mMoveTouch.active && mMoveTouch.id == id) {
        mMoveTouch = {};
    }
    if (mCameraTouch.active && mCameraTouch.id == id) {
        mCameraTouch = {};
    }
    sync_virtual_input();
}

}  // namespace dusk::ui
