#version 450
// Interpolated vertex color straight to the swapchain (SRGB format handles
// gamma on write).

layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main() { outColor = vec4(vColor, 1.0); }
