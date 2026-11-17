#version 450
// Shadow pass: rasterize the scene from the LIGHT's point of view, once per
// cascade (ADR-033). There is no fragment shader — the depth-attachment
// write IS the output. Consumes only position from the shared vertex layout.
//
// push.model is the WHOLE transform here: cascade light view-projection *
// model, multiplied on the CPU per draw and cascade, so the shader needs no
// cascade index and no per-frame data.

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform Push { mat4 model; }
push;

void main() { gl_Position = push.model * vec4(inPosition, 1.0); }
