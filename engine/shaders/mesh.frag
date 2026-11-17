#version 450
// Cook-Torrance PBR: one shadowed directional key light (3 shadow cascades,
// ADR-033) + up to 16 unshadowed point lights + hemisphere ambient +
// exponential fog + per-draw emissive. Roughness and metallic are per draw
// (push constant w channels, ADR-033); there is no material system yet.
//
// Everything here is LINEAR and the output is scene-referred HDR radiance
// (RGBA16F target): exposure, tone mapping and bloom happen in the post
// chain (composite.frag), never here.

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUv;

const int kMaxPointLights = 16; // must match Renderer::kMaxPointLights
const int kCascades = 3;        // must match kShadowCascades in Renderer.cpp

// Mirrors FrameUbo in Renderer.cpp (std140). The vertex shaders declare only
// the leading members they need — the prefix layout is identical.
layout(set = 0, binding = 0) uniform FrameData {
    mat4 viewProj;
    mat4 lightViewProj;  // cascade 0 (kept for the shared prefix layout)
    vec4 cameraPos;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 fogColor;       // rgb: linear fog color, a: density per meter (0 = off)
    vec4 ambientSky;     // rgb: irradiance-ish tint for normals pointing up
    vec4 ambientGround;  // rgb: same, for normals pointing down
    vec4 counts;         // x: number of active point lights
    vec4 pointPosRadius[kMaxPointLights]; // xyz: world position, w: radius
    vec4 pointColor[kMaxPointLights];     // rgb * HDR intensity
    // Sky tail (ADR-030): read by sky.frag only; declared here for layout.
    mat4 invViewProj;
    vec4 skyZenith;
    vec4 skyHorizon;
    vec4 skyGround;
    vec4 skySun;
    vec4 skyParams;
    // Cascade tail (ADR-033): read by this shader only.
    mat4 cascadeViewProj[kCascades]; // world -> cascade light clip (xy -1..1, z 0..1)
    vec4 cascadeTexel;  // xyz: world size of one shadow texel per cascade
    vec4 shadowParams;  // x: fade start (m), y: fade end (m), z: blend band (uv), w: normal offset (texels)
}
frame;

// One layer per cascade. Comparison sampler: each tap returns 1 (lit) or 0
// (occluded), and LINEAR filtering gives hardware 2x2 PCF per tap for free.
layout(set = 0, binding = 1) uniform sampler2DArrayShadow uShadowMap;

// Set 1 = per-MATERIAL (ADR-017): rebinds per draw, unlike set 0.
layout(set = 1, binding = 0) uniform sampler2D uAlbedo;

// Per-draw data (ADR-012): the vertex stage reads only `model`.
layout(push_constant) uniform Push {
    mat4 model;
    vec4 emissive; // rgb: linear HDR radiance added after lighting, a: roughness
    vec4 tint;     // rgb: albedo multiplier, a: metallic
}
push;

layout(location = 0) out vec4 outColor;

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
vec3 shade(vec3 n, vec3 v, vec3 l, vec3 albedo, vec3 f0, float roughness, float metallic,
           vec3 radiance) {
    float ndotl = max(dot(n, l), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }
    vec3 h = normalize(v + l);
    float ndotv = max(dot(n, v), 0.0);
    float ndoth = max(dot(n, h), 0.0);

    float d = distributionGGX(ndoth, roughness);
    float g = geometrySchlickGGX(ndotv, roughness) * geometrySchlickGGX(ndotl, roughness);
    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 specular = (d * g * f) / max(4.0 * ndotv * ndotl, 0.0001);

    // Energy conservation: light is either reflected (kS=f) or refracted
    // into diffuse — and metals have no diffuse at all.
    vec3 kd = (vec3(1.0) - f) * (1.0 - metallic);
    return (kd * albedo / kPi + specular) * radiance * ndotl;
}

// Cascade c's shadow-map coordinate for a world position: uv in 0..1, z =
// the light depth to compare. The position is first pushed along the normal
// by a fraction of THAT cascade's texel (normal-offset bias): coarse far
// cascades need proportionally more, which a fixed epsilon cannot give.
vec3 cascadeCoord(int c, vec3 worldPos, vec3 n) {
    vec3 p = worldPos + n * (frame.cascadeTexel[c] * frame.shadowParams.w);
    vec4 clip = frame.cascadeViewProj[c] * vec4(p, 1.0); // ortho: w == 1
    return vec3(clip.xy * 0.5 + 0.5, clip.z);
}

// 3x3 PCF over hardware-compared taps in one cascade layer. Slope-scaled
// depth bias happened at shadow-RENDER time (rasterizer), per cascade.
float cascadePcf(int c, vec3 coord) {
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            lit += texture(uShadowMap, vec4(coord.xy + vec2(x, y) * texel, float(c), coord.z));
        }
    }
    return lit / 9.0;
}

// How far inside cascade c's map a coordinate is: distance to the nearest
// map edge in uv, or -1 when the depth falls outside the cascade's slab.
float cascadeInset(vec3 coord) {
    if (coord.z <= 0.0 || coord.z >= 1.0) {
        return -1.0;
    }
    vec2 e = min(coord.xy, 1.0 - coord.xy);
    return min(e.x, e.y);
}

// 1 = fully lit, 0 = fully shadowed. Picks the FINEST cascade whose map
// holds the point (map-based selection: the stabilised cascades are spheres,
// so cascade 0 reaches well past its split), cross-fades into the next one
// over a band at the map edge, and fades to lit with view distance.
float shadowFactor(vec3 worldPos, vec3 n, float viewDist) {
    float fadeEnd = frame.shadowParams.y;
    if (viewDist >= fadeEnd) {
        return 1.0;
    }
    // Stay 2 texels inside the map so the PCF footprint never leaves it.
    float margin = 2.0 / float(textureSize(uShadowMap, 0).x);
    float band = frame.shadowParams.z;
    float lit = 1.0;
    for (int c = 0; c < kCascades; ++c) {
        vec3 coord = cascadeCoord(c, worldPos, n);
        float inset = cascadeInset(coord);
        if (inset < margin) {
            continue; // not (safely) in this cascade: try the next, coarser one
        }
        lit = cascadePcf(c, coord);
        if (inset < band && c + 1 < kCascades) {
            vec3 next = cascadeCoord(c + 1, worldPos, n);
            if (cascadeInset(next) >= margin) {
                lit = mix(cascadePcf(c + 1, next), lit, smoothstep(margin, band, inset));
            }
        }
        break;
    }
    return mix(lit, 1.0, smoothstep(frame.shadowParams.x, fadeEnd, viewDist));
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

    // Per-draw surface (ADR-033): defaults 0.35 / 0.15 are the old constants.
    // The roughness floor keeps the GGX lobe finite.
    float roughness = clamp(push.emissive.a, 0.045, 1.0);
    float metallic = clamp(push.tint.a, 0.0, 1.0);

    // Metals tint their reflection with albedo; dielectrics reflect ~4% white.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // Key light: the only shadow caster. Faces turned away from it skip the
    // cascade lookups (9-18 compare taps) — they get nothing to shadow.
    vec3 color = shade(n, v, normalize(frame.lightDirection.xyz), albedo, f0, roughness,
                       metallic, frame.lightColor.rgb);
    if (color.r + color.g + color.b > 0.0) {
        color *= shadowFactor(vWorldPos, n, viewDist);
    }

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
        color += shade(n, v, toLight / max(dist, 1e-5), albedo, f0, roughness, metallic,
                       radiance);
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
