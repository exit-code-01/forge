#version 450
// Mesh vertex shader (P3.1). Per-frame data via UBO, per-draw via push
// constant — see ADR-012 for why each lives where it does.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    vec4 cameraPos;      // xyz used
    vec4 lightDirection; // xyz: surface -> light, normalized
    vec4 lightColor;     // rgb * intensity
}
frame;

layout(push_constant) uniform Push { mat4 model; }
push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;

void main() {
    vec4 world = push.model * vec4(inPosition, 1.0);
    vWorldPos = world.xyz;
    // mat3(model) is only correct for uniform scale; non-uniform needs the
    // inverse-transpose (revisit when the transform system exists, P4-ish).
    vWorldNormal = mat3(push.model) * inNormal;
    gl_Position = frame.viewProj * world;
}
