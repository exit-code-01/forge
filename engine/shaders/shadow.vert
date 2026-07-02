#version 450
// Shadow pass: rasterize the scene from the LIGHT's point of view. There is
// no fragment shader — the depth-attachment write IS the output. Consumes
// only position from the shared vertex layout.

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
}
frame;

layout(push_constant) uniform Push { mat4 model; }
push;

void main() { gl_Position = frame.lightViewProj * push.model * vec4(inPosition, 1.0); }
