#include "dusk/presentation_skeleton.h"

#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.h"
#include "dusk/frame_interpolation.h"
#include "dusk/matrix_interpolation.h"
#include "m_Do/m_Do_ext.h"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

using MatrixValue = std::array<f32, 12>;

enum class ProviderKind {
    Standard,
    Morph,
    MorphSO,
    Morph2,
    CapturedPose,
    Fallback,
};

struct AnimationSource {
    J3DMtxCalcAnmBase* calculator{};
    J3DAnmTransform* animation{};
    f32 frame{};
    s16 frameMax{};
    u8 attribute{};
};

struct FrameState {
    f32 frame{};
    f32 rate{};
    s16 start{};
    s16 end{};
    s16 loop{};
    u8 attribute{};
};

struct SkeletonSnapshot {
    J3DModelData* modelData{};
    void* owner{};
    ProviderKind provider{ProviderKind::Standard};
    std::vector<J3DMtxCalc*> jointCalculators;
    std::vector<AnimationSource> animations;
    FrameState frame{};
    J3DAnmTransform* primaryAnimation{};
    J3DAnmTransform* secondaryAnimation{};
    f32 currentMorph{};
    f32 previousMorph{};
    f32 morphStep{};
    f32 animationBlend{};
    dusk::matrix_interp::MatrixSample baseMatrix;
    std::vector<J3DTransformInfo> transformHistory;
    std::vector<Quaternion> quaternionHistory;
    std::vector<s16> jointParents;
    std::vector<u16> jointOrder;
    std::vector<dusk::matrix_interp::MatrixSample> jointLocalMatrices;
    J3DModel* anchorModel{};
    s16 anchorJoint{-1};
};

struct ModelRestore {
    std::vector<MatrixValue> animationMatrices;
    std::vector<MatrixValue> envelopeMatrices;
    std::vector<u8> scaleFlags;
    std::vector<u8> envelopeScaleFlags;
    bool active{};
};

struct SkeletonRecord {
    ProviderKind provider{ProviderKind::Standard};
    J3DModelData* modelData{};
    void* owner{};
    SkeletonSnapshot previous;
    SkeletonSnapshot current;
    ModelRestore restore;
    uint64_t lastSeenSequence{};
    uint64_t scheduledSequence{};
    bool previousValid{};
    bool currentValid{};
};

struct ModelAttachment {
    J3DModel* parentModel{};
    u16 parentJoint{};
};

absl::flat_hash_map<J3DModel*, SkeletonRecord> s_records;
absl::flat_hash_map<const J3DModel*, bool> s_safeModelCallbacks;
absl::flat_hash_map<const mDoExt_morf_c*, bool> s_safeMorphCallbacks;
absl::flat_hash_map<J3DModel*, ModelAttachment> s_modelAttachments;
size_t s_presentedModelCount = 0;

bool keyed_animation_supported(const J3DAnmTransform* animation) {
    if (animation == nullptr) {
        return false;
    }
    const s32 kind = animation->getKind();
    return kind == 8 || kind == 16;
}

bool callbacks_allowed(const J3DModel* model) {
    auto it = s_safeModelCallbacks.find(model);
    return it != s_safeModelCallbacks.end() && it->second;
}

bool callbacks_allowed(const mDoExt_morf_c* morph) {
    auto it = s_safeMorphCallbacks.find(morph);
    return it != s_safeMorphCallbacks.end() && it->second;
}

bool has_joint_callbacks(J3DModel* model) {
    J3DModelData* modelData = model->getModelData();
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        if (modelData->getJointNodePointer(i)->getCallBack() != nullptr) {
            return true;
        }
    }
    return false;
}

bool model_can_recalculate(J3DModel* model) {
    if (model == nullptr || model->getModelData() == nullptr || model->getMtxBuffer() == nullptr) {
        return false;
    }
    if (model->getSkinDeform() != nullptr || model->checkFlag(J3DMdlFlag_SkinPosCpu) ||
        model->checkFlag(J3DMdlFlag_SkinNrmCpu))
    {
        return false;
    }
    if ((has_joint_callbacks(model) || model->mCalcCallBack != nullptr) &&
        !callbacks_allowed(model))
    {
        return false;
    }
    return true;
}

void copy_matrix(MatrixValue* dst, const Mtx src) {
    std::memcpy(dst->data(), src, sizeof(Mtx));
}

void copy_matrix(Mtx dst, const MatrixValue& src) {
    std::memcpy(dst, src.data(), sizeof(Mtx));
}

