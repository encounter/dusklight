// Deferred shadow composite: reconstructs the world position of every scene pixel from the
// depth snapshot (CameraService matrices), transforms it into the light's clip space, and
// PCF-compares against the shadow map rendered earlier this frame. Drawn as a fullscreen
// triangle with multiply blending (srcFactor = Dst, dstFactor = Zero) before HUD draws.
//
// Depth conventions (both reversed-Z): the scene snapshot has 1.0 at the camera near plane;
// the shadow map — rendered through the game's GX pipeline with a GC-convention light matrix —
// stores -clip.z, i.e. 1.0 nearest to the light and 0.0 at the light frustum far plane.
// A larger stored value therefore means "closer to the light".
//
// The optional contact-shadow raymarch follows Panos Karabelas' screen-space shadows
// (https://panoskarabelas.com/blog/posts/screen_space_shadows/, MIT via Spartan Engine):
// march from the pixel toward the light in view space and mark occlusion when the ray dips
// behind the depth buffer within a thickness threshold.

struct Uniforms {
    world_from_proj: mat4x4f, // scene depth unproject (camera)
    view_from_proj: mat4x4f,  // scene depth -> view space (contact shadows)
    proj_from_view: mat4x4f,  // view -> clip (contact shadows re-projection)
    light_vp: mat4x4f,        // world -> GC light clip (ortho, w stays 1)
    light_dir_view: vec3f,    // direction *toward* the light, view space, normalized
    bias: f32,                // shadow-map depth bias (reversed-depth units)
    size: vec2f,              // shadow map size in texels
    inv_size: vec2f,
    strength: f32,            // final darkening amount, horizon fade baked in
    pcf_taps: f32,            // 0 = single tap, 1 = 3x3, 2 = 5x5
    contact_enabled: f32,
    contact_thickness: f32,   // view-space thickness threshold
    contact_length: f32,      // view-space march distance
    debug_mode: u32,          // 0 = composite, 1 = show map, 2 = show factor
    _pad0: f32,
    _pad1: f32,
}

@group(0) @binding(0) var scene_depth: texture_2d<f32>;
@group(0) @binding(1) var shadow_map: texture_2d<f32>;
@group(0) @binding(2) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) index: u32) -> VertexOutput {
    var out: VertexOutput;
    let uv = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    out.position = vec4f(uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), 0.0, 1.0);
    out.uv = uv;
    return out;
}

fn load_shadow(texel: vec2<i32>) -> f32 {
    let clamped = clamp(texel, vec2<i32>(0i), vec2<i32>(uniforms.size) - 1i);
    return textureLoad(shadow_map, clamped, 0i).r;
}

// Returns 1.0 when the pixel at light-space depth `receiver` is shadowed by the map texel.
fn shadow_test(texel: vec2<i32>, receiver: f32) -> f32 {
    // Reversed depth: a larger stored value is closer to the light, i.e. an occluder.
    return select(0.0, 1.0, load_shadow(texel) > receiver + uniforms.bias);
}

// Bilinearly weighted comparison (what a hardware comparison sampler would do): filter the
// four *comparison results*, never the depths themselves. This is what turns per-texel
// staircases into smooth penumbra edges.
fn shadow_compare_bilinear(light_uv: vec2f, receiver: f32) -> f32 {
    let coordinates = light_uv * uniforms.size - 0.5;
    let base = floor(coordinates);
    let fraction = coordinates - base;
    let texel = vec2<i32>(base);
    let s00 = shadow_test(texel, receiver);
    let s10 = shadow_test(texel + vec2<i32>(1i, 0i), receiver);
    let s01 = shadow_test(texel + vec2<i32>(0i, 1i), receiver);
    let s11 = shadow_test(texel + vec2<i32>(1i, 1i), receiver);
    let top = mix(s00, s10, fraction.x);
    let bottom = mix(s01, s11, fraction.x);
    return mix(top, bottom, fraction.y);
}

fn sample_shadow_pcf(light_uv: vec2f, receiver: f32) -> f32 {
    let radius = i32(uniforms.pcf_taps);
    var sum = 0.0;
    var count = 0.0;
    for (var y = -radius; y <= radius; y += 1i) {
        for (var x = -radius; x <= radius; x += 1i) {
            let offset = vec2f(f32(x), f32(y)) * uniforms.inv_size;
            sum += shadow_compare_bilinear(light_uv + offset, receiver);
            count += 1.0;
        }
    }
    return sum / count;
}

