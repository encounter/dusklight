#include "dusk/frame_interpolation.h"

#include "d/d_bg_s_lin_chk.h"
#include "d/d_com_inf_game.h"
#include "dusk/game_clock.h"
#include "f_op/f_op_actor_mng.h"
#include "f_op/f_op_camera_mng.h"
#include "m_Do/m_Do_graphic.h"
#include "mtx.h"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct Recording {
    absl::flat_hash_map<uintptr_t, Mtx> matrix_values;
};

bool g_recording = false;
bool s_replacementsActive = false;
bool g_sync_presentation = false;

float g_step = 0.0f;
uint64_t g_sim_tick_seq = 0;
uint64_t s_observedPresentationEpoch = 0;

Recording g_current_recording;
Recording g_previous_recording;

absl::flat_hash_map<uintptr_t, Mtx> g_replacements;

struct CameraSnapshot {
    cXyz eye{};
    cXyz center{};
    cXyz up{};
    s16 bank{};
    f32 fovy{};
    f32 aspect{};
    f32 near_{};
    f32 far_{};
    int mode{};
    int type{};
    int style{};
    int algorithm{-1};
    int roomNo{};
    const fopAc_ac_c* targetActor{};
    fpc_ProcID targetActorId{fpcM_ERROR_PROCESS_ID_e};
    cXyz targetAttentionPosition{};
    const fopAc_ac_c* secondaryTargetActor{};
    fpc_ProcID secondaryTargetActorId{fpcM_ERROR_PROCESS_ID_e};
    cXyz secondaryTargetAttentionPosition{};
    u32 collisionFlags{};
    f32 gazeBackMargin{};
    bool active{};
    bool wideZoom{};
    bool valid{};
};

CameraSnapshot s_cam_prev{};
CameraSnapshot s_cam_curr{};
const camera_process_class* s_camera_owner = nullptr;
dusk::frame_interp::CameraInterpolationDiagnostics s_camera_diagnostics{};
f32 s_max_linear_radius_error = 0.0f;
f32 s_max_collision_correction = 0.0f;
uint64_t s_collision_hit_count = 0;

struct PresentationIntervalState {
    uint64_t simTickSeq{};
    f32 lastStep{};
    bool valid{};
};

PresentationIntervalState s_presentation_interval{};

struct ActorPoseSnapshot {
    cXyz position{};
    cXyz attentionPosition{};
    cXyz eyePosition{};
    csXyz shapeAngle{};
    s8 roomNo{};
};

struct ActorPoseRecord {
    fpc_ProcID processId{fpcM_ERROR_PROCESS_ID_e};
    ActorPoseSnapshot previous{};
    ActorPoseSnapshot current{};
    bool previousValid{};
    bool currentValid{};
    bool discontinuous{};
};

absl::flat_hash_map<uintptr_t, ActorPoseRecord> s_actor_poses;

view_class s_presentation_view_backup{};
int s_presentation_depth = 0;

struct InterpolationCallBackWork {
    dusk::frame_interp::InterpolationCallBack pCallBack;
    void* pUserWork;
};

std::vector<InterpolationCallBackWork> s_interpolationCallBackWork;

struct ModelInterpolationCallBackWork {
    dusk::frame_interp::InterpolationCallBack before;
    dusk::frame_interp::InterpolationCallBack after;
    void* pUserWork;
};

absl::flat_hash_map<const J3DModel*, ModelInterpolationCallBackWork>
    s_modelInterpolationCallBackWork;

