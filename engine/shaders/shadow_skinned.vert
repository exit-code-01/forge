#version 450
// Skinned shadow pass (ADR-030): shadow.vert plus the same linear-blend
// skinning as mesh_skinned.vert, so animated characters cast the shadow of
// the pose they are drawn in. Depth-only: no fragment shader.

layout(location = 0) in vec3 inPosition;
layout(location = 3) in uvec4 inJoints;
layout(location = 4) in vec4 inWeights;

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
    uvec4 skin;
}
push;

void main() {
    uint base = push.skin.x;
    mat4 skin = inWeights.x * palette.joints[base + inJoints.x] +
                inWeights.y * palette.joints[base + inJoints.y] +
                inWeights.z * palette.joints[base + inJoints.z] +
                inWeights.w * palette.joints[base + inJoints.w];
    gl_Position = frame.lightViewProj * push.model * skin * vec4(inPosition, 1.0);
}
