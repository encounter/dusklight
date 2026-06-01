#include "touch_controls.hpp"

#include <aurora/rmlui.hpp>
#include <dolphin/pad.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_player.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_info.h"
#include "d/d_msg_object.h"
#include "dusk/action_bindings.h"
#include "dusk/settings.h"
#include "dusk/touch_camera.h"
#include "f_op/f_op_overlap_mng.h"
#include "item_icon_provider.hpp"
#include "m_Do/m_Do_graphic.h"
#include "ui.hpp"

namespace dusk::ui {
namespace {

constexpr u32 kPort = PAD_CHAN0;
constexpr float kStickRadiusDp = 62.f;
constexpr float kStickKnobRadiusDp = 24.f;
constexpr float kAnalogZoneTopDp = 92.f;
constexpr float kAnalogZoneBottomDp = 30.f;
constexpr float kLeftZoneWidth = 0.46f;
constexpr float kRightZoneStart = 0.52f;
constexpr u8 kTriggerAnalog = 180;
constexpr auto kLDoubleTapWindow = std::chrono::milliseconds(300);
constexpr auto kHoldActionDuration = std::chrono::milliseconds(450);
constexpr float kFaceIconTargetRatio = 0.76f;
constexpr size_t kEquipTargetCount = 4;

std::array<EquipTarget, kEquipTargetCount> sEquipTargets{};
std::array<ControlOverride, static_cast<std::size_t>(Control::COUNT)> sControlOverrides{};
TouchControls* sTouchControls = nullptr;

struct ControlInfo {
    const char* id = nullptr;
    const char* iconId = nullptr;
    const char* oilId = nullptr;
    const char* oilFillId = nullptr;
    const char* countId = nullptr;
    u16 padButton = 0;
    std::optional<ActionBinds> tapAction;
    std::optional<ActionBinds> holdAction;
};

constexpr std::array<ControlInfo, static_cast<std::size_t>(Control::COUNT)> kControls = {{
    {
        .id = "button-a",
        .padButton = PAD_BUTTON_A,
    },
    {
        .id = "button-b",
        .iconId = "button-b-icon",
        .oilId = "button-b-oil",
        .oilFillId = "button-b-oil-fill",
        .padButton = PAD_BUTTON_B,
    },
    {
        .id = "button-x",
        .iconId = "button-x-icon",
        .oilId = "button-x-oil",
        .oilFillId = "button-x-oil-fill",
        .countId = "button-x-count",
        .padButton = PAD_BUTTON_X,
    },
    {
        .id = "button-y",
        .iconId = "button-y-icon",
        .oilId = "button-y-oil",
        .oilFillId = "button-y-oil-fill",
        .countId = "button-y-count",
        .padButton = PAD_BUTTON_Y,
    },
    {
        .id = "button-z",
        .iconId = "z-midna-icon",
        .padButton = PAD_TRIGGER_Z,
    },
    {
        .id = "trigger-l",
    },
    {
        .id = "trigger-r",
    },
    {
        .id = "first-person",
        .tapAction = ActionBinds::FIRST_PERSON_CAMERA,
    },
    {
        .id = "items",
        .padButton = PAD_BUTTON_UP,
    },
    {
        .id = "collections",
        .padButton = PAD_BUTTON_START,
    },
    {
        .id = "map",
        .tapAction = ActionBinds::OPEN_MAP_SCREEN,
        .holdAction = ActionBinds::TOGGLE_MINIMAP,
    },
    {
        .id = "skip",
        .padButton = PAD_BUTTON_START,
    },
    {
        .id = "dpad-up",
        .padButton = PAD_BUTTON_UP,
    },
    {
        .id = "dpad-down",
        .padButton = PAD_BUTTON_DOWN,
    },
    {
        .id = "dpad-left",
        .padButton = PAD_BUTTON_LEFT,
    },
    {
        .id = "dpad-right",
        .padButton = PAD_BUTTON_RIGHT,
    },
}};

constexpr const ControlInfo* control_info(Control control) noexcept {
    const auto index = static_cast<std::size_t>(control);
    return index < kControls.size() ? &kControls[index] : nullptr;
}

const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/touch_controls.rcss" />
</head>
<body id="root">
    <touch-stick id="control-stick">
        <stick-ring />
        <stick-knob id="control-knob" />
    </touch-stick>