void save_model_matrices(J3DModel* model, ModelRestore* restore) {
    J3DModelData* modelData = model->getModelData();
    J3DMtxBuffer* buffer = model->getMtxBuffer();
    const u16 jointCount = modelData->getJointNum();
    const u16 envelopeCount = modelData->getWEvlpMtxNum();

    restore->animationMatrices.resize(jointCount);
    restore->scaleFlags.resize(jointCount);
    for (u16 i = 0; i < jointCount; ++i) {
        copy_matrix(&restore->animationMatrices[i], model->getAnmMtx(i));
        restore->scaleFlags[i] = buffer->getScaleFlag(i);
    }

    restore->envelopeMatrices.resize(envelopeCount);
    restore->envelopeScaleFlags.resize(envelopeCount);
    for (u16 i = 0; i < envelopeCount; ++i) {
        copy_matrix(&restore->envelopeMatrices[i], model->getWeightAnmMtx(i));
        restore->envelopeScaleFlags[i] = buffer->getEnvScaleFlag(i);
    }
    restore->active = true;
}

void restore_model_matrices(J3DModel* model, ModelRestore* restore) {
    if (!restore->active || model == nullptr || model->getMtxBuffer() == nullptr) {
        return;
    }

    J3DMtxBuffer* buffer = model->getMtxBuffer();
    for (size_t i = 0; i < restore->animationMatrices.size(); ++i) {
        copy_matrix(model->getAnmMtx(static_cast<int>(i)), restore->animationMatrices[i]);
        buffer->getScaleFlagArray()[i] = restore->scaleFlags[i];
    }
    for (size_t i = 0; i < restore->envelopeMatrices.size(); ++i) {
        copy_matrix(model->getWeightAnmMtx(static_cast<int>(i)), restore->envelopeMatrices[i]);
        buffer->getEnvScaleFlagArray()[i] = restore->envelopeScaleFlags[i];
    }
    restore->active = false;
}

void publish_model_matrices(J3DModel* model) {
    J3DModelData* modelData = model->getModelData();
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        dusk::frame_interp::override_presentation_mtx(model->getAnmMtx(i), model->getAnmMtx(i));
    }
    for (u16 i = 0; i < modelData->getWEvlpMtxNum(); ++i) {
        dusk::frame_interp::override_presentation_mtx(
            model->getWeightAnmMtx(i), model->getWeightAnmMtx(i));
    }
}

bool find_presentation_anchor(J3DModel* model, J3DModel** anchorModel, s16* anchorJoint) {
    auto attachment = s_modelAttachments.find(model);
    if (attachment != s_modelAttachments.end()) {
        J3DModel* candidate = attachment->second.parentModel;
        auto parentRecord = s_records.find(candidate);
        if (candidate != nullptr && parentRecord != s_records.end() &&
            parentRecord->second.currentValid && candidate->getModelData() != nullptr &&
            attachment->second.parentJoint < candidate->getModelData()->getJointNum())
        {
            *anchorModel = candidate;
            *anchorJoint = static_cast<s16>(attachment->second.parentJoint);
            return true;
        }
    }

    const MtxP baseMatrix = model->getBaseTRMtx();
    for (auto& [candidate, record] : s_records) {
        if (candidate == model || !record.currentValid || record.provider == ProviderKind::Fallback ||
            candidate->getModelData() == nullptr)
        {
            continue;
        }

        const u16 jointCount = candidate->getModelData()->getJointNum();
        for (u16 jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
            if (std::memcmp(baseMatrix, candidate->getAnmMtx(jointIndex), sizeof(Mtx)) == 0) {
                *anchorModel = candidate;
                *anchorJoint = static_cast<s16>(jointIndex);
                return true;
            }
        }
    }
    return false;
}

bool capture_joint_hierarchy(J3DModel* model, SkeletonSnapshot* snapshot) {
    J3DModelData* modelData = model->getModelData();
    const u16 jointCount = modelData->getJointNum();
    find_presentation_anchor(model, &snapshot->anchorModel, &snapshot->anchorJoint);
    snapshot->jointParents.assign(jointCount, -2);
    snapshot->jointOrder.clear();
    snapshot->jointOrder.reserve(jointCount);

    auto visit = [&](auto&& self, J3DJoint* joint, s16 parent) -> bool {
        for (J3DJoint* current = joint; current != nullptr; current = current->getYounger()) {
            const u16 jointIndex = current->getJntNo();
            if (jointIndex >= jointCount || snapshot->jointParents[jointIndex] != -2) {
                return false;
            }
            snapshot->jointParents[jointIndex] = parent;
            snapshot->jointOrder.push_back(jointIndex);
            if (current->getChild() != nullptr &&
                !self(self, current->getChild(), static_cast<s16>(jointIndex)))
            {
                return false;
            }
        }
        return true;
    };

    if (!visit(visit, modelData->getJointTree().getRootNode(), -1) ||
        snapshot->jointOrder.size() != jointCount)
    {
        return false;
    }

    snapshot->jointLocalMatrices.resize(jointCount);
    for (u16 jointIndex : snapshot->jointOrder) {
        Mtx localMatrix;
        const s16 parent = snapshot->jointParents[jointIndex];
        if (parent < 0) {
            if (snapshot->anchorModel != nullptr) {
                Mtx inverseAnchor;
                if (MTXInverse(
                        snapshot->anchorModel->getAnmMtx(snapshot->anchorJoint), inverseAnchor) == 0)
                {
                    return false;
                }
                MTXConcat(inverseAnchor, model->getAnmMtx(jointIndex), localMatrix);
            } else {
                MTXCopy(model->getAnmMtx(jointIndex), localMatrix);
            }
        } else {
            Mtx inverseParent;
            if (MTXInverse(model->getAnmMtx(parent), inverseParent) == 0) {
                return false;
            }
            MTXConcat(inverseParent, model->getAnmMtx(jointIndex), localMatrix);
        }
        dusk::matrix_interp::record(&snapshot->jointLocalMatrices[jointIndex], localMatrix);
        dusk::matrix_interp::finalize(&snapshot->jointLocalMatrices[jointIndex]);
    }
    return true;
}

