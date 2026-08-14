#pragma once

#include <cmath>
#include <cstdint>

class J2DAnmBase;
class J2DAnmTransform;
class J2DPane;

namespace dusk::game_clock {

// Amount of time that a simulation tick advances
constexpr float kSimPeriod = 1.0f / 30.0f;
constexpr float kUiMaximumDt = 0.05f;
constexpr float kUiInitialDt = 1.0f / 60.0f;

float ui_dt();
inline float original_frames() { return ui_dt() / kSimPeriod; }

struct FrameTiming {
    // Amount of time elapsed in seconds since the last advance
    float dt;
    // Whether interpolation is active
    bool interpolating;
    // Run simulation and presentation separately (for interpolation or time scaling)
    bool separatePresentation;
    // Number of simulation ticks to run
    int numSimTicks;
    // Changes whenever presentation history must be discarded and re-anchored.
    uint64_t presentationEpoch;
};
extern FrameTiming g_frameTiming;

void initialize();
void reset();
const FrameTiming& advance();
void begin_sim_tick();
void commit_sim_tick();
bool is_sim_tick_active();
float sample_interpolation_step();

inline void advance_looping_frame(float& frame, float speed, float max) {
    if (max <= 0.0f) {
        return;
    }
    frame += speed * original_frames();
    if (frame >= max) {
        frame = fmodf(frame, max);
    }
}

inline void advance_toward_frame(float& frame, float target, float speed) {
    if (frame == target) {
        return;
    }
    float step = speed * original_frames();
    if (frame < target) {
        frame += step;
        if (frame > target) {
            frame = target;
        }
    } else {
        frame -= step;
        if (frame < target) {
            frame = target;
        }
    }
}

void present_looping(float& frame, J2DAnmBase* anm, float speed);
void present_toward(float& frame, float target, J2DAnmTransform* anm, J2DPane* pane = nullptr);

} // namespace dusk::game_clock