    <dpad-cluster id="dpad-cluster">
        <dpad-center />
        <button id="dpad-up" class="touch-control dpad-button up"><icon /></button>
        <button id="dpad-down" class="touch-control dpad-button down"><icon /></button>
        <button id="dpad-left" class="touch-control dpad-button left"><icon /></button>
        <button id="dpad-right" class="touch-control dpad-button right"><icon /></button>
    </dpad-cluster>

    <button id="trigger-l" class="touch-control trigger trigger-l"><span>L</span></button>
    <action-bar id="action-bar" class="touch-control">
        <button id="items" class="utility items"><icon /></button>
        <separator />
        <button id="first-person" class="utility first-person"><icon /></button>
        <separator />
        <button id="map" class="utility map"><icon /></button>
        <separator />
        <button id="collections" class="utility collections"><icon /></button>
    </action-bar>
    <button id="skip" class="touch-control skip"><icon /></button>

    <button id="trigger-r" class="touch-control trigger trigger-r"><span>R</span></button>
    <button id="button-z" class="touch-control trigger button-z midna"><img id="z-midna-icon" class="midna-icon" /><span>Z</span></button>

    <face-cluster>
        <button id="button-y" class="touch-control face y"><img id="button-y-icon" class="item-icon" /><oil-meter id="button-y-oil" class="oil-meter"><oil-fill id="button-y-oil-fill" /></oil-meter><count id="button-y-count" class="item-count"></count><span>Y</span></button>
        <button id="button-x" class="touch-control face x"><img id="button-x-icon" class="item-icon" /><oil-meter id="button-x-oil" class="oil-meter"><oil-fill id="button-x-oil-fill" /></oil-meter><count id="button-x-count" class="item-count"></count><span>X</span></button>
        <button id="button-b" class="touch-control face b"><img id="button-b-icon" class="item-icon" /><oil-meter id="button-b-oil" class="oil-meter"><oil-fill id="button-b-oil-fill" /></oil-meter><span>B</span></button>
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
        event.GetParameter("x", 0.f),
        event.GetParameter("y", 0.f),
    };
}

float dp_scale() noexcept {
    auto* context = aurora::rmlui::get_context();
    if (context == nullptr) {
        return 1.f;
    }
    return std::max(context->GetDensityIndependentPixelRatio(), 1.f);
}

s8 stick_value(float value) noexcept {
    return static_cast<s8>(std::clamp(std::lround(value * 127.f), -127l, 127l));
}

bool player_attention_locked() noexcept {
    const auto* player = daPy_getPlayerActorClass();
    return player != nullptr && (player->checkAttentionLock() || player->checkEnemyAttentionLock());
}

bool item_wheel_active() noexcept {
    return dMeter2Info_getWindowStatus() == 2;
}

bool fixed_dpad_visible() noexcept {
    if (fpcM_SearchByName(fpcNm_NAME_SCENE_e) != nullptr) {
        return true;
    }
    switch (dMeter2Info_getWindowStatus()) {
    case 0:  // Normal
    case 2:  // Item wheel
    case 4:  // Field map screen
    case 5:  // Dungeon map screen
        return false;
    default:
        return true;
    }
}

bool fishing_controls_active() noexcept {
    const auto* player = daAlink_getAlinkActorClass();
    if (player == nullptr) {
        return false;
    }
    return player->checkCanoeFishingWaitAnime();
}

enum class StickOutput {
    MainStick,
    CStick,
};

StickOutput stick_output_mode() noexcept {
    if (fishing_controls_active()) {
        return StickOutput::CStick;
    }
    return StickOutput::MainStick;
}

bool controls_available(bool allowItemWheel) noexcept {
    if (dComIfGp_getLinkPlayer() == nullptr) {
        return false;
    }

    const auto* fader = mDoGph_gInf_c::getFader();
    if (fader == nullptr || fader->getStatus() != JUTFader::Wait || mDoGph_gInf_c::isFade()) {
        return false;
    }

    const bool itemWheelActive = allowItemWheel && item_wheel_active();
    const auto heapLock = dComIfGp_isHeapLockFlag();
    if ((heapLock != 0 && heapLock != 5 && !(itemWheelActive && heapLock == 1)) ||
        (dComIfGp_isPauseFlag() && !itemWheelActive) || dComIfGp_getMesgStatus() != 0 ||
        dComIfGp_isEnableNextStage() || fopOvlpM_IsDoingReq())
    {
        return false;
    }

    return true;
}