void copy_camera_to_snap(CameraSnapshot* dst, camera_process_class* camera) {
    const view_class& v = camera->view;
    dst->eye = v.lookat.eye;
    dst->center = v.lookat.center;
    dst->up = v.lookat.up;
    dst->bank = v.bank;
    dst->fovy = v.fovy;
    dst->aspect = v.aspect;
    dst->near_ = v.near_;
    dst->far_ = v.far_;
    dst->mode = camera->mCamera.Mode();
    dst->type = camera->mCamera.Type();
    dst->style = camera->mCamera.mCamStyle;
    dst->algorithm = camera->mCamera.mCamParam.Algorythmn(dst->style);
    dst->roomNo = camera->mCamera.mRoomCtx.mRoomNo;
    dst->active = camera->mCamera.Active();
    dst->collisionFlags = camera->mCamera.mBumpCheckFlags;
    dst->gazeBackMargin = camera->mCamera.mCamSetup.mBGChk.GazeBackMargin() + 0.5f;
    if (dst->active && (dst->algorithm == 1 || dst->algorithm == 2) &&
        camera->mCamera.mpPlayerActor != nullptr)
    {
        dst->targetActor = camera->mCamera.mpPlayerActor;
        dst->targetActorId = fopAcM_GetID(dst->targetActor);
        dst->targetAttentionPosition = dst->targetActor->attention_info.position;
    } else {
        dst->targetActor = nullptr;
        dst->targetActorId = fpcM_ERROR_PROCESS_ID_e;
        dst->targetAttentionPosition = cXyz{};
    }
    if (dst->active && dst->algorithm == 2 && camera->mCamera.mpLockonTarget != nullptr) {
        dst->secondaryTargetActor = camera->mCamera.mpLockonTarget;
        dst->secondaryTargetActorId = fopAcM_GetID(dst->secondaryTargetActor);
        dst->secondaryTargetAttentionPosition =
            dst->secondaryTargetActor->attention_info.position;
    } else {
        dst->secondaryTargetActor = nullptr;
        dst->secondaryTargetActorId = fpcM_ERROR_PROCESS_ID_e;
        dst->secondaryTargetAttentionPosition = cXyz{};
    }
    dst->valid = true;
}

struct SphericalOffset {
    f32 radius;
    f32 yaw;
    f32 pitch;
};

constexpr f32 kMinimumOrbitRadius = 0.01f;

SphericalOffset spherical_offset(const cXyz& eye, const cXyz& center) {
    const f32 x = eye.x - center.x;
    const f32 y = eye.y - center.y;
    const f32 z = eye.z - center.z;
    const f32 horizontal = sqrtf(x * x + z * z);
    return {
        .radius = sqrtf(horizontal * horizontal + y * y),
        .yaw = atan2f(x, z),
        .pitch = atan2f(y, horizontal),
    };
}

f32 lerp_angle_radians(f32 lhs, f32 rhs, f32 step) {
    return lhs + remainderf(rhs - lhs, 2.0f * static_cast<f32>(M_PI)) * step;
}

cXyz cartesian_offset(const SphericalOffset& offset) {
    const f32 horizontal = offset.radius * cosf(offset.pitch);
    return {
        horizontal * sinf(offset.yaw),
        offset.radius * sinf(offset.pitch),
        horizontal * cosf(offset.yaw),
    };
}

bool same_camera_rig(const CameraSnapshot& lhs, const CameraSnapshot& rhs) {
    return lhs.active && rhs.active && lhs.mode == rhs.mode && lhs.type == rhs.type &&
           lhs.style == rhs.style && lhs.algorithm == rhs.algorithm && lhs.roomNo == rhs.roomNo;
}

bool same_camera_target(const CameraSnapshot& lhs, const CameraSnapshot& rhs) {
    return lhs.targetActor != nullptr && lhs.targetActor == rhs.targetActor &&
           lhs.targetActorId == rhs.targetActorId;
}

bool same_secondary_camera_target(const CameraSnapshot& lhs, const CameraSnapshot& rhs) {
    return lhs.secondaryTargetActor != nullptr &&
           lhs.secondaryTargetActor == rhs.secondaryTargetActor &&
           lhs.secondaryTargetActorId == rhs.secondaryTargetActorId;
}

cXyz midpoint(const cXyz& lhs, const cXyz& rhs) {
    return (lhs + rhs) * 0.5f;
}

bool interpolate_camera_orbit(cXyz* eye, const CameraSnapshot& prev,
                              const CameraSnapshot& curr, const cXyz& center, f32 step) {
    if (!same_camera_rig(prev, curr)) {
        return false;
    }

    const SphericalOffset prevOffset = spherical_offset(prev.eye, prev.center);
    const SphericalOffset currOffset = spherical_offset(curr.eye, curr.center);
    if (prevOffset.radius < kMinimumOrbitRadius || currOffset.radius < kMinimumOrbitRadius) {
        return false;
    }

    const SphericalOffset offset{
        .radius = prevOffset.radius + (currOffset.radius - prevOffset.radius) * step,
        .yaw = lerp_angle_radians(prevOffset.yaw, currOffset.yaw, step),
        .pitch = lerp_angle_radians(prevOffset.pitch, currOffset.pitch, step),
    };
    const cXyz relativeEye = cartesian_offset(offset);
    *eye = cXyz{
        center.x + relativeEye.x,
        center.y + relativeEye.y,
        center.z + relativeEye.z,
    };
    return true;
}

