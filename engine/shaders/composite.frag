#version 450
// Final composite: linear HDR scene (+ bloom) -> exposure -> ACES filmic
// tone map -> swapchain. The swapchain is an _SRGB format, so this shader
// writes LINEAR display-referred values and the hardware applies the sRGB
// encode — no pow(1/2.2) here, ever (that would double-gamma).

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uScene; // RGBA16F, full res
layout(set = 0, binding = 1) uniform sampler2D uBloom; // RGBA16F, half res (chain mip 0)

layout(push_constant) uniform Push {
    vec4 params; // xy: bloom texel size, z: bloom strength (0 = off), w: exposure
}
push;

layout(location = 0) out vec4 outColor;

// ACES filmic fit by Stephen Hill (RRT + ODT), sRGB primaries in and out.
// Unlike the single-curve fits it desaturates very bright saturated colors
// toward white, so a hot emissive reads as "bright", not as a flat clip.
const mat3 kAcesIn = mat3(0.59719, 0.07600, 0.02840,
                          0.35458, 0.90834, 0.13383,
                          0.04823, 0.01566, 0.83777);
const mat3 kAcesOut = mat3(1.60475, -0.10208, -0.00327,
                           -0.53108, 1.10813, -0.07276,
                           -0.07367, -0.00605, 1.07602);

vec3 rrtAndOdtFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 acesFilmic(vec3 color) {
    color = kAcesIn * color;
    color = rrtAndOdtFit(color);
    color = kAcesOut * color;
    return clamp(color, 0.0, 1.0);
}

// The fit above is darker than the curve it replaces at the same input; this
// constant re-normalizes so exposure 1 keeps mid-tones roughly where the old
// in-shader Reinhard put them (tuned against before/after captures).
const float kAcesPreScale = 1.25;

// Interleaved gradient noise (Jimenez): a FIXED screen-space pattern, so the
// dither below never shimmers frame to frame.
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    vec3 hdr = texture(uScene, vUv).rgb;

    if (push.params.z > 0.0) {
        // Last upsample of the bloom chain (half -> full res) with the same
        // 3x3 tent as bloom_up.frag: plain bilinear here shows as blockiness
        // around thin emitters.
        vec2 t = push.params.xy;
        vec3 bloom = texture(uBloom, vUv).rgb * 4.0;
        bloom += texture(uBloom, vUv + vec2(-t.x, 0.0)).rgb * 2.0;
        bloom += texture(uBloom, vUv + vec2(t.x, 0.0)).rgb * 2.0;
        bloom += texture(uBloom, vUv + vec2(0.0, -t.y)).rgb * 2.0;
        bloom += texture(uBloom, vUv + vec2(0.0, t.y)).rgb * 2.0;
        bloom += texture(uBloom, vUv + vec2(-t.x, -t.y)).rgb;
        bloom += texture(uBloom, vUv + vec2(t.x, -t.y)).rgb;
        bloom += texture(uBloom, vUv + vec2(-t.x, t.y)).rgb;
        bloom += texture(uBloom, vUv + vec2(t.x, t.y)).rgb;
        hdr += bloom * (push.params.z / 16.0);
    }

    vec3 color = acesFilmic(max(hdr, vec3(0.0)) * (push.params.w * kAcesPreScale));

    // About +-0.5 LSB of dither, scaled to follow the sRGB encode's slope:
    // wide bloom gradients in dark rooms band visibly in 8 bits without it.
    float noise = ign(gl_FragCoord.xy) - 0.5;
    color += noise * (1.0 / 255.0) * (2.0 * sqrt(max(color, vec3(0.0))) + 0.02);

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
