#pragma once

#include "mtx.h"

namespace dusk::matrix_interp {

struct DecomposedMatrix {
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec scale{1.0f, 1.0f, 1.0f};
    Vec skew{};
    Vec translation{};
    bool coordinateFlip = false;
    bool valid = false;
};

struct MatrixSample {
    Mtx value{};
    DecomposedMatrix decomposed;
};

void record(MatrixSample* sample, const Mtx value);
void finalize(MatrixSample* sample);
void interpolate(Mtx out, const MatrixSample& previous, const MatrixSample& current, float step);

}  // namespace dusk::matrix_interp
