#include "assets.hpp"

#include "battery/embed.hpp"

namespace randomizer {

EmbeddedAsset shadow_crystal_bti() {
    const auto bti = b::embed<"assets/textures/shadow_crystal.bti">();
    return {bti.data(), bti.size()};
}

}  // namespace randomizer
