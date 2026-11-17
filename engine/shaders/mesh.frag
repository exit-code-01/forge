#version 450
// Cook-Torrance PBR: one shadowed directional key light + up to 16 unshadowed
// point lights + hemisphere ambient + exponential fog + per-draw emissive.
// Material params are hardcoded constants until a material system feeds them.
//
// Everything here is LINEAR and the output is scene-referred HDR radiance
// (RGBA16F target): exposure, tone mapping and bloom happen in the post
// chain (composite.frag), never here.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUv;

const int kMaxPointLights = 16; // must match Renderer::kMaxPointLights

// Mirrors FrameUbo in Renderer.cpp (std140). The vertex shaders declare only
// the leading members they need — the prefix layout is identical.
layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 fogColor;       // rgb: linear fog color, a: density per meter (0 = off)
    vec4 ambientSky;     // rgb: irradiance-ish tint for normals pointing up
    vec4 ambientGround;  // rgb: same, for normals pointing down
    vec4 counts;         // x: number of active point lights
    vec4 pointPosRadius[kMaxPointLights]; // xyz: world position, w: radius
    vec4 pointColor[kMaxPointLights];     // rgb * HDR intensity
}
frame;

// Comparison sampler: each tap returns 1 (lit) or 0 (occluded), and LINEAR
// filtering on a shadow sampler gives hardware 2x2 PCF per tap for free.
layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;

// Set 1 = per-MATERIAL (ADR-017): rebinds per draw, unlike set 0.
layout(set = 1, binding = 0) uniform sampler2D uAlbedo;

// Per-draw data (ADR-012): the vertex stage reads only `model`.
layout(push_constant) uniform Push {
    mat4 model;
    vec4 emissive; // rgb: linear HDR radiance added after lighting
    vec4 tint;     // rgb: albedo multiplier
}
push;

layout(location = 0) out vec4 outColor;

const float kMetallic = 0.15;
const float kRoughness = 0.35;
const float kPi = 3.14159265359;

// GGX/Trowbridge-Reitz: how many microfacets point exactly at H.
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness; // Disney remap: perceptually linear sliders
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

// Smith height-correlated-ish Schlick-GGX: microfacet self-shadowing.
float geometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Fresnel-Schlick: reflectance grows toward grazing angles.
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// The one BRDF every light goes through. Returns outgoing radiance for
// incoming `radiance` from direction l (already includes N.L).
vec3 shade(vec3 n, vec3 v, vec3 l, vec3 albedo, vec3 f0, vec3 radiance) {
    float ndotl = max(dot(n, l), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }
    vec3 h = normalize(v + l);
    float ndotv = max(dot(n, v), 0.0);
    float ndoth = max(dot(n, h), 0.0);

    float d = distributionGGX(ndoth, kRoughness);
    float g = geometrySchlickGGX(ndotv, kRoughness) * geometrySchlickGGX(ndotl, kRoughness);
    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 0.0001);

    // Energy conservation: light is either reflected (kS=f) or refracted
    // into diffuse — and metals have no diffuse at all.
    vec3 kd = (vec3(1.0) - f) * (1.0 - kMetallic);
    return (kd * albedo / kPi + specular) * radiance * ndotl;
}

// 1 = fully lit, 0 = fully shadowed. 3x3 PCF over hardware-compared taps;
// depth bias happened at shadow-RENDER time (rasterizer), not here.
float shadowFactor(vec3 worldPos) {
    vec4 clip = frame.lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / clip.w; // ortho light: w == 1, but stay general
    if (ndc.z >= 1.0) {
        return 1.0; // beyond the light's far plane: no data means lit
    }
    vec2 uv = ndc.xy * 0.5 + 0.5; // same convention that rendered the map
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(uShadowMap, vec3(uv + vec2(x, y) * texel, ndc.z));
        }
    }
    return lit / 9.0;
}

// Inverse-square falloff, windowed (Karis 2013) so it reaches EXACTLY zero at
// the light's radius with zero slope — no visible cutoff ring, and the +1 in
// the denominator keeps it finite when a surface touches the light.
float pointAttenuation(float dist, float radius) {
    float x = dist / max(radius, 1e-4);
    float x2 = x * x;
    float window = clamp(1.0 - x2 * x2, 0.0, 1.0);
    return (window * window) / (dist * dist + 1.0);
}

void main() {
    // _SRGB format: the sampler hands us LINEAR albedo, no manual pow(2.2).
    vec3 albedo = texture(uAlbedo, vUv).rgb * push.tint.rgb;

    vec3 n = normalize(vWorldNormal);
    vec3 toCamera = frame.cameraPos.xyz - vWorldPos;
    float viewDist = length(toCamera);
    vec3 v = toCamera / max(viewDist, 1e-5);

    // Metals tint their reflection with albedo; dielectrics reflect ~4% white.
    vec3 f0 = mix(vec3(0.04), albedo, kMetallic);

    // Key light: the only shadow caster.
    vec3 color = shade(n, v, normalize(frame.lightDirection.xyz), albedo, f0,
                       frame.lightColor.rgb) *
                 shadowFactor(vWorldPos);

    // Local lights: same BRDF, no shadows.
    int pointCount = min(int(frame.counts.x), kMaxPointLights);
    for (int i = 0; i < pointCount; ++i) {
        vec3 toLight = frame.pointPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = frame.pointPosRadius[i].w;
        if (dist >= radius) {
            continue;
        }
        vec3 radiance = frame.pointColor[i].rgb * pointAttenuation(dist, radius);
        color += shade(n, v, toLight / max(dist, 1e-5), albedo, f0, radiance);
    }

    // Hemisphere ambient: sky tint from above, ground bounce from below.
    // Shadows kill direct light only, so this is what gives shadowed
    // interiors their form.
    vec3 ambient = mix(frame.ambientGround.rgb, frame.ambientSky.rgb, n.y * 0.5 + 0.5);
    color += ambient * albedo;

    // Exponential distance fog, in linear HDR (before any tone mapping).
    float density = frame.fogColor.a;
    float transmittance = exp(-density * viewDist);
    color = mix(frame.fogColor.rgb, color, transmittance);

    // Emissive is added AFTER lighting, shadows and fog. It only fades at
    // half the fog rate: emitters are the game's colour language and must
    // read through haze, while still sinking into it at long range.
    color += push.emissive.rgb * exp(-0.5 * density * viewDist);

    outColor = vec4(color, 1.0);
}
