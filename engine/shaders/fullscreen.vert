#version 450
// Fullscreen triangle for the post chain (bloom, composite). No vertex
// buffer: three vertices from gl_VertexIndex cover the viewport, and uv
// (0,0) lands on the framebuffer's top-left texel — the SAME orientation the
// offscreen targets were rendered in, so post passes never flip anything.

layout(location = 0) out vec2 vUv;

void main() {
    vUv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUv * 2.0 - 1.0, 0.0, 1.0);
}
