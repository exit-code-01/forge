#version 450
// Sky pass (ADR-030): drawn first into the HDR target, behind everything,
// by the fullscreen triangle (no depth test/write). Rooms have no ceilings,
// so this is what the open tops show. Procedural gradient (zenith / horizon
// / ground) plus a sun disc and halo on the key light's direction, or an
// optional equirectangular panorama; then the scene's fog, as if the sky
// stood at the far plane — the horizon and the farthest geometry meet at
// the same colour. Output is linear HDR like mesh.frag: exposure, bloom and
// ACES happen in the post chain.

layout(location = 0) in vec2 vUv;

const int kMaxPointLights = 16; // must match Renderer::kMaxPointLights

// Mirrors FrameUbo in Renderer.cpp (std140), including the sky tail that
// only this shader reads.
layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 fogColor; // rgb: linear fog colour, a: density per metre
    vec4 ambientSky;
    vec4 ambientGround;
    vec4 counts;
    vec4 pointPosRadius[kMaxPointLights];
    vec4 pointColor[kMaxPointLights];
    mat4 invViewProj;  // clip -> world (sky view rays)
    vec4 skyZenith;    // rgb
    vec4 skyHorizon;   // rgb
    vec4 skyGround;    // rgb
    vec4 skySun;       // rgb: disc radiance, a: cos(angular radius) (> 1 = no disc)
    vec4 skyParams;    // x: panorama on, y: panorama intensity, z: fog distance, w: halo
}
frame;

// Set 1 = the panorama's material set (white when there is none).
layout(set = 1, binding = 0) uniform sampler2D uPanorama;

layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265359;

void main() {
    // Ray through this pixel: unproject the far plane (Vulkan depth 0..1).
    vec4 far = frame.invViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - frame.cameraPos.xyz);

    vec3 sky;
    if (frame.skyParams.x > 0.5) {
        // Equirectangular: u = azimuth (0.5 = -Z, the rooms' "forward"),
        // v = 0 at the zenith (image row 0 = top). The u seam at the back
        // would pick the smallest mip in a 2x2 quad: use whichever of u /
        // fract(u + 0.5) has the smaller screen derivative.
        float yaw = frame.skyHorizon.a; // panorama yaw, radians
        vec2 uv = vec2(atan(dir.x, -dir.z) / (2.0 * kPi) + 0.5 + yaw / (2.0 * kPi),
                       acos(clamp(dir.y, -1.0, 1.0)) / kPi);
        vec2 dx = dFdx(uv);
        vec2 dy = dFdy(uv);
        float u2 = fract(uv.x + 0.5);
        float dx2 = dFdx(u2);
        float dy2 = dFdy(u2);
        if (abs(dx2) + abs(dy2) < abs(dx.x) + abs(dy.x)) {
            dx.x = dx2;
            dy.x = dy2;
        }
        sky = textureGrad(uPanorama, uv, dx, dy).rgb * frame.skyParams.y;
    } else {
        // Gradient: horizon -> zenith above, a quick fall to ground below.
        float up = max(dir.y, 0.0);
        float down = max(-dir.y, 0.0);
        sky = mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, sqrt(up));
        sky = mix(sky, frame.skyGround.rgb, smoothstep(0.0, 0.25, down));
    }

    // Sun on the key light's direction: a soft-edged disc plus a faint halo,
    // both setting behind the horizon.
    vec3 toSun = normalize(frame.lightDirection.xyz);
    float c = dot(dir, toSun);
    float cosRadius = frame.skySun.a;
    float edge = max((1.0 - cosRadius) * 0.25, 1e-6); // soft rim, ~1/8 of the radius
    float disc = cosRadius < 1.0 ? smoothstep(cosRadius - edge, cosRadius + edge, c) : 0.0;
    float halo = frame.skyParams.w * pow(max(c, 0.0), 24.0);
    sky += frame.skySun.rgb * (disc + halo) * smoothstep(-0.03, 0.02, dir.y);

    // The scene's fog, as at the far plane.
    float transmittance = exp(-frame.fogColor.a * frame.skyParams.z);
    sky = mix(frame.fogColor.rgb, sky, transmittance);

    outColor = vec4(sky, 1.0);
}
