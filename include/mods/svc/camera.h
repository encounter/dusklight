#pragma once

#include "mods/api.h"

#define CAMERA_SERVICE_ID "dev.twilitrealm.dusklight.camera"
#define CAMERA_SERVICE_MAJOR 1u
#define CAMERA_SERVICE_MINOR 0u

/*
 * Snapshot of the active game camera, as used for the frame currently being
 * recorded. Frame interpolation rewrites the live camera state in place before
 * each rendered frame, so values read from inside a gfx stage callback always
 * match what the frame actually renders with — on simulation and presentation
 * frames alike.
 *
 * Matrix conventions: every matrix is a column-major float[16] using the
 * matrix * column-vector convention, ready to memcpy into a WGSL mat4x4f.
 * Naming reads right to left: a_from_b transforms b-space into a-space.
 * NOTE: this is the TRANSPOSE of the game's row-major Mtx/Mtx44 layout; mods
 * that hook game code and want the raw game matrices should read
 * dComIfGd_getView() directly instead.
 *
 * View space is right-handed with -Z forward. Projection matrices are in
 * WebGPU clip convention exactly as rendered — reversed-Z (depth 1.0 at the
 * near plane, 0.0 at far), including any widescreen adjustments the game
 * applied this frame. proj_from_view * view_pos reproduces the depth values
 * found in GfxResolvedTargets depth snapshots, assuming the usual [0, 1]
 * viewport depth range (see viewport_near_z/viewport_far_z below).
 *
 * Unprojecting a depth-snapshot texel at UV (u, v) with sampled depth d:
 *   ndc = (u * 2 - 1, 1 - v * 2, d)   // WebGPU framebuffer y is down
 *   world4 = world_from_proj * vec4f(ndc, 1.0); world = world4.xyz / world4.w
 */
typedef struct CameraInfo {
    uint32_t struct_size;

    float view_from_world[16]; /* the view matrix */
    float world_from_view[16]; /* its inverse */
    float proj_from_view[16];  /* WebGPU-convention projection (reversed-Z) */
    float view_from_proj[16];  /* its inverse */
    float proj_from_world[16]; /* proj_from_view * view_from_world */
    float world_from_proj[16]; /* one-step depth-buffer -> world unproject */

    /* Raw camera parameters, world space, interpolation-corrected. Note the
     * projection matrices may include adjustments not derivable from these
     * (widescreen wide-zoom); prefer the matrices for anything screen-space. */
    float eye[3];
    float center[3];
    float up[3];
    float fovy;   /* vertical field of view, degrees */
    float aspect;
    float near_plane;
    float far_plane;
    float bank;   /* camera roll, degrees */

    /* Camera window viewport in GC logical framebuffer coordinates (640x480
     * space); scale by render size / logical size for pixels. */
    float viewport_x;
    float viewport_y;
    float viewport_width;
    float viewport_height;
    /* GX viewport depth range (almost always 0 and 1). If not, remap a
     * sampled depth d to NDC z with
     *   (d - (1 - viewport_far_z)) / (viewport_far_z - viewport_near_z)
     * before unprojecting. */
    float viewport_near_z;
    float viewport_far_z;
} CameraInfo;

#define CAMERA_INFO_INIT {sizeof(CameraInfo)}

typedef struct CameraService {
    ServiceHeader header;

    /* Snapshot the active game camera. Game thread only. Call from a gfx
     * stage callback (or mod_update, which runs inside the frame) for values
     * matching the frame being recorded. During scene stages this is the
     * current camera window's camera; at GFX_STAGE_FRAME_BEFORE_HUD or
     * GFX_STAGE_FRAME_AFTER_HUD it is the last window's. Returns
     * MOD_UNAVAILABLE while no camera exists (menus before the first in-game
     * frame; always during mod_initialize). */
    ModResult (*get_camera)(ModContext* ctx, CameraInfo* out_info);
} CameraService;

#ifdef __cplusplus
#include "mods/service.hpp"

template <>
struct dusk::mods::ServiceTraits<CameraService> {
    static constexpr const char* id = CAMERA_SERVICE_ID;
    static constexpr uint16_t major_version = CAMERA_SERVICE_MAJOR;
};
#endif
