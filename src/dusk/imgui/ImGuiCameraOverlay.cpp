#include "f_op/f_op_camera_mng.h"
#include "SSystem/SComponent/c_xyz.h"
#include "d/d_com_inf_game.h"

#include "imgui.h"
#include "ImGuiConfig.hpp"
#include "ImGuiConsole.hpp"
#include "ImGuiMenuTools.hpp"
#include "dusk/settings.h"
#include "dusk/frame_interpolation.h"

namespace {

const char* interpolation_kind_name(dusk::frame_interp::CameraInterpolationKind kind) {
    using Kind = dusk::frame_interp::CameraInterpolationKind;
    switch (kind) {
    case Kind::Unavailable:
        return "Unavailable";
    case Kind::Authoritative:
        return "Authoritative";
    case Kind::Previous:
        return "Previous";
    case Kind::Linear:
        return "Linear fallback";
    case Kind::Orbit:
        return "Target-relative orbit";
    case Kind::SemanticOrbit:
        return "Semantic orbit";
    }
    return "Unknown";
}

const char* fallback_reason_name(
    dusk::frame_interp::CameraInterpolationFallbackReason reason) {
    using Reason = dusk::frame_interp::CameraInterpolationFallbackReason;
    switch (reason) {
    case Reason::None:
        return "none";
    case Reason::MissingSnapshots:
        return "missing snapshots";
    case Reason::IncompatibleCamera:
        return "camera transition";
    case Reason::UnsupportedAlgorithm:
        return "unsupported algorithm";
    case Reason::MissingTarget:
        return "missing target";
    case Reason::TargetChanged:
        return "target changed";
    case Reason::TargetPoseUnavailable:
        return "target pose unavailable";
    }
    return "unknown";
}

}  // namespace

namespace dusk {
    void ImGuiMenuTools::ShowCameraOverlay() {
        if (!getSettings().backend.enableAdvancedSettings ||
            !ImGuiConsole::CheckMenuViewToggle(ImGuiKey_F9, m_showCameraOverlay))
        {
            return;
        }

        auto* cam = (camera_process_class*)dCam_getCamera();

        if (!m_showCameraOverlay || cam == nullptr)
            return;

        auto* dCam = &cam->mCamera;

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (m_cameraOverlayCorner != -1) {
            SetOverlayWindowLocation(m_cameraOverlayCorner);
            windowFlags |= ImGuiWindowFlags_NoMove;
        }

        // ImGui::SetNextWindowBgAlpha(0.65f);

        if (!ImGui::Begin("Camera Debug", nullptr, windowFlags)) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Camera Transform Data");

        cXyz center = dCam->mCenter;
        cXyz eye = dCam->mEye;

        if (ImGui::InputFloat3("Camera Center", &center.x)) {
            dCam->Reset(center, eye);
        }
        if (ImGui::InputFloat3("Camera Eye", &eye.x)) {
            dCam->Reset(center, eye);
        }

        if (ImGui::InputFloat("Camera FOV", &dCam->mFovy)) {
            dCam->mFovy = std::clamp(dCam->mFovy, 0.1f, 179.9f);
        }

        const auto& interpolation = frame_interp::camera_interpolation_diagnostics();
        ImGui::SeparatorText("Presentation");
        ImGui::Text("Interpolation: %s", interpolation_kind_name(interpolation.kind));
        if (interpolation.valid) {
            ImGui::Text("Tick / alpha: %llu / %.3f",
                        static_cast<unsigned long long>(interpolation.simTickSeq),
                        interpolation.step);
            ImGui::Text("Camera frames / rebase: %.3f / %s", interpolation.cameraFrames,
                        interpolation.rebased ? "yes" : "no");
            ImGui::Text("Algorithm / mode / type / style: %d / %d / %d / %d",
                        interpolation.algorithm, interpolation.mode, interpolation.type,
                        interpolation.style);
            ImGui::Text("Radius prev / shown / curr: %.2f / %.2f / %.2f",
                        interpolation.previousRadius, interpolation.presentedRadius,
                        interpolation.currentRadius);
            ImGui::Text("Linear radius at alpha: %.2f", interpolation.linearRadius);
            ImGui::Text("Linear radius error / max: %.2f / %.2f",
                        interpolation.linearRadiusError,
                        interpolation.maxLinearRadiusError);
            ImGui::Text("Compatible rig: %s", interpolation.compatibleRig ? "yes" : "no");
            ImGui::Text("Actor anchored: %s", interpolation.actorAnchored ? "yes" : "no");
            ImGui::Text("Collision / correction / max: %s / %.2f / %.2f",
                        interpolation.collisionHit ? "hit" : "clear",
                        interpolation.collisionCorrection,
                        interpolation.maxCollisionCorrection);
            ImGui::Text("Presentation collision hits: %llu",
                        static_cast<unsigned long long>(interpolation.collisionHitCount));
            if (interpolation.fallbackReason !=
                frame_interp::CameraInterpolationFallbackReason::None)
            {
                ImGui::Text("Fallback: %s",
                            fallback_reason_name(interpolation.fallbackReason));
            }
        }
        ImGui::Text("Recorded actor poses: %zu", frame_interp::recorded_actor_pose_count());

        ImGui::SeparatorText("Options");

        bool eventRunning = (dComIfGp_event_runCheck() || dComIfGp_isPauseFlag()) && !getSettings().game.debugFlyCam;
        if (eventRunning) {
            ImGui::BeginDisabled();
        }
        config::ImGuiCheckbox("Fly Mode", getSettings().game.debugFlyCam);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (eventRunning) {
                ImGui::SetTooltip("Cannot enable while paused or during an active event.");
            } else {
                ImGui::SetTooltip("Detach camera and fly freely.\n"
                                  "WASD/Arrows/Left stick: move, Mouse/C-stick: look\n"
                                  "Ctrl/L: down, Space/R: up, Shift/Z: fast\n"
                                "Q Key/Y: roll left, R Key/X: roll right");
            }
        }
        if (eventRunning) {
            ImGui::EndDisabled();
        }

        if (!getSettings().game.debugFlyCam) {
            ImGui::BeginDisabled();
        }
        config::ImGuiCheckbox("Freeze Time", getSettings().game.debugFlyCamLockEvents);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!getSettings().game.debugFlyCam) {
                ImGui::SetTooltip("Enable Fly Mode first.");
            } else {
                ImGui::SetTooltip("Freezes the game while flying.");
            }
        }
        if (!getSettings().game.debugFlyCam) {
            ImGui::EndDisabled();
        }

        ShowCornerContextMenu(m_cameraOverlayCorner, 0);

        ImGui::End();
    }
}