bool captured_pose_snapshots_compatible(
    const SkeletonSnapshot& previous, const SkeletonSnapshot& current) {
    return previous.provider == ProviderKind::CapturedPose &&
           current.provider == ProviderKind::CapturedPose &&
           previous.modelData == current.modelData && previous.owner == current.owner &&
           previous.anchorModel == current.anchorModel &&
           previous.anchorJoint == current.anchorJoint &&
           previous.jointParents == current.jointParents &&
           previous.jointOrder == current.jointOrder &&
           previous.jointLocalMatrices.size() == current.jointLocalMatrices.size();
}

bool evaluate_captured_pose(J3DModel* model, SkeletonRecord* record, f32 step) {
    const SkeletonSnapshot& previous = record->previous;
    const SkeletonSnapshot& current = record->current;
    if (!captured_pose_snapshots_compatible(previous, current)) {
        return false;
    }

    const u16 jointCount = model->getModelData()->getJointNum();
    if (jointCount == 0 || current.jointLocalMatrices.size() != jointCount ||
        current.jointOrder.size() != jointCount)
    {
        return false;
    }

    save_model_matrices(model, &record->restore);
    J3DMtxBuffer* buffer = model->getMtxBuffer();
    for (u16 jointIndex : current.jointOrder) {
        Mtx localMatrix;
        dusk::matrix_interp::interpolate(localMatrix,
            previous.jointLocalMatrices[jointIndex], current.jointLocalMatrices[jointIndex], step);

        const s16 parent = current.jointParents[jointIndex];
        if (parent < 0) {
            if (current.anchorModel != nullptr) {
                auto anchor = s_records.find(current.anchorModel);
                if (anchor == s_records.end() || !anchor->second.currentValid ||
                    current.anchorModel->getModelData() == nullptr || current.anchorJoint < 0 ||
                    current.anchorJoint >= current.anchorModel->getModelData()->getJointNum())
                {
                    restore_model_matrices(model, &record->restore);
                    return false;
                }
                MTXConcat(current.anchorModel->getAnmMtx(current.anchorJoint), localMatrix,
                    model->getAnmMtx(jointIndex));
            } else {
                MTXCopy(localMatrix, model->getAnmMtx(jointIndex));
            }
        } else {
            MTXConcat(model->getAnmMtx(parent), localMatrix, model->getAnmMtx(jointIndex));
        }

        // Zero is the conservative J3D value: it makes the draw path account for scale instead of
        // assuming the interpolated and recomposed matrix is orthonormal.
        buffer->getScaleFlagArray()[jointIndex] = 0;
    }

    model->calcWeightEnvelopeMtx();
    publish_model_matrices(model);
    return true;
}

void finish_model_pose(J3DModel* model) {
    model->calcAnmMtx();
    model->calcWeightEnvelopeMtx();
    if (model->mCalcCallBack != nullptr && callbacks_allowed(model)) {
        model->mCalcCallBack(model, 0);
    }
    publish_model_matrices(model);
}

bool is_looping(u8 attribute) {
    return attribute == J3DFrameCtrl::EMode_LOOP || attribute == J3DFrameCtrl::EMode_LOOP_REVERSE;
}

f32 normalize_loop_frame(f32 frame, f32 loop, f32 end) {
    const f32 span = end - loop;
    if (span <= 0.0f) {
        return frame;
    }
    while (frame >= end) {
        frame -= span;
    }
    while (frame < loop) {
        frame += span;
    }
    return frame;
}

