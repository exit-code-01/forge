#version 450
// First triangle: geometry lives IN the shader, indexed by gl_VertexIndex.
// No vertex buffers until P3 — this is the smallest thing that can render.

layout(location = 0) out vec3 vColor;

vec2 positions[3] = vec2[](vec2(0.0, -0.6), vec2(0.6, 0.6), vec2(-0.6, 0.6));
vec3 colors[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    vColor = colors[gl_VertexIndex];
}