Rml::Vector2f clamped_stick_delta(
    Rml::Vector2f start, Rml::Vector2f current, float stickRadius) noexcept {
    Rml::Vector2f delta = current - start;
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    delta *= length > stickRadius ? stickRadius / length : 1.f;
    return delta;
}

struct FaceButtonState {
    std::string iconSource;
    uint64_t iconRevision = 0;
    bool visible = false;
    bool showIcon = false;
};

void release_rml_texture(const std::string& source) noexcept {
    if (!source.empty()) {
        Rml::ReleaseTexture(source);
    }
}

FaceButtonState override_button_state(Control control) {
    switch (sControlOverrides[static_cast<size_t>(control)]) {
    case ControlOverride::Action:
        return {
            .iconSource = "",
            .visible = true,
            .showIcon = false,
        };
    case ControlOverride::Default:
    default:
        return {};
    }
}

bool game_controls_suppressed() noexcept {
    return !controls_available(true) || dComIfGp_event_runCheck() ||
           (dComIfGp_getMsgObjectClass() != nullptr && dMsgObject_isTalkNowCheck());
}

FaceButtonState xy_button_state(Control control) {
    if (sControlOverrides[static_cast<size_t>(control)] != ControlOverride::Default) {
        return override_button_state(control);
    }
    if (game_controls_suppressed()) {
        return {};
    }

    const bool itemMode = dComIfGp_getLinkPlayer() != nullptr && daPy_py_c::checkNowWolf() == 0;
    const auto source = itemMode ? item_icon_source_for_button(control) : std::string();
    return {
        .iconSource = source,
        .visible = true,
        .showIcon = itemMode && !source.empty(),
    };
}

FaceButtonState z_button_state() {
    if (sControlOverrides[static_cast<size_t>(Control::Z)] != ControlOverride::Default) {
        return override_button_state(Control::Z);
    }
    if (game_controls_suppressed()) {
        return {};
    }

    const auto source = midna_icon_source();
    return {
        .iconSource = source,
        .iconRevision = midna_icon_revision(),
        .visible = true,
        .showIcon = !source.empty(),
    };
}

FaceButtonState b_button_state() {
    if (game_controls_suppressed()) {
        return {
            .iconSource = "",
            .visible = true,
            .showIcon = false,
        };
    }

    const auto source = item_icon_source_for_button(Control::B);
    return {
        .iconSource = source,
        .visible = true,
        .showIcon = dMeter2Info_isUseButton(METER2_USEBUTTON_B) && !source.empty(),
    };
}

void clear_equip_targets() noexcept {
    for (auto& target : sEquipTargets) {
        target.valid = false;
    }
}

void sync_equip_target(int slot, Rml::Element* element, float widthRatio, float heightRatio,
    bool square = false) noexcept {
    if (slot < 0 || static_cast<size_t>(slot) >= sEquipTargets.size()) {
        return;
    }

    auto& target = sEquipTargets[slot];
    target.valid = false;

    auto* context = aurora::rmlui::get_context();
    if (element == nullptr || context == nullptr) {
        return;
    }

    const auto dimensions = context->GetDimensions();
    if (dimensions.x <= 0 || dimensions.y <= 0) {
        return;
    }

    const auto buttonPosition = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto buttonSize = element->GetBox().GetSize(Rml::BoxArea::Border);
    if (buttonSize.x <= 0.f || buttonSize.y <= 0.f) {
        return;
    }

    Rml::Vector2f targetSize{buttonSize.x * widthRatio, buttonSize.y * heightRatio};
    if (square) {
        const float side = std::min(buttonSize.x, buttonSize.y) * widthRatio;
        targetSize = Rml::Vector2f{side, side};
    }
    const auto targetPosition = buttonPosition + (buttonSize - targetSize) * 0.5f;
    const float scaleX = mDoGph_gInf_c::getWidthF() / static_cast<float>(dimensions.x);
    const float scaleY = mDoGph_gInf_c::getHeightF() / static_cast<float>(dimensions.y);

    target.left = mDoGph_gInf_c::getMinXF() + targetPosition.x * scaleX;
    target.top = mDoGph_gInf_c::getMinYF() + targetPosition.y * scaleY;
    target.width = targetSize.x * scaleX;
    target.height = targetSize.y * scaleY;
    target.valid = true;
}

}  // namespace

