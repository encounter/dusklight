#pragma once

#include <cstdint>

// Frame markers for the mod gfx service, called from the render driver (m_Do_graphic.cpp) at GX
// record time. Kept free of mods/svc/gfx.h so game code does not pull in webgpu.h; public-stage
// values mirror GfxStage and are static_asserted against it in loader/gfx.cpp.

namespace dusk::mods {

inline constexpr uint32_t GfxStageSceneAfterTerrain = 0;
inline constexpr uint32_t GfxStageFrameBeforeHud = 1;
inline constexpr uint32_t GfxStageFrameAfterHud = 2;
inline constexpr uint32_t GfxStageSceneBegin = 3;
inline constexpr uint32_t GfxStageSceneAfterOpaque = 4;
inline constexpr uint32_t GfxStageSceneListsReady = 5;

void gfx_run_stage(
    uint32_t stage, uint32_t windowIndex = 0, const void* gameView = nullptr,
    const void* gameViewport = nullptr);

}  // namespace dusk::mods
