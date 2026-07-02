#ifndef DUSK_TEXTURE_REPLACEMENTS_HPP
#define DUSK_TEXTURE_REPLACEMENTS_HPP

#include <cstdint>

namespace dusk::texture_replacements {

// Mod-registered replacements always win over the user's texture_replacements config directory
// (mods use their m_mods index + 1 as priority; see sync_texture_replacements).
inline constexpr int32_t kUserTextureReplacementPriority = -1'000'000;

void reload();
void set_enabled(bool enabled);
void shutdown();

}

#endif