bool interpolate_frame(
    const FrameState& previous, const FrameState& current, f32 step, f32* frame) {
    f32 delta = current.frame - previous.frame;
    if (is_looping(current.attribute)) {
        const f32 span = static_cast<f32>(current.end - current.loop);
        if (span > 0.0f) {
            if (current.rate >= 0.0f && delta < -span * 0.5f) {
                delta += span;
            } else if (current.rate < 0.0f && delta > span * 0.5f) {
                delta -= span;
            }
        }
    } else {
        const f32 maximumExpectedDelta = std::max(4.0f, std::fabs(current.rate) * 2.0f);
        if (std::fabs(delta) > maximumExpectedDelta) {
            return false;
        }
    }

    *frame = previous.frame + delta * step;
    if (is_looping(current.attribute)) {
        *frame = normalize_loop_frame(*frame, current.loop, current.end);
    }
    return std::isfinite(*frame);
}

bool interpolate_animation_source(
    const AnimationSource& previous, const AnimationSource& current, f32 step, f32* frame) {
    if (previous.animation != current.animation || previous.calculator != current.calculator ||
        current.animation == nullptr)
    {
        return false;
    }

    f32 delta = current.frame - previous.frame;
    if (is_looping(current.attribute) && current.frameMax > 0) {
        const f32 span = static_cast<f32>(current.frameMax);
        constexpr f32 kMaximumTickAdvance = 4.0f;
        const bool wrappedForward =
            previous.frame >= span - kMaximumTickAdvance && current.frame <= kMaximumTickAdvance;
        const bool wrappedBackward =
            previous.frame <= kMaximumTickAdvance && current.frame >= span - kMaximumTickAdvance;
        if (delta < -span * 0.5f && wrappedForward) {
            delta += span;
        } else if (delta > span * 0.5f && wrappedBackward) {
            delta -= span;
        } else if (std::fabs(delta) > kMaximumTickAdvance) {
            return false;
        }
    } else if (std::fabs(delta) > 4.0f) {
        return false;
    }

    *frame = previous.frame + delta * step;
    if (is_looping(current.attribute) && current.frameMax > 0) {
        *frame = normalize_loop_frame(*frame, 0.0f, static_cast<f32>(current.frameMax));
    }
    return std::isfinite(*frame);
}

bool same_standard_sources(const SkeletonSnapshot& previous, const SkeletonSnapshot& current) {
    if (previous.jointCalculators != current.jointCalculators ||
        previous.animations.size() != current.animations.size())
    {
        return false;
    }
    for (size_t i = 0; i < current.animations.size(); ++i) {
        if (previous.animations[i].calculator != current.animations[i].calculator ||
            previous.animations[i].animation != current.animations[i].animation)
        {
            return false;
        }
    }
    return true;
}

void capture_joint_calculators(J3DModel* model, SkeletonSnapshot* snapshot) {
    J3DModelData* modelData = model->getModelData();
    snapshot->jointCalculators.reserve(modelData->getJointNum());
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        snapshot->jointCalculators.push_back(modelData->getJointNodePointer(i)->getMtxCalc());
    }
}

bool apply_joint_calculators(
    J3DModel* model, const SkeletonSnapshot& snapshot, std::vector<J3DMtxCalc*>* savedCalculators) {
    J3DModelData* modelData = model->getModelData();
    if (snapshot.modelData != modelData ||
        snapshot.jointCalculators.size() != modelData->getJointNum())
    {
        return false;
    }

    savedCalculators->reserve(modelData->getJointNum());
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        J3DJoint* joint = modelData->getJointNodePointer(i);
        savedCalculators->push_back(joint->getMtxCalc());
        joint->setMtxCalc(snapshot.jointCalculators[i]);
    }
    return true;
}

void restore_joint_calculators(J3DModel* model, const std::vector<J3DMtxCalc*>& savedCalculators) {
    J3DModelData* modelData = model->getModelData();
    const size_t jointCount =
        std::min(savedCalculators.size(), static_cast<size_t>(modelData->getJointNum()));
    for (size_t i = 0; i < jointCount; ++i) {
        modelData->getJointNodePointer(static_cast<u16>(i))->setMtxCalc(savedCalculators[i]);
    }
}

void capture_base_matrix(J3DModel* model, dusk::matrix_interp::MatrixSample* baseMatrix) {
    Mtx identity;
    Mtx combined;
    MTXIdentity(identity);
    J3DCalcViewBaseMtx(identity, *model->getBaseScale(), model->getBaseTRMtx(), combined);
    dusk::matrix_interp::record(baseMatrix, combined);
    dusk::matrix_interp::finalize(baseMatrix);
}

void apply_presentation_base(J3DModel* model, const SkeletonSnapshot& previous,
    const SkeletonSnapshot& current, f32 step, Vec* savedScale, Mtx savedBase) {
    *savedScale = *model->getBaseScale();
    MTXCopy(model->getBaseTRMtx(), savedBase);

    Mtx presentationBase;
    dusk::matrix_interp::interpolate(
        presentationBase, previous.baseMatrix, current.baseMatrix, step);

    const Vec unitScale{1.0f, 1.0f, 1.0f};
    model->setBaseScale(unitScale);
    model->setBaseTRMtx(presentationBase);
}

