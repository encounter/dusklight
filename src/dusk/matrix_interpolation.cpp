#include "dusk/matrix_interpolation.h"

#include <cmath>
#include <cstring>

namespace dusk::matrix_interp {
namespace {

void lerp_elements(Mtx out, const Mtx lhs, const Mtx rhs, float step) {
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            const float value = lhs[row][column];
            out[row][column] = value + (rhs[row][column] - value) * step;
        }
    }
}

Vec combine_vec(const Vec& lhs, const Vec& rhs, float rhsScale) {
    Vec scaledRhs;
    VECScale(&rhs, &scaledRhs, rhsScale);

    Vec result;
    VECAdd(&lhs, &scaledRhs, &result);
    return result;
}

bool normalize_vec(Vec* value, float* magnitude) {
    constexpr float kMinimumMagnitudeSquared = 1.0e-12f;
    const float magnitudeSquared = VECSquareMag(value);
    if (!std::isfinite(magnitudeSquared) || magnitudeSquared <= kMinimumMagnitudeSquared) {
        return false;
    }

    *magnitude = VECMag(value);
    VECNormalize(value, value);
    return true;
}

bool matrix_is_finite(const Mtx matrix) {
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            if (!std::isfinite(matrix[row][column])) {
                return false;
            }
        }
    }
    return true;
}

// Adapted from RT64's matrix decomposition implementation.
// https://github.com/rt64/rt64/blob/5473732a822a4423b5696e7cb18fecc425a59875/src/common/rt64_math.cpp#L245
// License: MIT
bool decompose(const Mtx matrix, DecomposedMatrix* out) {
    if (!matrix_is_finite(matrix)) {
        return false;
    }

    DecomposedMatrix result;
    result.translation = {matrix[0][3], matrix[1][3], matrix[2][3]};

    Vec axes[3] = {
        {matrix[0][0], matrix[1][0], matrix[2][0]},
        {matrix[0][1], matrix[1][1], matrix[2][1]},
        {matrix[0][2], matrix[1][2], matrix[2][2]},
    };

    if (!normalize_vec(&axes[0], &result.scale.x)) {
        return false;
    }

    result.skew.z = VECDotProduct(&axes[0], &axes[1]);
    axes[1] = combine_vec(axes[1], axes[0], -result.skew.z);
    if (!normalize_vec(&axes[1], &result.scale.y)) {
        return false;
    }
    result.skew.z /= result.scale.y;

    result.skew.y = VECDotProduct(&axes[0], &axes[2]);
    axes[2] = combine_vec(axes[2], axes[0], -result.skew.y);
    result.skew.x = VECDotProduct(&axes[1], &axes[2]);
    axes[2] = combine_vec(axes[2], axes[1], -result.skew.x);
    if (!normalize_vec(&axes[2], &result.scale.z)) {
        return false;
    }
    result.skew.y /= result.scale.z;
    result.skew.x /= result.scale.z;

    Vec cross;
    VECCrossProduct(&axes[1], &axes[2], &cross);
    const float orientation = VECDotProduct(&axes[0], &cross);
    if (!std::isfinite(orientation)) {
        return false;
    }
    result.coordinateFlip = orientation < 0.0f;
    if (result.coordinateFlip) {
        VECScale(&result.scale, &result.scale, -1.0f);
        for (Vec& axis : axes) {
            VECScale(&axis, &axis, -1.0f);
        }
    }

    Mtx rotationMatrix = {
        {axes[0].x, axes[1].x, axes[2].x, 0.0f},
        {axes[0].y, axes[1].y, axes[2].y, 0.0f},
        {axes[0].z, axes[1].z, axes[2].z, 0.0f},
    };
    QUATMtx(&result.rotation, rotationMatrix);
    QUATNormalize(&result.rotation, &result.rotation);
    const float rotationMagnitudeSquared = QUATDotProduct(&result.rotation, &result.rotation);
    if (!std::isfinite(rotationMagnitudeSquared) || rotationMagnitudeSquared < 0.5f) {
        return false;
    }

    result.valid = true;
    *out = result;
    return true;
}