f32 distance_between(const cXyz& lhs, const cXyz& rhs) {
    const f32 x = lhs.x - rhs.x;
    const f32 y = lhs.y - rhs.y;
    const f32 z = lhs.z - rhs.z;
    return sqrtf(x * x + y * y + z * z);
}

ActorPoseSnapshot actor_pose_snapshot(const fopAc_ac_c& actor) {
    return {
        .position = actor.current.pos,
        .attentionPosition = actor.attention_info.position,
        .eyePosition = actor.eyePos,
        .shapeAngle = actor.shape_angle,
        .roomNo = actor.current.roomNo,
    };
}

bool actor_pose_discontinuous(const ActorPoseSnapshot& previous,
                              const ActorPoseSnapshot& current) {
    constexpr f32 kTeleportDistance = 2000.0f;
    return previous.roomNo != current.roomNo ||
           distance_between(previous.position, current.position) > kTeleportDistance;
}

void roll_actor_poses() {
    for (auto& entry : s_actor_poses) {
        ActorPoseRecord& record = entry.second;
        if (record.currentValid) {
            record.previous = record.current;
            record.previousValid = true;
            record.discontinuous = false;
        }
    }
}

bool evaluate_semantic_orbit(cXyz* eye, cXyz* center, const CameraSnapshot& prev,
                             const CameraSnapshot& curr, f32 step,
                             dusk::frame_interp::CameraInterpolationFallbackReason* reason) {
    using FallbackReason = dusk::frame_interp::CameraInterpolationFallbackReason;

    if (!same_camera_rig(prev, curr)) {
        *reason = FallbackReason::IncompatibleCamera;
        return false;
    }
    if (prev.algorithm != 1 && prev.algorithm != 2) {
        *reason = FallbackReason::UnsupportedAlgorithm;
        return false;
    }
    if (prev.targetActor == nullptr || curr.targetActor == nullptr) {
        *reason = FallbackReason::MissingTarget;
        return false;
    }
    if (!same_camera_target(prev, curr)) {
        *reason = FallbackReason::TargetChanged;
        return false;
    }

    dusk::frame_interp::ActorPresentationPose targetPose;
    if (!dusk::frame_interp::sample_actor_pose(curr.targetActor, step, &targetPose)) {
        *reason = FallbackReason::TargetPoseUnavailable;
        return false;
    }

    cXyz previousAnchor = prev.targetAttentionPosition;
    cXyz currentAnchor = curr.targetAttentionPosition;
    cXyz presentedAnchor = targetPose.attentionPosition;
    if (prev.algorithm == 2) {
        if (prev.secondaryTargetActor == nullptr || curr.secondaryTargetActor == nullptr) {
            *reason = FallbackReason::MissingTarget;
            return false;
        }
        if (!same_secondary_camera_target(prev, curr)) {
            *reason = FallbackReason::TargetChanged;
            return false;
        }
        dusk::frame_interp::ActorPresentationPose secondaryPose;
        if (!dusk::frame_interp::sample_actor_pose(curr.secondaryTargetActor, step,
                                                   &secondaryPose))
        {
            *reason = FallbackReason::TargetPoseUnavailable;
            return false;
        }
        previousAnchor = midpoint(previousAnchor, prev.secondaryTargetAttentionPosition);
        currentAnchor = midpoint(currentAnchor, curr.secondaryTargetAttentionPosition);
        presentedAnchor = midpoint(presentedAnchor, secondaryPose.attentionPosition);
    }

    const cXyz previousOffset = prev.center - previousAnchor;
    const cXyz currentOffset = curr.center - currentAnchor;
    cXyz centerOffset;
    dusk::frame_interp::lerp(centerOffset, previousOffset, currentOffset, step);
    *center = presentedAnchor + centerOffset;
    if (!interpolate_camera_orbit(eye, prev, curr, *center, step)) {
        *reason = FallbackReason::IncompatibleCamera;
        return false;
    }

    *reason = FallbackReason::None;
    return true;
}

