#version 450
// Cook-Torrance PBR, one directional light (P3.1). Material params are
// hardcoded constants until texturing (P3.2) feeds them from maps.
// Everything here is LINEAR; the SRGB swapchain encodes gamma on write.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUv;

layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
}
frame;

layout(set = 0, binding = 1) uniform sampler2D uAlbedo;
// Comparison sampler: each tap returns 1 (lit) or 0 (occluded), and LINEAR
// filtering on a shadow sampler gives hardware 2x2 PCF per tap for free.
layout(set = 0, binding = 2) uniform sampler2DShadow uShadowMap;

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

// 1 = fully lit, 0 = fully shadowed. 3x3 PCF over hardware-compared taps;
// depth bias happened at shadow-RENDER time (rasterizer), not here.
float shadowFactor(vec3 worldPos) {
    vec4 clip = frame.lightViewProj * vec4(worldPos, 1.0);
    vec3 ndc = clip.xyz / clip.w; // ortho light: w == 1, but stay general
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

void main() {
    // _SRGB format: the sampler hands us LINEAR albedo, no manual pow(2.2).
    vec3 albedo = texture(uAlbedo, vUv).rgb;

    vec3 n = normalize(vWorldNormal);
    vec3 v = normalize(frame.cameraPos.xyz - vWorldPos);
    vec3 l = normalize(frame.lightDirection.xyz);
    vec3 h = normalize(v + l);

    float ndotl = max(dot(n, l), 0.0);
    float ndotv = max(dot(n, v), 0.0);
    float ndoth = max(dot(n, h), 0.0);

    // Metals tint their reflection with albedo; dielectrics reflect ~4% white.
    vec3 f0 = mix(vec3(0.04), albedo, kMetallic);

    float d = distributionGGX(ndoth, kRoughness);
    float g = geometrySchlickGGX(ndotv, kRoughness) * geometrySchlickGGX(ndotl, kRoughness);
    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 0.0001);

    // Energy conservation: light is either reflected (kS=f) or refracted
    // into diffuse — and metals have no diffuse at all.
    vec3 kd = (vec3(1.0) - f) * (1.0 - kMetallic);
    vec3 direct =
        (kd * albedo / kPi + specular) * frame.lightColor.rgb * ndotl * shadowFactor(vWorldPos);

    vec3 ambient = vec3(0.03) * albedo; // shadows kill direct only; ambient survives
    vec3 color = direct + ambient;
    color = color / (color + vec3(1.0)); // Reinhard: HDR light -> displayable range

    outColor = vec4(color, 1.0);
}