bool get_equip_target(int slot, EquipTarget& target) noexcept {
    if (slot < 0 || static_cast<size_t>(slot) >= sEquipTargets.size()) {
        return false;
    }

    const auto& stored = sEquipTargets[slot];
    if (!stored.valid) {
        return false;
    }

    target = stored;
    return true;
}

void set_control_override(Control control, ControlOverride override) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index >= sControlOverrides.size()) {
        return;
    }
    sControlOverrides[index] = override;
}

void sync_virtual_input() noexcept {
    if (sTouchControls != nullptr) {
        sTouchControls->sync_virtual_input();
    }
}

TouchControls::TouchControls()
    : Document(kDocumentSource, true),
      mRoot(mDocument != nullptr ? mDocument->GetElementById("root") : nullptr),
      mControlStick(mDocument != nullptr ? mDocument->GetElementById("control-stick") : nullptr),
      mControlKnob(mDocument != nullptr ? mDocument->GetElementById("control-knob") : nullptr),
      mDPadCluster(mDocument != nullptr ? mDocument->GetElementById("dpad-cluster") : nullptr),
      mActionBar(mDocument != nullptr ? mDocument->GetElementById("action-bar") : nullptr) {
    sTouchControls = this;
    if (mDocument != nullptr) {
        for (std::size_t i = 0; i < kControls.size(); ++i) {
            const auto& info = kControls[i];
            auto& elements = mControlElements[i];
            elements.root = info.id != nullptr ? mDocument->GetElementById(info.id) : nullptr;
            elements.icon =
                info.iconId != nullptr ? mDocument->GetElementById(info.iconId) : nullptr;
            elements.oil = info.oilId != nullptr ? mDocument->GetElementById(info.oilId) : nullptr;
            elements.oilFill =
                info.oilFillId != nullptr ? mDocument->GetElementById(info.oilFillId) : nullptr;
            elements.count =
                info.countId != nullptr ? mDocument->GetElementById(info.countId) : nullptr;
        }
    }

    listen(mRoot, Rml::EventId::Transitionend, [this](Rml::Event& event) {
        if (event.GetTargetElement() == mRoot && !mRoot->HasAttribute("open") &&
            Document::visible())
        {
            Document::hide(mPendingClose);
        }
    });

    auto listenControl = [this](Control control) {
        const auto index = static_cast<std::size_t>(control);
        auto* element = index < mControlElements.size() ? mControlElements[index].root : nullptr;
        if (element == nullptr) {
            return;
        }
        listen(element, aurora::rmlui::TouchStartEvent, [this, control](Rml::Event& event) {
            if (!visible() || mWasSuppressed) {
                return;
            }
            if (start_control_touch(touch_id(event), control)) {
                event.StopPropagation();
            }
        });
        listen(element, aurora::rmlui::TouchEndEvent, [this](Rml::Event& event) {
            if (release_control_touch(touch_id(event), false)) {
                event.StopPropagation();
            }
        });
        listen(element, aurora::rmlui::TouchCancelEvent, [this](Rml::Event& event) {
            if (release_control_touch(touch_id(event), true)) {
                event.StopPropagation();
            }
        });
    };
    for (std::size_t i = 0; i < kControls.size(); ++i) {
        listenControl(static_cast<Control>(i));
    }

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
    if (sTouchControls == this) {
        sTouchControls = nullptr;
    }
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

void TouchControls::set_control_pressed(Control control, bool pressed) {
    set_control_visual(control, pressed);
    sync_control_button_mask();

    switch (control) {
    case Control::L:
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
            if (!player_attention_locked() && mLastLTapTime != clock::time_point{} &&
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
        if (!pressed && !player_attention_locked()) {
            mLLatched = false;
        }
        break;
    case Control::R:
        mRTriggerHeld = pressed;
        break;
    default:
        break;
    }
}

bool TouchControls::fire_control_action(Control control, ControlAction action) noexcept {
    const auto* info = control_info(control);
    if (info == nullptr) {
        return false;
    }

    const auto actionBind = action == ControlAction::Tap ? info->tapAction : info->holdAction;
    if (!actionBind) {
        return false;
    }

    const auto actionIndex = static_cast<std::size_t>(*actionBind);
    if (actionIndex >= mQueuedActions.size()) {
        return false;
    }

    mQueuedActions.set(actionIndex);
    return true;
}

bool TouchControls::start_control_touch(SDL_FingerID id, Control control) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index >= mControlTouches.size()) {
        return false;
    }

    auto& touch = mControlTouches[index];
    if (touch.active) {
        return false;
    }

    touch = {
        .id = id,
        .startTime = clock::now(),
        .active = true,
        .longPressFired = false,
    };
    set_control_pressed(control, true);
    return true;
}