void restore_presentation_base(J3DModel* model, const Vec& savedScale, const Mtx savedBase) {
    model->setBaseScale(savedScale);
    MTXCopy(savedBase, model->getBaseTRMtx());
}

bool evaluate_standard(J3DModel* model, SkeletonRecord* record, f32 step) {
    const SkeletonSnapshot& previous = record->previous;
    const SkeletonSnapshot& current = record->current;
    if (!same_standard_sources(previous, current)) {
        return false;
    }

    std::vector<f32> savedFrames;
    std::vector<f32> presentationFrames;
    savedFrames.reserve(current.animations.size());
    presentationFrames.reserve(current.animations.size());
    for (size_t i = 0; i < current.animations.size(); ++i) {
        f32 frame;
        if (!interpolate_animation_source(
                previous.animations[i], current.animations[i], step, &frame))
        {
            return false;
        }
        savedFrames.push_back(current.animations[i].animation->getFrame());
        presentationFrames.push_back(frame);
    }

    std::vector<J3DMtxCalc*> savedCalculators;
    if (!apply_joint_calculators(model, current, &savedCalculators)) {
        return false;
    }

    save_model_matrices(model, &record->restore);
    Vec savedScale;
    Mtx savedBase;
    apply_presentation_base(model, previous, current, step, &savedScale, savedBase);

    for (size_t i = 0; i < current.animations.size(); ++i) {
        current.animations[i].animation->setFrame(presentationFrames[i]);
    }
    finish_model_pose(model);

    for (size_t i = 0; i < current.animations.size(); ++i) {
        current.animations[i].animation->setFrame(savedFrames[i]);
    }
    restore_presentation_base(model, savedScale, savedBase);
    restore_joint_calculators(model, savedCalculators);
    return true;
}

bool morph_snapshots_compatible(const SkeletonSnapshot& previous, const SkeletonSnapshot& current) {
    if (previous.provider != current.provider || previous.owner != current.owner ||
        previous.modelData != current.modelData ||
        previous.transformHistory.size() != current.transformHistory.size() ||
        previous.quaternionHistory.size() != current.quaternionHistory.size() ||
        previous.jointCalculators != current.jointCalculators)
    {
        return false;
    }

    const bool animationChanged = previous.primaryAnimation != current.primaryAnimation ||
                                  previous.secondaryAnimation != current.secondaryAnimation;
    return !animationChanged || current.currentMorph < 1.0f;
}

bool evaluate_morph(J3DModel* model, SkeletonRecord* record, f32 step) {
    const SkeletonSnapshot& previous = record->previous;
    const SkeletonSnapshot& current = record->current;
    if (!morph_snapshots_compatible(previous, current) || current.primaryAnimation == nullptr) {
        return false;
    }

    f32 frame;
    const bool animationChanged = previous.primaryAnimation != current.primaryAnimation ||
                                  previous.secondaryAnimation != current.secondaryAnimation;
    if (animationChanged) {
        frame = current.frame.frame - current.frame.rate * (1.0f - step);
        if (is_looping(current.frame.attribute)) {
            frame = normalize_loop_frame(frame, current.frame.loop, current.frame.end);
        }
    } else if (!interpolate_frame(previous.frame, current.frame, step, &frame)) {
        return false;
    }
    if (!std::isfinite(frame)) {
        return false;
    }

    std::vector<J3DMtxCalc*> savedCalculators;
    if (!apply_joint_calculators(model, current, &savedCalculators)) {
        return false;
    }

    auto* morph = static_cast<mDoExt_morf_c*>(current.owner);
    const size_t jointCount = current.transformHistory.size();
    std::vector<J3DTransformInfo> savedTransforms(
        morph->mpTransformInfo, morph->mpTransformInfo + jointCount);
    std::vector<Quaternion> savedQuaternions(morph->mpQuat, morph->mpQuat + jointCount);
    const f32 savedFrame = morph->getFrame();
    const f32 savedCurrentMorph = morph->mCurMorf;
    const f32 savedPreviousMorph = morph->mPrevMorf;
    const f32 savedMorphStep = morph->mMorfStep;
    const f32 savedPrimaryFrame = current.primaryAnimation->getFrame();
    const f32 savedSecondaryFrame =
        current.secondaryAnimation != nullptr ? current.secondaryAnimation->getFrame() : 0.0f;
    f32 savedAnimationBlend = 0.0f;
    if (current.provider == ProviderKind::Morph2) {
        auto* morph2 = static_cast<mDoExt_McaMorf2*>(current.owner);
        savedAnimationBlend = morph2->getAnmRate();
        morph2->setPresentationAnmRate(
            previous.animationBlend + (current.animationBlend - previous.animationBlend) * step);
    }

    std::copy(
        previous.transformHistory.begin(), previous.transformHistory.end(), morph->mpTransformInfo);
    std::copy(previous.quaternionHistory.begin(), previous.quaternionHistory.end(), morph->mpQuat);
    morph->setFrameF(frame);
    morph->mPrevMorf = current.previousMorph;
    morph->mCurMorf = current.previousMorph + (current.currentMorph - current.previousMorph) * step;
    morph->mMorfStep = current.morphStep;
    current.primaryAnimation->setFrame(frame);
    if (current.secondaryAnimation != nullptr) {
        current.secondaryAnimation->setFrame(frame);
    }

    save_model_matrices(model, &record->restore);
    Vec savedScale;
    Mtx savedBase;
    apply_presentation_base(model, previous, current, step, &savedScale, savedBase);
    finish_model_pose(model);
    restore_presentation_base(model, savedScale, savedBase);

    std::copy(savedTransforms.begin(), savedTransforms.end(), morph->mpTransformInfo);
    std::copy(savedQuaternions.begin(), savedQuaternions.end(), morph->mpQuat);
    morph->setFrameF(savedFrame);
    morph->mCurMorf = savedCurrentMorph;
    morph->mPrevMorf = savedPreviousMorph;
    morph->mMorfStep = savedMorphStep;
    current.primaryAnimation->setFrame(savedPrimaryFrame);
    if (current.secondaryAnimation != nullptr) {
        current.secondaryAnimation->setFrame(savedSecondaryFrame);
    }
    if (current.provider == ProviderKind::Morph2) {
        static_cast<mDoExt_McaMorf2*>(current.owner)->setPresentationAnmRate(savedAnimationBlend);
    }
    restore_joint_calculators(model, savedCalculators);
    return true;
}

