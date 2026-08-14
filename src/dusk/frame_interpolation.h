#pragma once

#include "SSystem/SComponent/c_angle.h"
#include "SSystem/SComponent/c_sxyz.h"
#include "SSystem/SComponent/c_xyz.h"

#include <cmath>
#include <cstring>
#include <dolphin/mtx.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

class camera_process_class;
class fopAc_ac_c;
class J3DModel;
class view_class;

#ifdef __cplusplus
namespace dusk::frame_interp {

enum class CameraInterpolationKind {
    Unavailable,
    Authoritative,
    Previous,
    Linear,
    Orbit,
    SemanticOrbit,
};

enum class CameraInterpolationFallbackReason {
    None,
    MissingSnapshots,
    IncompatibleCamera,
    UnsupportedAlgorithm,
    MissingTarget,
    TargetChanged,
    TargetPoseUnavailable,
};

struct CameraInterpolationDiagnostics {
    CameraInterpolationKind kind = CameraInterpolationKind::Unavailable;
    float step = 0.0f;
    float previousRadius = 0.0f;
    float currentRadius = 0.0f;
    float presentedRadius = 0.0f;
    float linearRadius = 0.0f;
    float linearRadiusError = 0.0f;
    float maxLinearRadiusError = 0.0f;
    float cameraFrames = 0.0f;
    float collisionCorrection = 0.0f;
    float maxCollisionCorrection = 0.0f;
    uint64_t simTickSeq = 0;
    uint64_t collisionHitCount = 0;
    int algorithm = -1;
    int mode = -1;
    int type = -1;
    int style = -1;
    CameraInterpolationFallbackReason fallbackReason =
        CameraInterpolationFallbackReason::MissingSnapshots;
    bool compatibleRig = false;
    bool actorAnchored = false;
    bool collisionHit = false;
    bool rebased = false;
    bool valid = false;
};

struct ActorPresentationPose {
    cXyz position{};
    cXyz attentionPosition{};
    cXyz eyePosition{};
    csXyz shapeAngle{};
};

void begin_record();
void end_record();
void begin_sim_tick();
uint64_t sim_tick_seq();
void begin_frame(float step);
void interpolate();
float get_interpolation_step();

void request_presentation_sync();
bool presentation_sync_active();

bool is_enabled();

// TODO: These should be phased out as UI is progressively updated to use game_clock
bool get_ui_tick_pending();

bool is_sim_frame();

void record_camera(::camera_process_class* cam, int camera_id);
void reset_camera();
void interp_view(::view_class* view);
const CameraInterpolationDiagnostics& camera_interpolation_diagnostics();

void capture_actor_pose(::fopAc_ac_c* actor);
void erase_actor_pose(::fopAc_ac_c* actor);
bool sample_actor_pose(const ::fopAc_ac_c* actor, float step, ActorPresentationPose* pose);
size_t recorded_actor_pose_count();
void record_final_mtx(Mtx m, const void *key);
void record_final_mtx(Mtx m);

bool lookup_replacement(const void* key, Mtx out);
bool lookup_concat_replacement(const void* lhs, const void* rhs, Mtx out);

typedef void (*InterpolationCallBack)(void* pUserWork);
void add_interpolation_callback(InterpolationCallBack pCallBack, void* pUserWork);
void add_model_interpolation_callbacks(::J3DModel* model, InterpolationCallBack before,
                                       InterpolationCallBack after, void* pUserWork);
bool has_model_interpolation_callbacks(const ::J3DModel* model);
void begin_model_interpolation(::J3DModel* model);
void end_model_interpolation(::J3DModel* model);

void begin_presentation_camera();
void end_presentation_camera();

inline s16 lerp(s16 lhs, s16 rhs, float step) {
    const f32 ra = S2RAD(lhs);
    const f32 d = remainderf(S2RAD(rhs) - ra, 2.0f * M_PI);
    return cAngle::Radian_to_SAngle(ra + d * step);
}

inline void lerp(cXyz& out, const cXyz& lhs, const cXyz& rhs, float step) {
    out.x = lhs.x + (rhs.x - lhs.x) * step;
    out.y = lhs.y + (rhs.y - lhs.y) * step;
    out.z = lhs.z + (rhs.z - lhs.z) * step;
}

inline void lerp(csXyz& out, const csXyz& lhs, const csXyz& rhs, float step) {
    out.x = lerp(lhs.x, rhs.x, step);
    out.y = lerp(lhs.y, rhs.y, step);
    out.z = lerp(lhs.z, rhs.z, step);
}

inline void lerp(Mtx& out, const Mtx& lhs, const Mtx& rhs, float step) {
    for (size_t row = 0; row < 3; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            const float l = lhs[row][col];
            out[row][col] = l + (rhs[row][col] - l) * step;
        }
    }
}

