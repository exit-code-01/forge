#version 450
// Bloom upsample: 3x3 tent (9 bilinear taps) of the next-smaller mip. The
// pipeline blends ADDITIVELY onto the destination mip, which still holds
// its own downsampled content — so by mip 0 the target is the sum of every
// blur radius in the chain.

layout(location = 0) in vec2 vUv;
layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant) uniform Push {
    vec4 params; // xy: source texel size (the tent's tap spacing)
}
push;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 t = push.params.xy;
    vec3 color = texture(uSource, vUv).rgb * 4.0;
    color += texture(uSource, vUv + vec2(-t.x, 0.0)).rgb * 2.0;
    color += texture(uSource, vUv + vec2(t.x, 0.0)).rgb * 2.0;
    color += texture(uSource, vUv + vec2(0.0, -t.y)).rgb * 2.0;
    color += texture(uSource, vUv + vec2(0.0, t.y)).rgb * 2.0;
    color += texture(uSource, vUv + vec2(-t.x, -t.y)).rgb;
    color += texture(uSource, vUv + vec2(t.x, -t.y)).rgb;
    color += texture(uSource, vUv + vec2(-t.x, t.y)).rgb;
    color += texture(uSource, vUv + vec2(t.x, t.y)).rgb;
    outColor = vec4(color * (1.0 / 16.0), 1.0);
}