bool clamp_presentation_eye(cXyz* eye, const cXyz& center, const CameraSnapshot& camera,
                            f32* correction) {
    if ((camera.collisionFlags & 0xb7) == 0 || camera.gazeBackMargin < 0.0f) {
        return false;
    }

    cXyz direction = *eye - center;
    const f32 distance = direction.abs();
    if (distance <= kMinimumOrbitRadius) {
        return false;
    }
    direction *= 1.0f / distance;
    const cXyz probe = *eye + direction * camera.gazeBackMargin;

    dBgS_CamLinChk lineCheck;
    if (camera.collisionFlags & 0x8000) {
        lineCheck.ClrCam();
        lineCheck.SetObj();
    } else {
        lineCheck.ClrObj();
        lineCheck.SetCam();
    }
    lineCheck.Set(&center, &probe, nullptr);
    if (camera.collisionFlags & 4) {
        lineCheck.ClrSttsRoofOff();
    } else {
        lineCheck.SetSttsRoofOff();
    }
    if (camera.collisionFlags & 2) {
        lineCheck.ClrSttsWallOff();
    } else {
        lineCheck.SetSttsWallOff();
    }
    if (camera.collisionFlags & 1) {
        lineCheck.ClrSttsGroundOff();
    } else {
        lineCheck.SetSttsGroundOff();
    }
    if (camera.collisionFlags & 8) {
        lineCheck.OnWaterGrp();
    } else {
        lineCheck.OffWaterGrp();
    }
    if (!dComIfG_Bgsp().LineCross(&lineCheck)) {
        return false;
    }

    cM3dGPla plane;
    if (!dComIfG_Bgsp().GetTriPla(lineCheck, &plane)) {
        return false;
    }

    const cXyz unclampedEye = *eye;
    *eye = lineCheck.GetCross() + *plane.GetNP() * camera.gazeBackMargin;
    *correction = distance_between(unclampedEye, *eye);
    return true;
}

const Mtx* resolve_replacement(const Mtx* source, Mtx* scratch) {
    if (!s_replacementsActive || source == nullptr ||
        dusk::frame_interp::presentation_sync_active())
    {
        return source;
    }

    auto it = g_replacements.find(reinterpret_cast<uintptr_t>(source));
    if (it == g_replacements.end()) {
        return source;
    }

    MTXCopy(it->second, *scratch);
    return scratch;
}

bool has_recording_data(const Recording& recording) {
    return !recording.matrix_values.empty();
}

void clear_replacements() {
    g_replacements.clear();
}

void clear_interpolation_history() {
    g_recording = false;
    s_replacementsActive = false;
    g_sync_presentation = false;
    g_previous_recording = {};
    g_current_recording = {};
    clear_replacements();
    s_cam_prev = {};
    s_cam_curr = {};
    s_camera_owner = nullptr;
    s_camera_diagnostics = {};
    s_max_linear_radius_error = 0.0f;
    s_max_collision_correction = 0.0f;
    s_collision_hit_count = 0;
    s_presentation_interval = {};
    s_actor_poses.clear();
    s_interpolationCallBackWork.clear();
    s_modelInterpolationCallBackWork.clear();
    s_presentation_depth = 0;
}

}  // namespace

