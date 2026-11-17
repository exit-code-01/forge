#version 450
// Bloom downsample: the 13-tap "COD: Advanced Warfare" filter (Jimenez 2014).
// Thirteen bilinear taps = five overlapping 2x2 boxes, which is what keeps a
// thin bright source (a laser beam) from popping between texels as the
// camera moves.
//
// params.z > 0 selects the PREFILTER variant used for the first pass
// (full-res HDR scene -> half-res): each box is soft-knee thresholded, then
// the boxes are combined with Karis weights (1 / (1 + luma)) so one
// ultra-bright texel cannot dominate — the anti-firefly / anti-flicker step.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant) uniform Push {
    vec4 params; // xy: source texel size, z: threshold (0 = plain downsample), w: knee
}
push;

layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Keeps what is above the threshold, with a quadratic soft knee below it so
// surfaces crossing the threshold fade in instead of snapping on.
vec3 brightPass(vec3 c) {
    // Clamp (also swallows Inf): one bad pixel would otherwise smear across
    // every mip of the chain.
    c = clamp(c, vec3(0.0), vec3(64.0));
    float threshold = push.params.z;
    float knee = push.params.w;
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, br - threshold) / max(br, 1e-4);
    return c * max(contribution, 0.0);
}

void main() {
    vec2 t = push.params.xy;
    // a . b . c
    // . j . k .
    // d . e . f
    // . l . m .
    // g . h . i
    vec3 a = texture(uSource, vUv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSource, vUv + t * vec2(0.0, -2.0)).rgb;
    vec3 c = texture(uSource, vUv + t * vec2(2.0, -2.0)).rgb;
    vec3 d = texture(uSource, vUv + t * vec2(-2.0, 0.0)).rgb;
    vec3 e = texture(uSource, vUv).rgb;
    vec3 f = texture(uSource, vUv + t * vec2(2.0, 0.0)).rgb;
    vec3 g = texture(uSource, vUv + t * vec2(-2.0, 2.0)).rgb;
    vec3 h = texture(uSource, vUv + t * vec2(0.0, 2.0)).rgb;
    vec3 i = texture(uSource, vUv + t * vec2(2.0, 2.0)).rgb;
    vec3 j = texture(uSource, vUv + t * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(uSource, vUv + t * vec2(1.0, -1.0)).rgb;
    vec3 l = texture(uSource, vUv + t * vec2(-1.0, 1.0)).rgb;
    vec3 m = texture(uSource, vUv + t * vec2(1.0, 1.0)).rgb;

    vec3 color;
    if (push.params.z > 0.0) {
        // Five boxes: center (weight 0.5) + four corners (0.125 each).
        vec3 g0 = brightPass((j + k + l + m) * 0.25);
        vec3 g1 = brightPass((a + b + d + e) * 0.25);
        vec3 g2 = brightPass((b + c + e + f) * 0.25);
        vec3 g3 = brightPass((d + e + g + h) * 0.25);
        vec3 g4 = brightPass((e + f + h + i) * 0.25);
        float w0 = 0.5 / (1.0 + luma(g0));
        float w1 = 0.125 / (1.0 + luma(g1));
        float w2 = 0.125 / (1.0 + luma(g2));
        float w3 = 0.125 / (1.0 + luma(g3));
        float w4 = 0.125 / (1.0 + luma(g4));
        color = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    } else {
        color = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 +
                (j + k + l + m) * 0.125;
    }
    outColor = vec4(color, 1.0);
}
