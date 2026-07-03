#include "registry.hpp"

#include "dusk/mods/loader/loader.hpp"

#include "d/d_com_inf_game.h"

namespace dusk::mods::svc {
namespace {

/* Row-major 4x4 working representation (matrix * column-vector, like the
 * game's Mtx/Mtx44); transposed into CameraInfo's column-major layout last. */
struct Mtx4 {
    f32 m[4][4];
};

Mtx4 expand_affine(const Mtx m) {
    Mtx4 out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            out.m[r][c] = m[r][c];
        }
    }
    out.m[3][3] = 1.0f;
    return out;
}

Mtx4 multiply(const Mtx4& a, const Mtx4& b) {
    Mtx4 out{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            f64 sum = 0.0;
            for (int k = 0; k < 4; ++k) {
                sum += static_cast<f64>(a.m[r][k]) * static_cast<f64>(b.m[k][c]);
            }
            out.m[r][c] = static_cast<f32>(sum);
        }
    }
    return out;
}

void store_column_major(const Mtx4& in, float out[16]) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c * 4 + r] = in.m[r][c];
        }
    }
}

ModResult camera_get_camera(ModContext* context, CameraInfo* outInfo) {
    auto* mod = mod_from_context(context);
    if (mod == nullptr || outInfo == nullptr || outInfo->struct_size < sizeof(CameraInfo)) {
        return MOD_INVALID_ARGUMENT;
    }

    view_class* view = dComIfGd_getView();
    if (view == nullptr) {
        return MOD_UNAVAILABLE;
    }
    // A constructed-but-never-drawn view has garbage parameters; gate on the
    // invariants camera_draw's C_MTXPerspective call requires.
    if (!(view->near_ > 0.0f) || !(view->far_ > view->near_) || !(view->fovy > 0.0f) ||
        view->fovy >= 180.0f || !(view->aspect > 0.0f))
    {
        return MOD_UNAVAILABLE;
    }

    // The GPU only ever sees the six elements GXSetProjection uploads; build
    // from exactly those so widescreen wide-zoom's in-place rewrites (off-axis
    // p02/p12 terms) are carried through.
    const f32 p00 = view->projMtx[0][0];
    const f32 p02 = view->projMtx[0][2];
    const f32 p11 = view->projMtx[1][1];
    const f32 p12 = view->projMtx[1][2];
    const f32 p22 = view->projMtx[2][2];
    const f32 p23 = view->projMtx[2][3];
    if (p00 == 0.0f || p11 == 0.0f || p23 == 0.0f) {
        return MOD_UNAVAILABLE;
    }

    // WebGPU-convention projection: the GC matrix with clip z negated (aurora's
    // generated vertex shader does out.pos.z = -out.pos.z under reversed-Z),
    // mapping near -> depth 1, far -> depth 0.
    Mtx4 proj{};
    proj.m[0][0] = p00;
    proj.m[0][2] = p02;
    proj.m[1][1] = p11;
    proj.m[1][2] = p12;
    proj.m[2][2] = -p22;
    proj.m[2][3] = -p23;
    proj.m[3][2] = -1.0f;

    // Analytic inverse of the sparse perspective form above.
    const f32 e = -p22;
    const f32 f = -p23;
    Mtx4 invProj{};
    invProj.m[0][0] = 1.0f / p00;
    invProj.m[0][3] = p02 / p00;
    invProj.m[1][1] = 1.0f / p11;
    invProj.m[1][3] = p12 / p11;
    invProj.m[2][3] = -1.0f;
    invProj.m[3][2] = 1.0f / f;
    invProj.m[3][3] = e / f;

    const Mtx4 viewMtx = expand_affine(view->viewMtx);
    const Mtx4 invViewMtx = expand_affine(view->invViewMtx);

    store_column_major(viewMtx, outInfo->view_from_world);
    store_column_major(invViewMtx, outInfo->world_from_view);
    store_column_major(proj, outInfo->proj_from_view);
    store_column_major(invProj, outInfo->view_from_proj);
    store_column_major(multiply(proj, viewMtx), outInfo->proj_from_world);
    store_column_major(multiply(invViewMtx, invProj), outInfo->world_from_proj);

    outInfo->eye[0] = view->lookat.eye.x;
    outInfo->eye[1] = view->lookat.eye.y;
    outInfo->eye[2] = view->lookat.eye.z;
    outInfo->center[0] = view->lookat.center.x;
    outInfo->center[1] = view->lookat.center.y;
    outInfo->center[2] = view->lookat.center.z;
    outInfo->up[0] = view->lookat.up.x;
    outInfo->up[1] = view->lookat.up.y;
    outInfo->up[2] = view->lookat.up.z;
    outInfo->fovy = view->fovy;
    outInfo->aspect = view->aspect;
    outInfo->near_plane = view->near_;
    outInfo->far_plane = view->far_;
    outInfo->bank = static_cast<f32>(view->bank) * (180.0f / 32768.0f);

    if (const view_port_class* viewport = dComIfGd_getViewport(); viewport != nullptr) {
        outInfo->viewport_x = viewport->x_orig;
        outInfo->viewport_y = viewport->y_orig;
        outInfo->viewport_width = viewport->width;
        outInfo->viewport_height = viewport->height;
        outInfo->viewport_near_z = viewport->near_z;
        outInfo->viewport_far_z = viewport->far_z;
    } else {
        outInfo->viewport_x = 0.0f;
        outInfo->viewport_y = 0.0f;
        outInfo->viewport_width = 640.0f;
        outInfo->viewport_height = 480.0f;
        outInfo->viewport_near_z = 0.0f;
        outInfo->viewport_far_z = 1.0f;
    }
    return MOD_OK;
}

constexpr CameraService s_cameraService{
    .header = SERVICE_HEADER(CameraService, CAMERA_SERVICE_MAJOR, CAMERA_SERVICE_MINOR),
    .get_camera = camera_get_camera,
};

}  // namespace

const CameraService& camera_service() {
    return s_cameraService;
}

}  // namespace dusk::mods::svc