void recompose(Mtx out, const DecomposedMatrix& matrix) {
    Mtx rotationMatrix;
    MTXQuat(rotationMatrix, &matrix.rotation);

    for (size_t row = 0; row < 3; ++row) {
        out[row][0] = rotationMatrix[row][0] * matrix.scale.x;
        out[row][1] =
            (rotationMatrix[row][0] * matrix.skew.z + rotationMatrix[row][1]) * matrix.scale.y;
        out[row][2] = (rotationMatrix[row][0] * matrix.skew.y +
                          rotationMatrix[row][1] * matrix.skew.x + rotationMatrix[row][2]) *
                      matrix.scale.z;
    }
    out[0][3] = matrix.translation.x;
    out[1][3] = matrix.translation.y;
    out[2][3] = matrix.translation.z;
}

DecomposedMatrix equivalent_decomposition(const DecomposedMatrix& source, size_t axis) {
    DecomposedMatrix result = source;
    Quaternion halfTurn{};
    if (axis == 0) {
        halfTurn.x = 1.0f;
        result.scale.y = -result.scale.y;
        result.scale.z = -result.scale.z;
        result.skew.y = -result.skew.y;
        result.skew.z = -result.skew.z;
    } else if (axis == 1) {
        halfTurn.y = 1.0f;
        result.scale.x = -result.scale.x;
        result.scale.z = -result.scale.z;
        result.skew.x = -result.skew.x;
        result.skew.z = -result.skew.z;
    } else {
        halfTurn.z = 1.0f;
        result.scale.x = -result.scale.x;
        result.scale.y = -result.scale.y;
        result.skew.x = -result.skew.x;
        result.skew.y = -result.skew.y;
    }
    QUATMultiply(&source.rotation, &halfTurn, &result.rotation);
    return result;
}

// Adapted from RT64's rigid-body decomposition selection.
// https://github.com/rt64/rt64/blob/6dc3d4d9d4b310843e803e979a6bbedffe2cdbe9/src/hle/rt64_rigid_body.cpp#L101-L129
// License: MIT
DecomposedMatrix choose_previous_decomposition(
    const DecomposedMatrix& previous, const DecomposedMatrix& current) {
    DecomposedMatrix best = previous;
    if (previous.coordinateFlip == current.coordinateFlip) {
        return best;
    }

    float bestSimilarity = std::abs(QUATDotProduct(&best.rotation, &current.rotation));
    for (size_t axis = 0; axis < 3; ++axis) {
        DecomposedMatrix candidate = equivalent_decomposition(previous, axis);
        const float similarity = std::abs(QUATDotProduct(&candidate.rotation, &current.rotation));
        if (similarity > bestSimilarity) {
            best = candidate;
            bestSimilarity = similarity;
        }
    }
    return best;
}

Vec lerp_vec(const Vec& lhs, const Vec& rhs, float step) {
    return {
        lhs.x + (rhs.x - lhs.x) * step,
        lhs.y + (rhs.y - lhs.y) * step,
        lhs.z + (rhs.z - lhs.z) * step,
    };
}

Quaternion interpolate_rotation(const Quaternion& previous, const Quaternion& current, float step) {
    Quaternion result;
    QUATSlerp(&previous, &current, &result, step);
    return result;
}

}  // namespace

void record(MatrixSample* sample, const Mtx value) {
    MTXCopy(value, sample->value);
    sample->decomposed.valid = false;
}

void finalize(MatrixSample* sample) {
    sample->decomposed.valid = decompose(sample->value, &sample->decomposed);
}

void interpolate(Mtx out, const MatrixSample& previous, const MatrixSample& current, float step) {
    if (step <= 0.0f) {
        MTXCopy(previous.value, out);
        return;
    }
    if (step >= 1.0f || std::memcmp(previous.value, current.value, sizeof(Mtx)) == 0) {
        MTXCopy(current.value, out);
        return;
    }

    if (previous.decomposed.valid && current.decomposed.valid) {
        const DecomposedMatrix adjustedPrevious =
            choose_previous_decomposition(previous.decomposed, current.decomposed);
        DecomposedMatrix result;
        result.rotation =
            interpolate_rotation(adjustedPrevious.rotation, current.decomposed.rotation, step);
        QUATNormalize(&result.rotation, &result.rotation);
        result.scale = lerp_vec(adjustedPrevious.scale, current.decomposed.scale, step);
        result.skew = lerp_vec(adjustedPrevious.skew, current.decomposed.skew, step);
        result.translation =
            lerp_vec(adjustedPrevious.translation, current.decomposed.translation, step);
        recompose(out, result);
        if (matrix_is_finite(out)) {
            return;
        }
    }

    lerp_elements(out, previous.value, current.value, step);
}

}  // namespace dusk::matrix_interp