fn scene_depth_at(uv: vec2f) -> f32 {
    let size = vec2<i32>(textureDimensions(scene_depth));
    let texel = clamp(vec2<i32>(uv * vec2f(size)), vec2<i32>(0i), size - 1i);
    return textureLoad(scene_depth, texel, 0i).r;
}

fn view_position(uv: vec2f, depth: f32) -> vec3f {
    let ndc = vec4f(uv.x * 2.0 - 1.0, 1.0 - 2.0 * uv.y, depth, 1.0);
    let position = uniforms.view_from_proj * ndc;
    return position.xyz / position.w;
}

// Interleaved gradient noise (Jimenez); fixed per-pixel dither, no temporal rotation.
fn ign(pixel: vec2f) -> f32 {
    return fract(52.9829189 * fract(dot(pixel, vec2f(0.06711056, 0.00583715))));
}

// Screen-space contact shadows: march toward the light in view space; occluded when the ray
// passes behind the depth buffer by less than the thickness threshold. Faded out with view
// distance: position reconstruction error grows with distance while the thickness threshold
// is fixed, so far surfaces (and anything translucent composited over them — clouds, fog)
// would otherwise pick up dithered false occlusion. Contact shadows are a near-field effect.
fn contact_shadow_fade(view_distance: f32) -> f32 {
    return saturate(1.0 - (view_distance - 3000.0) / 5000.0);
}

fn contact_shadow(origin: vec3f, pixel: vec2f) -> f32 {
    let steps = 24;
    let step_vec = uniforms.light_dir_view * (uniforms.contact_length / f32(steps));
    var ray = origin + step_vec * ign(pixel);
    for (var i = 0; i < steps; i += 1) {
        ray += step_vec;
        // Project the ray position back to screen space.
        let clip = uniforms.proj_from_view * vec4f(ray, 1.0);
        if clip.w <= 0.0 {
            break;
        }
        let ray_ndc = clip.xyz / clip.w;
        let ray_uv = vec2f(0.5 + 0.5 * ray_ndc.x, 0.5 - 0.5 * ray_ndc.y);
        if any(ray_uv < vec2f(0.0)) || any(ray_uv > vec2f(1.0)) {
            break;
        }
        let scene = scene_depth_at(ray_uv);
        if scene <= 0.0 {
            continue;
        }
        // Compare in view space: positive delta = the ray is behind the scene surface.
        let scene_z = view_position(ray_uv, scene).z;
        let delta = scene_z - ray.z; // view space looks down -z; larger z = closer
        if delta > 0.0 && delta < uniforms.contact_thickness {
            return 1.0;
        }
    }
    return 0.0;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let depth = scene_depth_at(in.uv);
    if depth <= 0.0 {
        // Sky / cleared pixels receive no shadow.
        if uniforms.debug_mode == 1u {
            return vec4f(load_shadow(vec2<i32>(in.uv * uniforms.size)), 0.0, 0.0, 1.0);
        }
        return vec4f(1.0);
    }

    if uniforms.debug_mode == 1u {
        let value = load_shadow(vec2<i32>(in.uv * uniforms.size));
        return vec4f(value, value, value, 1.0);
    }

    let ndc = vec4f(in.uv.x * 2.0 - 1.0, 1.0 - 2.0 * in.uv.y, depth, 1.0);
    let world4 = uniforms.world_from_proj * ndc;
    let world = world4.xyz / world4.w;

    let light_clip = uniforms.light_vp * vec4f(world, 1.0);
    let receiver = -light_clip.z; // reversed light depth, 1 = nearest to the light
    let light_uv = vec2f(0.5 + 0.5 * light_clip.x, 0.5 - 0.5 * light_clip.y);

    var occlusion = 0.0;
    if all(light_uv >= vec2f(0.0)) && all(light_uv <= vec2f(1.0)) && receiver > 0.0 &&
        receiver <= 1.0
    {
        occlusion = sample_shadow_pcf(light_uv, receiver);
    }

    if uniforms.contact_enabled != 0.0 && occlusion < 1.0 {
        let origin = view_position(in.uv, depth);
        let fade = contact_shadow_fade(-origin.z);
        if fade > 0.0 {
            occlusion = max(occlusion, fade * contact_shadow(origin, in.position.xy));
        }
    }

    let value = 1.0 - uniforms.strength * occlusion;
    return vec4f(value, value, value, 1.0);
}
