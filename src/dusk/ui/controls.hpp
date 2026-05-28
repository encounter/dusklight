#pragma once

namespace dusk::ui {

struct EquipTarget {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool valid = false;
};

enum class Control {
    A,
    B,
    X,
    Y,
    Z,
    L,
    R,
    FIRST_PERSON,
    ITEMS,
    COLLECTIONS,
    MAP,
    SKIP,
    COUNT,
};

}  // namespace dusk::ui
