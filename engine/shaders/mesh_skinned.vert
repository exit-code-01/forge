#version 450
// Skinned mesh vertex shader (ADR-030): mesh.vert plus linear-blend skinning.
// The palette is a per-frame storage buffer (set 0, binding 2) holding every
// animated entity's model-space joint matrices back to back; this draw's
// slice starts at push.skin.x. Feeds the unchanged mesh.frag.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in uvec4 inJoints;  // second vertex stream (SkinVertex)
layout(location = 4) in vec4 inWeights;  // sum to 1

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
}
frame;

layout(std430, set = 0, binding = 2) readonly buffer JointPalette { mat4 joints[]; }
palette;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 emissive;
    vec4 tint;
    uvec4 skin; // x: first joint of this draw's palette slice
}
push;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUv;

void main() {
    uint base = push.skin.x;
    mat4 skin = inWeights.x * palette.joints[base + inJoints.x] +
                inWeights.y * palette.joints[base + inJoints.y] +
                inWeights.z * palette.joints[base + inJoints.z] +
                inWeights.w * palette.joints[base + inJoints.w];
    mat4 world = push.model * skin;
    vec4 worldPos = world * vec4(inPosition, 1.0);
    vWorldPos = worldPos.xyz;
    // Same uniform-scale assumption as mesh.vert; mesh.frag normalizes.
    vWorldNormal = mat3(world) * inNormal;
    vUv = inUv;
    gl_Position = frame.viewProj * worldPos;
}