void presentation_begin(void* userWork) {
    auto* model = static_cast<J3DModel*>(userWork);
    auto it = s_records.find(model);
    if (it == s_records.end() || !it->second.currentValid ||
        dusk::frame_interp::presentation_sync_active())
    {
        return;
    }

    SkeletonRecord& record = it->second;
    if (record.restore.active) {
        restore_model_matrices(model, &record.restore);
    }

    if (!record.previousValid) {
        return;
    }

    const f32 step = dusk::frame_interp::get_interpolation_step();
    if (step <= 0.0f || step >= 1.0f) {
        return;
    }

    bool presented = false;
    if (record.provider == ProviderKind::Standard) {
        presented = evaluate_standard(model, &record, step);
    } else if (record.provider == ProviderKind::CapturedPose) {
        presented = evaluate_captured_pose(model, &record, step);
    } else {
        presented = evaluate_morph(model, &record, step);
    }
    if (presented) {
        ++s_presentedModelCount;
    }
}

void presentation_end(void* userWork) {
    auto* model = static_cast<J3DModel*>(userWork);
    auto it = s_records.find(model);
    if (it != s_records.end()) {
        restore_model_matrices(model, &it->second.restore);
    }
}

void store_snapshot(J3DModel* model, SkeletonSnapshot&& snapshot) {
    const uint64_t sequence = dusk::frame_interp::sim_tick_seq();
    auto& record = s_records[model];
    if (record.modelData != snapshot.modelData || record.owner != snapshot.owner ||
        record.provider != snapshot.provider)
    {
        const uint64_t scheduledSequence = record.scheduledSequence;
        restore_model_matrices(model, &record.restore);
        record = {};
        record.scheduledSequence = scheduledSequence;
        record.modelData = snapshot.modelData;
        record.owner = snapshot.owner;
        record.provider = snapshot.provider;
    }

    record.current = std::move(snapshot);
    record.currentValid = true;
    record.lastSeenSequence = sequence;
    if (record.scheduledSequence != sequence) {
        dusk::frame_interp::add_presentation_callbacks(
            &presentation_begin, &presentation_end, model);
        record.scheduledSequence = sequence;
    }
}

void store_fallback(J3DModel* model, void* owner) {
    SkeletonSnapshot snapshot;
    snapshot.modelData = model->getModelData();
    snapshot.owner = owner;
    snapshot.provider = ProviderKind::Fallback;
    store_snapshot(model, std::move(snapshot));
}

bool model_can_present_captured_pose(J3DModel* model) {
    return model != nullptr && model->getModelData() != nullptr && model->getMtxBuffer() != nullptr &&
           model->getSkinDeform() == nullptr && !model->checkFlag(J3DMdlFlag_SkinPosCpu) &&
           !model->checkFlag(J3DMdlFlag_SkinNrmCpu);
}