namespace dusk::frame_interp {

void begin_sim_tick() {
    if (!is_enabled()) {
        return;
    }

    s_interpolationCallBackWork.clear();
    s_modelInterpolationCallBackWork.clear();
    s_cam_prev = std::move(s_cam_curr);
    roll_actor_poses();
    ++g_sim_tick_seq;
}

uint64_t sim_tick_seq() {
    return g_sim_tick_seq;
}

void begin_frame(float step) {
    const game_clock::FrameTiming& timing = game_clock::g_frameTiming;
    if (s_observedPresentationEpoch != timing.presentationEpoch) {
        s_observedPresentationEpoch = timing.presentationEpoch;
        clear_interpolation_history();
    }

    g_step = std::clamp(step, 0.0f, 1.0f);
    if (!is_enabled()) {
        clear_interpolation_history();
    }
}

bool is_enabled() {
    return game_clock::g_frameTiming.interpolating;
}

bool is_sim_frame() {
    return !game_clock::g_frameTiming.separatePresentation ||
           game_clock::is_sim_tick_active();
}

void begin_record() {
    if (!is_enabled()) {
        clear_interpolation_history();
        return;
    }

    g_sync_presentation = false;
    g_previous_recording = std::move(g_current_recording);
    g_current_recording = {};
    g_recording = true;
    s_replacementsActive = false;
    clear_replacements();

    if (dComIfGp_getCamera(0) == nullptr) {
        s_cam_prev.valid = false;
        s_cam_curr.valid = false;
    }
}

void end_record() {
    g_recording = false;
}

void interpolate() {
    clear_replacements();
    s_replacementsActive = is_enabled() && !g_recording && !g_sync_presentation &&
                           has_recording_data(g_current_recording);
    if (!s_replacementsActive) {
        return;
    }
    for (auto const& old : g_previous_recording.matrix_values) {
        if (auto it = g_current_recording.matrix_values.find(old.first);
            it != g_current_recording.matrix_values.end())
        {
            lerp(g_replacements[old.first], old.second, it->second, g_step);
        }
    }
}

void request_presentation_sync() {
    if (!is_enabled()) {
        return;
    }
    g_sync_presentation = true;
}

bool presentation_sync_active() {
    if (!is_enabled()) {
        return false;
    }
    return g_sync_presentation;
}

float get_interpolation_step() {
    return presentation_sync_active() ? 1.0f : g_step;
}

bool get_ui_tick_pending() {
    return !is_enabled() || is_sim_frame();
}

void record_final_mtx(Mtx m, const void* key) {
    if (!g_recording || m == nullptr) {
        return;
    }

    auto& it = g_current_recording.matrix_values[reinterpret_cast<uintptr_t>(key)];
    MTXCopy(m, it);
}

void record_final_mtx(Mtx m) {
    record_final_mtx(m, m);
}

bool lookup_replacement(const void* key, Mtx out) {
    if (presentation_sync_active() || !s_replacementsActive || key == nullptr) {
        return false;
    }

    auto it = g_replacements.find(reinterpret_cast<uintptr_t>(key));
    if (it == g_replacements.end()) {
        return false;
    }

    MTXCopy(it->second, out);
    return true;
}

bool lookup_concat_replacement(const void* lhs, const void* rhs, Mtx out) {
    if (presentation_sync_active() || !s_replacementsActive || lhs == nullptr || rhs == nullptr) {
        return false;
    }

    Mtx lhs_scratch;
    Mtx rhs_scratch;
    const Mtx* resolved_lhs = resolve_replacement(reinterpret_cast<const Mtx*>(lhs), &lhs_scratch);
    const Mtx* resolved_rhs = resolve_replacement(reinterpret_cast<const Mtx*>(rhs), &rhs_scratch);
    if (resolved_lhs == reinterpret_cast<const Mtx*>(lhs) && resolved_rhs == reinterpret_cast<const Mtx*>(rhs)) {
        return false;
    }

    MTXConcat(*resolved_lhs, *resolved_rhs, out);
    return true;
}

void record_camera(::camera_process_class* cam, int camera_id) {
    if (!is_enabled() || camera_id != 0 || cam == nullptr) {
        return;
    }
    if (s_camera_owner != cam) {
        reset_camera();
        s_camera_owner = cam;
    }
    copy_camera_to_snap(&s_cam_curr, cam);
#if WIDESCREEN_SUPPORT
    s_cam_curr.wideZoom = mDoGph_gInf_c::isWideZoom();
#endif
}

void reset_camera() {
    s_cam_prev = {};
    s_cam_curr = {};
    s_camera_owner = nullptr;
    s_camera_diagnostics = {};
    s_max_linear_radius_error = 0.0f;
    s_max_collision_correction = 0.0f;
    s_collision_hit_count = 0;
    s_presentation_interval = {};
}

const CameraInterpolationDiagnostics& camera_interpolation_diagnostics() {
    return s_camera_diagnostics;
}

void capture_actor_pose(::fopAc_ac_c* actor) {
    if (!is_enabled() || !is_sim_frame() || actor == nullptr) {
        return;
    }

    const uintptr_t key = reinterpret_cast<uintptr_t>(actor);
    const fpc_ProcID processId = fopAcM_GetID(actor);
    auto& record = s_actor_poses[key];
    const ActorPoseSnapshot pose = actor_pose_snapshot(*actor);
    if (record.processId != processId || !record.currentValid) {
        record = {
            .processId = processId,
            .previous = pose,
            .current = pose,
            .previousValid = true,
            .currentValid = true,
            .discontinuous = false,
        };
        return;
    }

    record.current = pose;
    record.currentValid = true;
    if (!record.previousValid || actor_pose_discontinuous(record.previous, record.current)) {
        record.previous = record.current;
        record.previousValid = true;
        record.discontinuous = true;
    }
}

void erase_actor_pose(::fopAc_ac_c* actor) {
    if (actor != nullptr) {
        s_actor_poses.erase(reinterpret_cast<uintptr_t>(actor));
    }
}

bool sample_actor_pose(const ::fopAc_ac_c* actor, float step, ActorPresentationPose* pose) {
    if (actor == nullptr || pose == nullptr) {
        return false;
    }

    const auto it = s_actor_poses.find(reinterpret_cast<uintptr_t>(actor));
    if (it == s_actor_poses.end() || it->second.processId != fopAcM_GetID(actor)) {
        return false;
    }

    const ActorPoseRecord& record = it->second;
    if (!record.previousValid || !record.currentValid) {
        return false;
    }
    if (record.discontinuous) {
        return false;
    }

    step = std::clamp(step, 0.0f, 1.0f);
    lerp(pose->position, record.previous.position, record.current.position, step);
    lerp(pose->attentionPosition, record.previous.attentionPosition,
         record.current.attentionPosition, step);
    lerp(pose->eyePosition, record.previous.eyePosition, record.current.eyePosition, step);
    lerp(pose->shapeAngle, record.previous.shapeAngle, record.current.shapeAngle, step);
    return true;
}

size_t recorded_actor_pose_count() {
    return s_actor_poses.size();
}

void interp_view(::view_class* view) {
    if (!is_enabled()) {
        s_camera_diagnostics = {};
        return;
    }

    if (!s_cam_prev.valid || !s_cam_curr.valid) {
        s_camera_diagnostics = {};
        return;
    }

    const f32 step = get_interpolation_step();
    const bool is_cam_curr_authoritative = is_sim_frame() && step <= 0.0f;
    bool rebased = false;
    f32 cameraFrames = 0.0f;
    if (!is_sim_frame()) {
        if (!s_presentation_interval.valid ||
            s_presentation_interval.simTickSeq != g_sim_tick_seq ||
            step < s_presentation_interval.lastStep)
        {
            s_presentation_interval = {
                .simTickSeq = g_sim_tick_seq,
                .lastStep = 0.0f,
                .valid = true,
            };
            rebased = true;
        }
        cameraFrames = step - s_presentation_interval.lastStep;
        s_presentation_interval.lastStep = step;
    }

    const f32 previousRadius = distance_between(s_cam_prev.eye, s_cam_prev.center);
    const f32 currentRadius = distance_between(s_cam_curr.eye, s_cam_curr.center);
    cXyz linearCenter;
    cXyz linearEye;
    lerp(linearCenter, s_cam_prev.center, s_cam_curr.center, step);
    lerp(linearEye, s_cam_prev.eye, s_cam_curr.eye, step);
    const f32 linearRadius = distance_between(linearEye, linearCenter);
    const f32 orbitRadius = previousRadius + (currentRadius - previousRadius) * step;
    const f32 linearRadiusError = fabsf(orbitRadius - linearRadius);
    s_max_linear_radius_error = std::max(s_max_linear_radius_error, linearRadiusError);
    s_camera_diagnostics = {
        .kind = CameraInterpolationKind::Unavailable,
        .step = step,
        .previousRadius = previousRadius,
        .currentRadius = currentRadius,
        .linearRadius = linearRadius,
        .linearRadiusError = linearRadiusError,
        .maxLinearRadiusError = s_max_linear_radius_error,
        .cameraFrames = cameraFrames,
        .maxCollisionCorrection = s_max_collision_correction,
        .simTickSeq = g_sim_tick_seq,
        .collisionHitCount = s_collision_hit_count,
        .algorithm = s_cam_curr.algorithm,
        .mode = s_cam_curr.mode,
        .type = s_cam_curr.type,
        .style = s_cam_curr.style,
        .fallbackReason = CameraInterpolationFallbackReason::None,
        .compatibleRig = same_camera_rig(s_cam_prev, s_cam_curr),
        .rebased = rebased,
        .valid = true,
    };

    cXyz eye;
    cXyz center;
    cXyz up;
    if (is_cam_curr_authoritative || step >= 1.0f) {
        eye = s_cam_curr.eye;
        center = s_cam_curr.center;
        up = s_cam_curr.up;
        s_camera_diagnostics.kind = CameraInterpolationKind::Authoritative;
    } else if (step <= 0.0f) {
        eye = s_cam_prev.eye;
        center = s_cam_prev.center;
        up = s_cam_prev.up;
        s_camera_diagnostics.kind = CameraInterpolationKind::Previous;
    } else {
        CameraInterpolationFallbackReason fallbackReason = CameraInterpolationFallbackReason::None;
        if (evaluate_semantic_orbit(&eye, &center, s_cam_prev, s_cam_curr, step,
                                    &fallbackReason))
        {
            s_camera_diagnostics.kind = CameraInterpolationKind::SemanticOrbit;
            s_camera_diagnostics.actorAnchored = true;
            s_camera_diagnostics.collisionHit =
                clamp_presentation_eye(&eye, center, s_cam_curr,
                                       &s_camera_diagnostics.collisionCorrection);
            if (s_camera_diagnostics.collisionHit) {
                ++s_collision_hit_count;
                s_max_collision_correction =
                    std::max(s_max_collision_correction,
                             s_camera_diagnostics.collisionCorrection);
                s_camera_diagnostics.collisionHitCount = s_collision_hit_count;
                s_camera_diagnostics.maxCollisionCorrection =
                    s_max_collision_correction;
            }
        } else {
            s_camera_diagnostics.fallbackReason = fallbackReason;
            lerp(center, s_cam_prev.center, s_cam_curr.center, step);
        }
        if (s_camera_diagnostics.kind == CameraInterpolationKind::SemanticOrbit) {
            // The semantic path already produced both center and eye.
        } else if (interpolate_camera_orbit(&eye, s_cam_prev, s_cam_curr, center, step)) {
            s_camera_diagnostics.kind = CameraInterpolationKind::Orbit;
        } else {
            lerp(eye, s_cam_prev.eye, s_cam_curr.eye, step);
            s_camera_diagnostics.kind = CameraInterpolationKind::Linear;
        }
        lerp(up, s_cam_prev.up, s_cam_curr.up, step);
    }
    s_camera_diagnostics.presentedRadius = distance_between(eye, center);
    if (!up.normalizeRS()) {
        up = s_cam_curr.up;
        if (!up.normalizeRS()) {
            up = cXyz{0.0f, 1.0f, 0.0f};
        }
    }

    view->lookat.eye = eye;
    view->lookat.center = center;
    view->lookat.up = up;
    if (is_cam_curr_authoritative) {
        view->bank = s_cam_curr.bank;
        view->fovy = s_cam_curr.fovy;
        view->aspect = s_cam_curr.aspect;
        view->near_ = s_cam_curr.near_;
        view->far_ = s_cam_curr.far_;
    } else {
        view->bank = lerp(s_cam_prev.bank, s_cam_curr.bank, step);
        view->fovy = s_cam_prev.fovy + (s_cam_curr.fovy - s_cam_prev.fovy) * step;
        view->aspect = s_cam_prev.aspect + (s_cam_curr.aspect - s_cam_prev.aspect) * step;
        view->near_ = s_cam_prev.near_ + (s_cam_curr.near_ - s_cam_prev.near_) * step;
        view->far_ = s_cam_prev.far_ + (s_cam_curr.far_ - s_cam_prev.far_) * step;
    }

    // FRAME INTERP TODO: It might be better if I rewired the game to not clear this flag until the
    // next sim frame, but I don't care enough to right now
#if WIDESCREEN_SUPPORT
    const f32 wide_step = is_cam_curr_authoritative ? 1.0f : step;
    if (mDoGph_gInf_c::isWide() && !mDoGph_gInf_c::isWideZoom() && wide_step >= 0.5f ? s_cam_curr.wideZoom : s_cam_prev.wideZoom) {
        mDoGph_gInf_c::onWideZoom();
    }
#endif
}

static void run_interpolation_callbacks() {
    for (size_t i = 0; i < s_interpolationCallBackWork.size(); i++) {
        auto const& work = s_interpolationCallBackWork[i];
        work.pCallBack(work.pUserWork);
    }
}

void add_interpolation_callback(InterpolationCallBack pCallBack, void* pUserWork) {
    if (!is_enabled() || s_presentation_depth > 0 || !is_sim_frame()) {
        return;
    }

    s_interpolationCallBackWork.emplace_back(pCallBack, pUserWork);
}

void add_model_interpolation_callbacks(::J3DModel* model, InterpolationCallBack before,
                                       InterpolationCallBack after, void* pUserWork) {
    if (!is_enabled() || s_presentation_depth > 0 || !is_sim_frame() || model == nullptr) {
        return;
    }

    s_modelInterpolationCallBackWork[model] = {before, after, pUserWork};
}

bool has_model_interpolation_callbacks(const ::J3DModel* model) {
    return s_modelInterpolationCallBackWork.contains(model);
}

void begin_model_interpolation(::J3DModel* model) {
    auto it = s_modelInterpolationCallBackWork.find(model);
    if (it != s_modelInterpolationCallBackWork.end() && it->second.before != nullptr) {
        it->second.before(it->second.pUserWork);
    }
}

void end_model_interpolation(::J3DModel* model) {
    auto it = s_modelInterpolationCallBackWork.find(model);
    if (it != s_modelInterpolationCallBackWork.end() && it->second.after != nullptr) {
        it->second.after(it->second.pUserWork);
    }
}

void begin_presentation_camera() {
    if (!is_enabled()) {
        return;
    }
    if (s_presentation_depth > 0) {
        s_presentation_depth++;
        return;
    }
    if (!s_cam_prev.valid || !s_cam_curr.valid) {
        return;
    }

    view_class* const view = dComIfGd_getView();
    if (view == nullptr) {
        return;
    }

    std::memcpy(&s_presentation_view_backup, view, sizeof(view_class));
    interp_view(view);

    // FRAME INTERP TODO: Largely copied from d_camera's camera_draw function from this point, got any better ideas?
    C_MTXPerspective(view->projMtx, view->fovy, view->aspect, view->near_, view->far_);
    mDoMtx_lookAt(view->viewMtx, &view->lookat.eye, &view->lookat.center, &view->lookat.up, view->bank);
#if WIDESCREEN_SUPPORT
    mDoGph_gInf_c::setWideZoomProjection(view->projMtx);
#endif
    j3dSys.setViewMtx(view->viewMtx);
    cMtx_inverse(view->viewMtx, view->invViewMtx);

    bool camera_attention_status = dComIfGp_getCameraAttentionStatus(0) & 0x80;
    Z2GetAudience()->setAudioCamera(view->viewMtx, view->lookat.eye, view->lookat.center, view->fovy, view->aspect, camera_attention_status, 0, false);

    dBgS_GndChk gndchk;
    gndchk.OnWaterGrp();
    gndchk.SetPos(&view->lookat.eye);
    f32 cross = dComIfG_Bgsp().GroundCross(&gndchk);
    if (cross != -G_CM3D_F_INF) {
        if (dComIfG_Bgsp().ChkGrpInf(gndchk, 0x100)) {
            mDoAud_getCameraMapInfo(6);
        } else {
            mDoAud_getCameraMapInfo(dComIfG_Bgsp().GetMtrlSndId(gndchk));
        }
        mDoAud_setCameraGroupInfo(dComIfG_Bgsp().GetGrpSoundId(gndchk));
        Vec spDC;
        spDC.x = view->lookat.eye.x;
        spDC.y = cross;
        spDC.z = view->lookat.eye.z;
        Z2AudioMgr::getInterface()->setCameraPolygonPos(&spDC);
    } else {
        Z2AudioMgr::getInterface()->setCameraPolygonPos(nullptr);
    }

    MTXCopy(view->viewMtx, view->viewMtxNoTrans);
    view->viewMtxNoTrans[0][3] = 0.0f;
    view->viewMtxNoTrans[1][3] = 0.0f;
    view->viewMtxNoTrans[2][3] = 0.0f;
    cMtx_concatProjView(view->projMtx, view->viewMtx, view->projViewMtx);

    f32 far_;
    f32 var_f30;
    if (dComIfGp_getCameraAttentionStatus(0) & 8) {
        far_ = view->far_;
    } else {
#if DEBUG
        if (g_envHIO.mOther.mAdjustCullFar != 0) {
            var_f30 = g_envHIO.mOther.mCullFarValue;
        } else
#endif
        {
            var_f30 = dStage_stagInfo_GetCullPoint(dComIfGp_getStageStagInfo());
        }
        far_ = var_f30;
    }

    mDoLib_clipper::setup(view->fovy, view->aspect, view->near_, far_);

    // FRAME INTERP NOTE: Removed the call to offWideZoom that was here, it causes problems with presentation during cutscenes.

    s_presentation_depth = 1;

    run_interpolation_callbacks();
}

void end_presentation_camera() {
    if (s_presentation_depth == 0) {
        return;
    }
    s_presentation_depth--;
    if (s_presentation_depth > 0) {
        return;
    }

    view_class* const view = dComIfGd_getView();
    if (view != nullptr) {
        std::memcpy(view, &s_presentation_view_backup, sizeof(view_class));
    }
}
}  // namespace dusk::frame_interp
