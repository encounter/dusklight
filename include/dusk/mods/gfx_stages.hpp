#pragma once

#include <cstdint>

// Frame markers for the mod gfx service, called from the render driver (m_Do_graphic.cpp) at GX
// record time. Kept free of mods/svc/gfx.h so game code does not pull in webgpu.h; the values
// mirror GfxStage and are static_asserted against it in loader/gfx.cpp.

namespace dusk::mods {

inline constexpr uint32_t GfxStageWorldLate = 0;
inline constexpr uint32_t GfxStageBeforeHud = 1;
inline constexpr uint32_t GfxStageAfterHud = 2;
inline constexpr uint32_t GfxStageWorldBeforeTerrain = 3;
inline constexpr uint32_t GfxStageWorldListsReady = 4;

void gfx_run_stage(
    uint32_t stage, uint32_t windowIndex = 0, const void* gameView = nullptr,
    const void* gameViewport = nullptr);

}  // namespace dusk::mods