void store_captured_pose_or_fallback(J3DModel* model, void* owner) {
    if (!model_can_present_captured_pose(model)) {
        store_fallback(model, owner);
        return;
    }

    SkeletonSnapshot snapshot;
    snapshot.modelData = model->getModelData();
    snapshot.owner = owner;
    snapshot.provider = ProviderKind::CapturedPose;
    if (!capture_joint_hierarchy(model, &snapshot)) {
        store_fallback(model, owner);
        return;
    }
    store_snapshot(model, std::move(snapshot));
}

FrameState capture_frame_state(mDoExt_morf_c& morph) {
    return {
        .frame = morph.getFrame(),
        .rate = morph.getPlaySpeed(),
        .start = static_cast<s16>(morph.getStartFrame()),
        .end = static_cast<s16>(morph.getEndFrame()),
        .loop = static_cast<s16>(morph.getLoopFrame()),
        .attribute = static_cast<u8>(morph.getPlayMode()),
    };
}

template <typename Morph>
void capture_morph_impl(Morph* morph, ProviderKind provider,
    J3DAnmTransform* secondaryAnimation = nullptr, f32 animationBlend = 0.0f) {
    if (!dusk::frame_interp::is_enabled() || !dusk::frame_interp::is_sim_frame() ||
        morph == nullptr || morph->getModel() == nullptr || morph->getAnm() == nullptr)
    {
        return;
    }

    J3DModel* model = morph->getModel();
    J3DModel* anchorModel = nullptr;
    s16 anchorJoint = -1;
    if (find_presentation_anchor(model, &anchorModel, &anchorJoint)) {
        store_captured_pose_or_fallback(model, morph);
        return;
    }
    if (!keyed_animation_supported(morph->getAnm()) ||
        (secondaryAnimation != nullptr && !keyed_animation_supported(secondaryAnimation)) ||
        !model_can_recalculate(model) ||
        (morph->hasPresentationCallbacks() && !callbacks_allowed(morph)))
    {
        store_captured_pose_or_fallback(model, morph);
        return;
    }

    const u16 jointCount = model->getModelData()->getJointNum();
    if (jointCount == 0 || morph->mpTransformInfo == nullptr || morph->mpQuat == nullptr) {
        store_captured_pose_or_fallback(model, morph);
        return;
    }

    SkeletonSnapshot snapshot;
    snapshot.modelData = model->getModelData();
    snapshot.owner = morph;
    snapshot.provider = provider;
    snapshot.frame = capture_frame_state(*morph);
    snapshot.primaryAnimation = morph->getAnm();
    snapshot.secondaryAnimation = secondaryAnimation;
    snapshot.currentMorph = morph->mCurMorf;
    snapshot.previousMorph = morph->mPrevMorf;
    snapshot.morphStep = morph->mMorfStep;
    snapshot.animationBlend = animationBlend;
    capture_joint_calculators(model, &snapshot);
    capture_base_matrix(model, &snapshot.baseMatrix);
    snapshot.transformHistory.assign(morph->mpTransformInfo, morph->mpTransformInfo + jointCount);
    snapshot.quaternionHistory.assign(morph->mpQuat, morph->mpQuat + jointCount);
    store_snapshot(model, std::move(snapshot));
}

}  // namespace

namespace dusk::presentation_skeleton {

void begin_sim_tick() {
    const uint64_t sequence = frame_interp::sim_tick_seq();
    s_presentedModelCount = 0;
    s_modelAttachments.clear();
    for (auto it = s_records.begin(); it != s_records.end();) {
        SkeletonRecord& record = it->second;
        restore_model_matrices(it->first, &record.restore);
        if (record.currentValid) {
            record.previous = record.current;
            record.previousValid = true;
        }
        record.currentValid = false;

        if (sequence > record.lastSeenSequence + 120) {
            auto stale = it++;
            s_records.erase(stale);
        } else {
            ++it;
        }
    }
}

void clear() {
    for (auto& [model, record] : s_records) {
        restore_model_matrices(model, &record.restore);
    }
    s_records.clear();
    s_safeModelCallbacks.clear();
    s_safeMorphCallbacks.clear();
    s_modelAttachments.clear();
    s_presentedModelCount = 0;
}

void capture_model(J3DModel* model) {
    if (!frame_interp::is_enabled() || !frame_interp::is_sim_frame() || model == nullptr ||
        model->getModelData() == nullptr || model->getMtxBuffer() == nullptr)
    {
        return;
    }

    J3DModelData* modelData = model->getModelData();
    J3DJointTree& jointTree = modelData->getJointTree();
    if (modelData->getJointNum() == 0 || jointTree.getRootNode() == nullptr) {
        return;
    }
    J3DMtxCalc* rootCalculator = jointTree.getRootNode()->getMtxCalc();
    if (rootCalculator == nullptr) {
        rootCalculator = jointTree.getBasicMtxCalc();
    }

    bool hasStandardAnimation = false;
    bool hasMorphAnimation = false;
    bool hasCustomCalculator = false;
    auto classify_calculator = [&](J3DMtxCalc* calculator) {
        if (auto* animationCalculator = dynamic_cast<J3DMtxCalcAnmBase*>(calculator)) {
            hasStandardAnimation |= animationCalculator->getAnmTransform() != nullptr;
        } else if (auto* morphCalculator = dynamic_cast<mDoExt_morf_c*>(calculator)) {
            hasMorphAnimation |= morphCalculator->getAnm() != nullptr;
        } else if (calculator != nullptr && calculator != jointTree.getBasicMtxCalc()) {
            hasCustomCalculator = true;
        }
    };
    classify_calculator(rootCalculator);
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        classify_calculator(modelData->getJointNodePointer(i)->getMtxCalc());
    }