template <typename T, int capacity>
class DualBuffer {
public:
    explicit DualBuffer(T* dst = NULL)
        : m_prev_valid(false),
          m_curr_valid(false),
          m_count(0),
          m_dst(dst),
          m_post(NULL),
          m_post_user(NULL),
          m_rolled_seq(0) {}

    void bind(T* dst) { m_dst = dst; }

    void reset() {
        m_prev_valid = false;
        m_curr_valid = false;
        m_count = 0;
        m_rolled_seq = 0;
    }

    bool ready() const { return m_prev_valid && m_curr_valid; }

    bool fits(int count) const {
        if (count > capacity) {
            return false;
        }
        return count > 0;
    }

    void roll() {
        if (!is_enabled() || !m_curr_valid || m_count <= 0) {
            return;
        }
        std::memcpy(m_prev, m_curr, static_cast<size_t>(m_count) * sizeof(T));
        m_prev_valid = true;
    }

    void capture(const T* src, int count) {
        if (!fits(count) || !is_enabled() || src == NULL) {
            return;
        }
        std::memcpy(m_curr, src, static_cast<size_t>(count) * sizeof(T));
        m_count = count;
        m_curr_valid = true;
    }

    void apply(T* dst, int count) const {
        if (!fits(count) || dst == NULL || !ready()) {
            return;
        }
        const f32 step = get_interpolation_step();
        for (int i = 0; i < count; ++i) {
            lerp(dst[i], m_prev[i], m_curr[i], step);
        }
    }

    void schedule(void (*post)(void*) = NULL, void* post_user = NULL) {
        if (!is_enabled() || m_dst == NULL || !fits(m_count)) {
            return;
        }
        m_post = post;
        m_post_user = post_user;
        add_interpolation_callback(&present_trampoline, this);
    }

    void capture_on_sim(const T* src, int count) {
        on_sim_tick();
        capture(src, count);
    }

    void capture_and_schedule(const T* src, int count, void (*post)(void*) = NULL, void* post_user = NULL) {
        roll();
        capture(src, count);
        schedule(post, post_user);
    }

    void writeback(T* src_and_dst, int count, void (*post)(void*) = NULL, void* post_user = NULL) {
        bind(src_and_dst);
        capture_and_schedule(src_and_dst, count, post, post_user);
    }

    void writeback_on_sim_tick(T* src_and_dst, int count, void (*post)(void*) = NULL,
                               void* post_user = NULL) {
        bind(src_and_dst);
        capture_on_sim(src_and_dst, count);
        schedule(post, post_user);
    }

private:
    static void present_trampoline(void* user) {
        static_cast<DualBuffer*>(user)->present();
    }

    void present() {
        apply(m_dst, m_count);
        if (m_post != NULL) {
            m_post(m_post_user);
        }
    }

    void on_sim_tick() {
        const uint64_t seq = sim_tick_seq();
        if (seq == m_rolled_seq) {
            return;
        }
        m_rolled_seq = seq;
        roll();
    }

    T m_prev[capacity];
    T m_curr[capacity];
    bool m_prev_valid;
    bool m_curr_valid;
    int m_count;
    T* m_dst;
    void (*m_post)(void*);
    void* m_post_user;
    uint64_t m_rolled_seq;
};

template <typename T, int capacity, int strand_count>
class DualBufferGroup {
public:
    typedef DualBuffer<T, capacity> Buffer;

    DualBuffer<T, capacity>& operator[](int i) { return m_buffers[i]; }
    const DualBuffer<T, capacity>& operator[](int i) const { return m_buffers[i]; }

    void reset() {
        for (int i = 0; i < strand_count; ++i) {
            m_buffers[i].reset();
        }
    }

    void schedule(void (*post)(void*) = NULL, void* post_user = NULL) {
        for (int i = 0; i < strand_count; ++i) {
            m_buffers[i].schedule();
        }
        finish_post(post, post_user);
    }

    void writeback(T* const* src_and_dst, int count, void (*post)(void*) = NULL, void* post_user = NULL) {
        for (int i = 0; i < strand_count; ++i) {
            m_buffers[i].writeback(src_and_dst[i], count);
        }
        finish_post(post, post_user);
    }

private:
    static void finish_post(void (*post)(void*), void* post_user) {
        if (post != NULL) {
            add_interpolation_callback(post, post_user);
        }
    }

    Buffer m_buffers[strand_count];
};

}  // namespace dusk::frame_interp
#endif
