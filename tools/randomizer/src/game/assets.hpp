#pragma once

#include <cstddef>

namespace randomizer {

struct EmbeddedAsset {
    const void* data;
    size_t size;
};

// assets/textures/shadow_crystal.bti (rando icon for the shadow crystal item).
EmbeddedAsset shadow_crystal_bti();

}  // namespace randomizer