    // The specialized morph capture runs immediately after J3DModel::calc().
    if (hasMorphAnimation) {
        return;
    }
    J3DModel* anchorModel = nullptr;
    s16 anchorJoint = -1;
    if (find_presentation_anchor(model, &anchorModel, &anchorJoint)) {
        store_captured_pose_or_fallback(model, rootCalculator);
        return;
    }
    if (!hasStandardAnimation) {
        if (hasCustomCalculator || has_joint_callbacks(model) || model->mCalcCallBack != nullptr) {
            store_captured_pose_or_fallback(model, rootCalculator);
        }
        return;
    }
    if (!model_can_recalculate(model) ||
        dynamic_cast<J3DMtxCalcAnmBase*>(rootCalculator) == nullptr)
    {
        store_captured_pose_or_fallback(model, rootCalculator);
        return;
    }

    SkeletonSnapshot snapshot;
    snapshot.modelData = modelData;
    snapshot.owner = rootCalculator;
    snapshot.provider = ProviderKind::Standard;
    capture_joint_calculators(model, &snapshot);
    capture_base_matrix(model, &snapshot.baseMatrix);

    auto add_calculator = [&snapshot](J3DMtxCalc* calculator) {
        if (calculator == nullptr) {
            return true;
        }
        auto* animationCalculator = dynamic_cast<J3DMtxCalcAnmBase*>(calculator);
        if (animationCalculator == nullptr ||
            !keyed_animation_supported(animationCalculator->getAnmTransform()))
        {
            return false;
        }
        for (const AnimationSource& source : snapshot.animations) {
            if (source.calculator == animationCalculator) {
                return true;
            }
        }
        J3DAnmTransform* animation = animationCalculator->getAnmTransform();
        snapshot.animations.push_back({
            .calculator = animationCalculator,
            .animation = animation,
            .frame = animation->getFrame(),
            .frameMax = animation->getFrameMax(),
            .attribute = animation->getAttribute(),
        });
        return true;
    };

    if (!add_calculator(rootCalculator)) {
        store_captured_pose_or_fallback(model, rootCalculator);
        return;
    }
    for (u16 i = 0; i < modelData->getJointNum(); ++i) {
        J3DMtxCalc* calculator = modelData->getJointNodePointer(i)->getMtxCalc();
        if (calculator != nullptr && !add_calculator(calculator)) {
            store_captured_pose_or_fallback(model, rootCalculator);
            return;
        }
    }
    if (snapshot.animations.empty()) {
        store_captured_pose_or_fallback(model, rootCalculator);
        return;
    }
    store_snapshot(model, std::move(snapshot));
}

void capture_morph(mDoExt_McaMorf* morph) {
    capture_morph_impl(morph, ProviderKind::Morph);
}

void capture_morph(mDoExt_McaMorfSO* morph) {
    capture_morph_impl(morph, ProviderKind::MorphSO);
}

void capture_morph(mDoExt_McaMorf2* morph) {
    capture_morph_impl(morph, ProviderKind::Morph2,
        morph != nullptr ? morph->getSecondaryAnm() : nullptr,
        morph != nullptr ? morph->getAnmRate() : 0.0f);
}

void set_model_callbacks_safe(J3DModel* model, bool safe) {
    if (model != nullptr) {
        s_safeModelCallbacks[model] = safe;
    }
}

void set_morph_callbacks_safe(mDoExt_morf_c* morph, bool safe) {
    if (morph != nullptr) {
        s_safeMorphCallbacks[morph] = safe;
    }
}

void set_model_attachment(J3DModel* model, J3DModel* parentModel, unsigned short parentJoint) {
    if (frame_interp::is_enabled() && frame_interp::is_sim_frame() && model != nullptr &&
        parentModel != nullptr)
    {
        s_modelAttachments[model] = {
            .parentModel = parentModel,
            .parentJoint = parentJoint,
        };
    }
}

size_t tracked_model_count() {
    return s_records.size();
}

size_t presented_model_count() {
    return s_presentedModelCount;
}

}  // namespace dusk::presentation_skeleton