void TouchControls::release_control(Control control) noexcept {
    const auto index = static_cast<std::size_t>(control);
    if (index < mControlTouches.size()) {
        mControlTouches[index] = {};
    }

    sync_control_button_mask();
    switch (control) {
    case Control::L:
        mLPressed = false;
        mLLatched = false;
        mManualLLatched = false;
        mLReleasePending = false;
        mLPressStartTime = {};
        mLastLTapTime = {};
        break;
    case Control::R:
        mRTriggerHeld = false;
        break;
    default:
        break;
    }
    set_control_visual(control, false);
}

void TouchControls::sync_control_button_mask() noexcept {
    u16 buttonMask = 0;
    for (std::size_t i = 0; i < mControlTouches.size() && i < kControls.size(); ++i) {
        if (mControlTouches[i].active) {
            buttonMask |= kControls[i].padButton;
        }
    }
    mButtonMask = buttonMask;
}

void TouchControls::set_control_visual(Control control, bool pressed) noexcept {
    const auto index = static_cast<std::size_t>(control);
    auto* element = index < mControlElements.size() ? mControlElements[index].root : nullptr;
    if (element != nullptr) {
        element->SetClass("pressed", pressed);
    }
}

void TouchControls::sync_l_lock_state() noexcept {
    if (player_attention_locked()) {
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
    mControlTouches = {};
    mQueuedActions.reset();
    mButtonMask = 0;
    mLPressed = false;
    mLLatched = false;
    mManualLLatched = false;
    mLReleasePending = false;
    mLPressStartTime = {};
    mLastLTapTime = {};
    mRTriggerHeld = false;
    mWantsVirtualPad = false;
    PADClearVirtualStatus(kPort);
    touch_camera::clear();
    for (const auto& control : kControls) {
        if (control.tapAction) {
            setVirtualActionBind(*control.tapAction, kPort, false, false);
        }
        if (control.holdAction) {
            setVirtualActionBind(*control.holdAction, kPort, false, false);
        }
    }
    if (mControlStick != nullptr) {
        mControlStick->SetClass("active", false);
    }
    for (auto& elements : mControlElements) {
        if (elements.root != nullptr) {
            elements.root->SetClass("pressed", false);
        }
    }
    sync_visual_state();
}

void TouchControls::sync_touch_state() noexcept {
    if (mWasSuppressed || !getSettings().game.enableTouchControls) {
        clear_virtual_input();
        return;
    }

    sync_l_lock_state();
    const bool aimActive = dCamera_c::isAimActive();
    if (aimActive && mMoveTouch.active) {
        if (!mCameraTouch.active) {
            mCameraTouch = mMoveTouch;
            mCameraTouch.start = mMoveTouch.current;
        }
        mMoveTouch = {};
    }

    const float stickRadius = kStickRadiusDp * dp_scale();
    if (mControlStick != nullptr) {
        if (mMoveTouch.active && stickRadius > 0.f) {
            const auto delta =
                clamped_stick_delta(mMoveTouch.start, mMoveTouch.current, stickRadius);
            const float knobRadius = kStickKnobRadiusDp * dp_scale();
            mControlStick->SetClass("active", true);
            mControlStick->SetProperty(Rml::PropertyId::Left,
                Rml::Property(mMoveTouch.start.x - stickRadius, Rml::Unit::PX));
            mControlStick->SetProperty(Rml::PropertyId::Top,
                Rml::Property(mMoveTouch.start.y - stickRadius, Rml::Unit::PX));
            mControlKnob->SetProperty(Rml::PropertyId::Left,
                Rml::Property(stickRadius + delta.x - knobRadius, Rml::Unit::PX));
            mControlKnob->SetProperty(Rml::PropertyId::Top,
                Rml::Property(stickRadius + delta.y - knobRadius, Rml::Unit::PX));
        } else {
            mControlStick->SetClass("active", false);
        }
    }

    sync_visual_state();
}

void TouchControls::sync_virtual_input() noexcept {
    sync_touch_state();
    if (mWasSuppressed || !getSettings().game.enableTouchControls) {
        return;
    }

    PADStatus status{};
    status.err = PAD_ERR_NONE;
    status.button = mButtonMask;

    if (mLPressed || mLLatched || mManualLLatched) {
        status.button |= PAD_TRIGGER_L;
        status.triggerLeft = kTriggerAnalog;
    }
    if (mRTriggerHeld) {
        status.button |= PAD_TRIGGER_R;
        status.triggerRight = kTriggerAnalog;
    }

    const float stickRadius = kStickRadiusDp * dp_scale();
    if (mMoveTouch.active && stickRadius > 0.f) {
        const auto delta = clamped_stick_delta(mMoveTouch.start, mMoveTouch.current, stickRadius);
        const float stickX = stick_value(delta.x / stickRadius);
        const float stickY = stick_value(-delta.y / stickRadius);
        switch (stick_output_mode()) {
        case StickOutput::CStick:
            status.substickX = stickX;
            status.substickY = stickY;
            break;
        case StickOutput::MainStick:
        default:
            status.stickX = stickX;
            status.stickY = stickY;
            break;
        }
    }

    mWantsVirtualPad = status.button != 0 || status.stickX != 0 || status.stickY != 0 ||
                       status.substickX != 0 || status.substickY != 0 || status.triggerLeft != 0 ||
                       status.triggerRight != 0;
    if (mWantsVirtualPad) {
        PADSetVirtualStatus(kPort, &status);
    } else {
        PADClearVirtualStatus(kPort);
    }

    for (const auto& control : kControls) {
        if (control.tapAction) {
            const bool queued = mQueuedActions.test(static_cast<std::size_t>(*control.tapAction));
            setVirtualActionBind(
                *control.tapAction, kPort, queued, queued && visible() && !mWasSuppressed);
        }
        if (control.holdAction) {
            const bool queued = mQueuedActions.test(static_cast<std::size_t>(*control.holdAction));
            setVirtualActionBind(
                *control.holdAction, kPort, queued, queued && visible() && !mWasSuppressed);
        }
    }
    mQueuedActions.reset();
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
    const bool hideGameplayControls = game_controls_suppressed();
    const bool showFixedDPad = fixed_dpad_visible();
    const auto& lTrigger = mControlElements[static_cast<std::size_t>(Control::L)];
    const auto& rTrigger = mControlElements[static_cast<std::size_t>(Control::R)];

    if (lTrigger.root != nullptr) {
        lTrigger.root->SetPseudoClass("hidden", hideGameplayControls);
        lTrigger.root->SetClass(
            "active", !hideGameplayControls &&
                          (mLPressed || mLLatched || mManualLLatched || player_attention_locked()));
    }
    if (rTrigger.root != nullptr) {
        rTrigger.root->SetPseudoClass("hidden", hideGameplayControls);
    }

    if (hideGameplayControls) {
        release_control(Control::L);
        release_control(Control::R);
    }

    constexpr std::array dpadControls{
        Control::DPAD_UP,
        Control::DPAD_DOWN,
        Control::DPAD_LEFT,
        Control::DPAD_RIGHT,
    };
    if (mDPadCluster != nullptr) {
        mDPadCluster->SetPseudoClass("hidden", !showFixedDPad);
    }
    for (const auto control : dpadControls) {
        const auto index = static_cast<std::size_t>(control);
        auto* element = index < mControlElements.size() ? mControlElements[index].root : nullptr;
        if (element != nullptr) {
            element->SetPseudoClass("hidden", !showFixedDPad);
        }
        if (!showFixedDPad) {
            release_control(control);
        }
    }
}

void TouchControls::sync_action_bar_state() noexcept {
    auto* event = dComIfGp_getEvent();
    const bool skipVisible =
        event != nullptr && event->mEventStatus == 1 && event->mSkipFunc != nullptr &&
        !event->chkFlag2(2);
    const bool hidden = !skipVisible &&
                        (!controls_available(false) || dComIfGp_event_runCheck() ||
                            (dComIfGp_getMsgObjectClass() != nullptr &&
                                dMsgObject_isTalkNowCheck()));
    const auto& skip = mControlElements[static_cast<std::size_t>(Control::SKIP)];
    if (mActionBar != nullptr) {
        mActionBar->SetPseudoClass("hidden", hidden || skipVisible);
    }
    if (skip.root != nullptr) {
        skip.root->SetPseudoClass("hidden", !skipVisible);
    }
    if (skipVisible) {
        release_control(Control::FIRST_PERSON);
        release_control(Control::ITEMS);
        release_control(Control::COLLECTIONS);
        release_control(Control::MAP);
        return;
    }

    release_control(Control::SKIP);
    if (!hidden) {
        return;
    }

    release_control(Control::FIRST_PERSON);
    release_control(Control::ITEMS);
    release_control(Control::COLLECTIONS);
    release_control(Control::MAP);
}

void TouchControls::sync_control_displays() noexcept {
    const auto bState = b_button_state();
    const auto xState = xy_button_state(Control::X);
    const auto yState = xy_button_state(Control::Y);
    const auto zState = z_button_state();

    const auto& b = mControlElements[static_cast<std::size_t>(Control::B)];
    const auto& x = mControlElements[static_cast<std::size_t>(Control::X)];
    const auto& y = mControlElements[static_cast<std::size_t>(Control::Y)];
    const auto& z = mControlElements[static_cast<std::size_t>(Control::Z)];

    if (z.root != nullptr) {
        z.root->SetPseudoClass("hidden", !zState.visible);
        z.root->SetClass("has-icon", zState.showIcon);
    }
    if (!zState.visible) {
        release_control(Control::Z);
    }
    if (z.icon != nullptr) {
        z.icon->SetClass("visible", zState.showIcon);
    }

    const bool zSourceChanged = zState.iconSource != mZTriggerIconSource;
    const bool zRevisionChanged = zState.iconRevision != mZTriggerIconRevision;
    if (zSourceChanged || zRevisionChanged) {
        const std::string previousSource = mZTriggerIconSource;
        mZTriggerIconSource = zState.iconSource;
        mZTriggerIconRevision = zState.iconRevision;
        if (z.icon == nullptr) {
            release_rml_texture(previousSource);
        } else if (zState.iconSource.empty()) {
            z.icon->RemoveAttribute("src");
        } else {
            release_rml_texture(zState.iconSource);
            if (!zSourceChanged) {
                z.icon->RemoveAttribute("src");
            }
            z.icon->SetAttribute("src", zState.iconSource);
        }
        if (zSourceChanged) {
            release_rml_texture(previousSource);
        }
    }

    const auto syncIcon = [this](Rml::Element* button, Rml::Element* icon, std::string& lastSource,
                              Control control, const FaceButtonState& state) {
        if (button != nullptr) {
            button->SetPseudoClass("hidden", !state.visible);
            button->SetClass("has-item", state.showIcon);
        }
        if (!state.visible) {
            release_control(control);
        }

        if (icon != nullptr) {
            icon->SetClass("visible", state.showIcon);
        }

        if (state.iconSource == lastSource) {
            return;
        }

        const std::string previousSource = lastSource;
        lastSource = state.iconSource;
        if (icon == nullptr) {
            release_rml_texture(previousSource);
            return;
        }
        if (state.iconSource.empty()) {
            icon->RemoveAttribute("src");
        } else {
            icon->SetAttribute("src", state.iconSource);
        }
        release_rml_texture(previousSource);
    };

    syncIcon(b.root, b.icon, mButtonBIconSource, Control::B, bState);
    syncIcon(x.root, x.icon, mButtonXIconSource, Control::X, xState);
    syncIcon(y.root, y.icon, mButtonYIconSource, Control::Y, yState);

    const auto syncCount = [](Rml::Element* countElement, std::string& lastLabel, Control control,
                               const FaceButtonState& state) {
        const std::string label = state.showIcon ? item_count_label_for_button(control) : "";
        if (label == lastLabel) {
            return;
        }

        lastLabel = label;
        if (countElement == nullptr) {
            return;
        }
        countElement->SetClass("visible", !label.empty());
        countElement->SetInnerRML(label);
    };

    syncCount(x.count, mButtonXCountLabel, Control::X, xState);
    syncCount(y.count, mButtonYCountLabel, Control::Y, yState);

    const auto syncOil = [](Rml::Element* meter, Rml::Element* fill, Control control,
                             const FaceButtonState& state) {
        const auto oilFill = state.showIcon ? item_oil_fill_for_button(control) : std::nullopt;
        if (meter != nullptr) {
            meter->SetClass("visible", oilFill.has_value());
        }
        if (fill != nullptr) {
            const float percent = oilFill ? *oilFill * 100.f : 0.f;
            fill->SetProperty(Rml::PropertyId::Width, Rml::Property(percent, Rml::Unit::PERCENT));
        }
    };

    syncOil(b.oil, b.oilFill, Control::B, bState);
    syncOil(x.oil, x.oilFill, Control::X, xState);
    syncOil(y.oil, y.oilFill, Control::Y, yState);

    clear_equip_targets();
    if (!visible() || mWasSuppressed || !getSettings().game.enableTouchControls) {
        return;
    }

    if (xState.showIcon) {
        sync_equip_target(0, x.root, kFaceIconTargetRatio, kFaceIconTargetRatio);
    }
    if (yState.showIcon) {
        sync_equip_target(1, y.root, kFaceIconTargetRatio, kFaceIconTargetRatio);
    }
    if (zState.showIcon) {
        sync_equip_target(2, z.icon, 1.f, 1.f, true);
    }
    if (bState.showIcon) {
        sync_equip_target(3, b.root, kFaceIconTargetRatio, kFaceIconTargetRatio);
    }
}

void TouchControls::update() {
    sync_visibility();
    sync_control_long_presses();
    sync_safe_area();
    sync_visual_state();
    sync_action_bar_state();
    sync_control_displays();
    sync_touch_state();
}

bool TouchControls::release_control_touch(SDL_FingerID id, bool cancelled) noexcept {
    for (std::size_t i = 0; i < mControlTouches.size(); ++i) {
        auto& touch = mControlTouches[i];
        if (!touch.active || touch.id != id) {
            continue;
        }

        const auto control = static_cast<Control>(i);
        const bool shouldFireTapAction = !cancelled && !touch.longPressFired;
        touch = {};
        set_control_pressed(control, false);
        if (shouldFireTapAction) {
            fire_control_action(control, ControlAction::Tap);
        }
        return true;
    }

    return false;
}

void TouchControls::sync_control_long_presses() noexcept {
    const auto now = clock::now();
    for (std::size_t i = 0; i < mControlTouches.size(); ++i) {
        auto& touch = mControlTouches[i];
        if (!touch.active || touch.longPressFired || now - touch.startTime < kHoldActionDuration) {
            continue;
        }

        if (!fire_control_action(static_cast<Control>(i), ControlAction::Hold)) {
            continue;
        }

        touch.longPressFired = true;
    }
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
    if (dCamera_c::isAimActive()) {
        if (!mCameraTouch.active) {
            mCameraTouch = {
                .id = id,
                .start = position,
                .current = position,
                .active = true,
            };
        }
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
        const auto delta = position - mCameraTouch.current;
        mCameraTouch.current = position;
        const float scale = dp_scale();
        touch_camera::add_delta(delta.x / scale, delta.y / scale);
    }
}

void TouchControls::handle_touch_up(Rml::Event& event) noexcept {
    if (!visible() || mWasSuppressed) {
        return;
    }

    const auto id = touch_id(event);
    if (release_control_touch(id, false)) {
        return;
    }
    if (mMoveTouch.active && mMoveTouch.id == id) {
        mMoveTouch = {};
    }
    if (mCameraTouch.active && mCameraTouch.id == id) {
        mCameraTouch = {};
    }
}

}  // namespace dusk::ui
