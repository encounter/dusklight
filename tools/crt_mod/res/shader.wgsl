// Keep in sync with mod.cpp DrawUniforms
struct Params {
    width: f32,
    height: f32,
    scanline_strength: f32,
    line_scale: f32,
    mask_strength: f32,
    mask_scale: f32,
    curvature: f32,
    vignette: f32,
    chroma: f32,
    bloom: f32,
    sharpness: f32,
    brightness: f32,
    mask_style: u32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@group(0) @binding(2) var<uniform> params: Params;

struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

var<private> positions: array<vec2f, 3> = array(
    vec2f(-1.0, 1.0),
    vec2f(-1.0, -3.0),
    vec2f(3.0, 1.0),
);
var<private> uvs: array<vec2f, 3> = array(
    vec2f(0.0, 0.0),
    vec2f(0.0, 2.0),
    vec2f(2.0, 0.0),
);

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VSOut {
    var out: VSOut;
    out.pos = vec4f(positions[vi], 0.0, 1.0);
    out.uv = uvs[vi];
    return out;
}

fn to_linear(color: vec3f) -> vec3f {
    return pow(max(color, vec3f(0.0)), vec3f(2.2));
}

fn to_srgb(color: vec3f) -> vec3f {
    return pow(max(color, vec3f(0.0)), vec3f(1.0 / 2.2));
}

fn inside01(uv: vec2f) -> bool {
    return uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

fn border_fade(uv: vec2f) -> f32 {
    let edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    return smoothstep(-0.0015, 0.0025, edge);
}

fn sample_scene(uv: vec2f) -> vec3f {
    if (!inside01(uv)) {
        return vec3f(0.0);
    }
    return to_linear(textureSampleLevel(src, samp, uv, 0.0).rgb);
}

fn warp_uv(uv: vec2f, amount: f32) -> vec2f {
    if (amount <= 0.0001) {
        return uv;
    }
    let centered = uv * 2.0 - vec2f(1.0);
    let warped = centered * vec2f(
        1.0 + centered.y * centered.y * amount,
        1.0 + centered.x * centered.x * amount * 1.25,
    );
    return warped * 0.5 + vec2f(0.5);
}

fn source_size() -> vec2f {
    let line_scale = max(params.line_scale, 1.0);
    return vec2f(max(params.width, 1.0), max(params.height / line_scale, 1.0));
}

fn fetch(pos: vec2f, off: vec2f) -> vec3f {
    let src_size = source_size();
    let uv = (floor(pos * src_size + off) + vec2f(0.5)) / src_size;
    return sample_scene(uv) * params.brightness;
}

fn dist(pos: vec2f) -> vec2f {
    let pixel_pos = pos * source_size();
    return -((pixel_pos - floor(pixel_pos)) - vec2f(0.5));
}

fn gaus(pos: f32, scale: f32) -> f32 {
    return exp2(scale * pow(abs(pos), 2.0));
}

fn hard_pix() -> f32 {
    return mix(-2.0, -7.0, clamp(params.sharpness, 0.0, 1.0));
}

fn hard_scan() -> f32 {
    return mix(-5.0, -16.0, clamp(params.scanline_strength, 0.0, 1.0));
}

fn horz3(pos: vec2f, off: f32) -> vec3f {
    let b = fetch(pos, vec2f(-1.0, off));
    let c = fetch(pos, vec2f(0.0, off));
    let d = fetch(pos, vec2f(1.0, off));
    let dst = dist(pos).x;
    let scale = hard_pix();
    let wb = gaus(dst - 1.0, scale);
    let wc = gaus(dst, scale);
    let wd = gaus(dst + 1.0, scale);
    return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}

fn horz5(pos: vec2f, off: f32) -> vec3f {
    let a = fetch(pos, vec2f(-2.0, off));
    let b = fetch(pos, vec2f(-1.0, off));
    let c = fetch(pos, vec2f(0.0, off));
    let d = fetch(pos, vec2f(1.0, off));
    let e = fetch(pos, vec2f(2.0, off));
    let dst = dist(pos).x;
    let scale = hard_pix();
    let wa = gaus(dst - 2.0, scale);
    let wb = gaus(dst - 1.0, scale);
    let wc = gaus(dst, scale);
    let wd = gaus(dst + 1.0, scale);
    let we = gaus(dst + 2.0, scale);
    return (a * wa + b * wb + c * wc + d * wd + e * we) / (wa + wb + wc + wd + we);
}

fn horz7(pos: vec2f, off: f32) -> vec3f {
    let a = fetch(pos, vec2f(-3.0, off));
    let b = fetch(pos, vec2f(-2.0, off));
    let c = fetch(pos, vec2f(-1.0, off));
    let d = fetch(pos, vec2f(0.0, off));
    let e = fetch(pos, vec2f(1.0, off));
    let f = fetch(pos, vec2f(2.0, off));
    let g = fetch(pos, vec2f(3.0, off));
    let dst = dist(pos).x;
    let scale = mix(-1.0, -3.0, clamp(params.bloom, 0.0, 1.0));
    let wa = gaus(dst - 3.0, scale);
    let wb = gaus(dst - 2.0, scale);
    let wc = gaus(dst - 1.0, scale);
    let wd = gaus(dst, scale);
    let we = gaus(dst + 1.0, scale);
    let wf = gaus(dst + 2.0, scale);
    let wg = gaus(dst + 3.0, scale);
    return (a * wa + b * wb + c * wc + d * wd + e * we + f * wf + g * wg) /
        (wa + wb + wc + wd + we + wf + wg);
}

fn scan(pos: vec2f, off: f32) -> f32 {
    return gaus(dist(pos).y + off, hard_scan());
}

fn bloom_scan(pos: vec2f, off: f32) -> f32 {
    return gaus(dist(pos).y + off, mix(-1.5, -4.0, clamp(params.bloom, 0.0, 1.0)));
}

fn tri(pos: vec2f) -> vec3f {
    let a = horz3(pos, -1.0);
    let b = horz5(pos, 0.0);
    let c = horz3(pos, 1.0);
    let wa = scan(pos, -1.0);
    let wb = scan(pos, 0.0);
    let wc = scan(pos, 1.0);
    return a * wa + b * wb + c * wc;
}

fn bloom(pos: vec2f) -> vec3f {
    let a = horz5(pos, -2.0);
    let b = horz7(pos, -1.0);
    let c = horz7(pos, 0.0);
    let d = horz7(pos, 1.0);
    let e = horz5(pos, 2.0);
    let wa = bloom_scan(pos, -2.0);
    let wb = bloom_scan(pos, -1.0);
    let wc = bloom_scan(pos, 0.0);
    let wd = bloom_scan(pos, 1.0);
    let we = bloom_scan(pos, 2.0);
    return (a * wa + b * wb + c * wc + d * wd + e * we) / max(wa + wb + wc + wd + we, 0.001);
}

fn chroma_sample(uv: vec2f, centered: vec2f) -> vec3f {
    let chroma_offset = centered * dot(centered, centered) * params.chroma * 0.10;
    return vec3f(
        sample_scene(uv + chroma_offset).r,
        sample_scene(uv).g,
        sample_scene(uv - chroma_offset).b,
    ) * params.brightness;
}

fn primary_mask(primary: u32, dark: f32, light: f32) -> vec3f {
    var mask = vec3f(dark);
    switch primary {
        case 0u: {
            mask.r = light;
        }
        case 1u: {
            mask.g = light;
        }
        default: {
            mask.b = light;
        }
    }
    return mask;
}

fn crt_mask(pos: vec2f, style: u32, strength: f32) -> vec3f {
    let amount = clamp(strength, 0.0, 1.0);
    if (style == 0u || amount <= 0.001) {
        return vec3f(1.0);
    }

    let dark = mix(1.0, 0.25, amount);
    let light = mix(1.0, 1.55, amount);
    let px = max(pos.x, 0.0);
    let py = max(pos.y, 0.0);

    switch style {
        case 1u: {
            return primary_mask(u32(floor(px)) % 3u, dark, light);
        }
        case 2u: {
            let row = u32(floor(py)) % 4u;
            let primary = (u32(floor(px / 2.0)) + row / 2u) % 3u;
            var mask = primary_mask(primary, dark, light);
            if (row == 3u) {
                mask *= 1.0 - 0.20 * amount;
            }
            return mask;
        }
        default: {
            let primary = (u32(floor(px)) + (u32(floor(py)) % 2u)) % 3u;
            let cell = fract(pos) - vec2f(0.5);
            let dot_shape = 1.0 - smoothstep(0.45, 0.78, length(cell) * 2.0);
            let dot_dim = mix(1.0 - 0.35 * amount, 1.0, dot_shape);
            return primary_mask(primary, dark, light) * dot_dim;
        }
    }
}

@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    let center = in.uv - vec2f(0.5);
    let uv = warp_uv(in.uv, params.curvature);
    let edge = border_fade(uv);
    if (edge <= 0.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let base = chroma_sample(uv, center);
    var color = mix(base, tri(uv), clamp(params.scanline_strength, 0.0, 1.0));
    color += bloom(uv) * params.bloom * 0.25;
    let screen_pos = in.uv * vec2f(params.width, params.height);
    color *= crt_mask(screen_pos / max(params.mask_scale, 1.0), params.mask_style,
        params.mask_strength);

    let vignette_center = center * vec2f(1.12, 1.32);
    let vignette = clamp(1.0 - params.vignette * dot(vignette_center, vignette_center) * 1.8,
        0.0, 1.0);
    color *= vignette * edge;

    return vec4f(clamp(to_srgb(color), vec3f(0.0), vec3f(1.0)), 1.0);
}
