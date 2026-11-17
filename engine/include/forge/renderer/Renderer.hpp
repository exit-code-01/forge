// engine/include/forge/renderer/Renderer.hpp
//
// Owns the full chain that puts pixels on a Window: surface -> device ->
// swapchain -> depth buffer -> pipeline -> per-frame commands & sync. pimpl
// on purpose — not one Vulkan type reaches consumers, and one pointer hop
// per drawFrame() is free at frame granularity.
//
// Lifetimes (enforce by declaration order in the app): Window and
// VulkanContext MUST outlive the Renderer. drawFrame() is main-thread only
// for now — it queries the window's framebuffer size through GLFW.

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace forge {

class Window;
class VulkanContext;
class Renderer;

// Engine-internal seam (EditorUi); types are completed only inside the
// engine's src/ tree — see src/renderer/RendererInternal.hpp.
namespace internal {
struct UiRenderHook;
struct RendererVkInfo;
RendererVkInfo queryVkInfo(const Renderer& renderer);
} // namespace internal

// Value-type camera: the app owns WHERE it looks, the renderer owns HOW that
// becomes clip space (aspect from the live swapchain, Vulkan Y-flip, 0..1
// depth). Grows into a scene-graph citizen at P7; stays a dumb struct until.
struct Camera {
    glm::vec3 position{2.0f, 1.5f, 2.5f};
    glm::vec3 target{0.0f};
    float fovYDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

// P3 vertex: position + normal + uv. Tangents arrive with normal mapping —
// the layout is versioned by the engine, not the app.
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Skinning stream (ADR-030): a SECOND vertex buffer beside Vertex, so static
// meshes keep their layout. Up to 4 influences; weights sum to 1. Joint
// indices are relative to the draw's palette slice (DrawItem::jointOffset).
struct SkinVertex {
    uint16_t joints[4] = {0, 0, 0, 0};
    glm::vec4 weights{1.0f, 0.0f, 0.0f, 0.0f};
};

// Opaque GPU-resource handles (indices into renderer-owned tables).
struct MeshHandle {
    uint32_t value = 0;
};
struct TextureHandle {
    uint32_t value = 0; // 0 is always the built-in checker (ADR-013)
};

// One draw: THIS mesh, THIS texture, THERE. The scene is a span of these —
// a real scene graph is P7's business; the renderer only needs the list.
struct DrawItem {
    MeshHandle mesh;
    TextureHandle texture;
    glm::mat4 model{1.0f};
    // Emissive (VAULT upgrade pass): linear HDR radiance ADDED after lighting
    // — light strips, lasers, SPARK's eye. Values > 1 bloom. Zero = off.
    glm::vec3 emissive{0.0f};
    // Albedo multiplier (linear rgb). White = the texture as authored.
    glm::vec3 tint{1.0f};
    // Skinned meshes (ADR-030): first matrix of this draw's slice of the
    // frame's joint palette (drawFrame's `joints`). kNoJoints — or a mesh
    // without a skin stream — draws the plain, unskinned path.
    static constexpr uint32_t kNoJoints = 0xFFFFFFFFu;
    uint32_t jointOffset = kNoJoints;
};

// A local omni light: inverse-square-ish falloff windowed to zero at `radius`.
// color is linear rgb * HDR intensity. No shadows (the key light owns those).
struct PointLight {
    glm::vec3 position{0.0f};
    float radius = 6.0f;
    glm::vec3 color{1.0f};
};

// The sky (ADR-030): drawn behind the scene into the HDR target — rooms
// have no ceilings, so this is what their open tops show. Procedural
// gradient + a sun disc on the key light's direction (setLighting), or an
// equirectangular panorama texture (8-bit sRGB, added via addTexture). The
// scene's fog applies to the sky as if it stood at the camera's far plane,
// so the horizon and the farthest geometry meet at the same colour.
// Colours are linear HDR radiance (tone-mapped like everything else).
// The defaults are the indoor facility sky: dark, cool, and well below the
// bloom threshold, close to the flat background it replaced.
struct Sky {
    glm::vec3 zenith{0.018f, 0.022f, 0.034f};
    glm::vec3 horizon{0.052f, 0.054f, 0.068f};
    glm::vec3 ground{0.030f, 0.030f, 0.034f};
    float sunSize = 1.5f;       // angular radius in degrees; 0 hides the disc
    float sunIntensity = 0.12f; // disc radiance as a multiple of the key light colour
    float sunHalo = 0.15f;      // faint glow around the disc (fraction of the disc)
    static constexpr uint32_t kNoPanorama = 0xFFFFFFFFu;
    TextureHandle panorama{kNoPanorama}; // equirect; replaces the gradient
    float panoramaIntensity = 1.0f;      // linear multiplier on the texture
    float panoramaYaw = 0.0f;            // degrees, turns the panorama about +Y
};

class Renderer {
public:
    // Throws std::runtime_error if no eligible GPU / surface creation fails.
    Renderer(Window& window, VulkanContext& context);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Blocking uploads to GPU-local memory (staging copy). Load-time API.
    [[nodiscard]] MeshHandle addMesh(std::span<const Vertex> vertices,
                                     std::span<const uint32_t> indices);
    // Same plus the skinning stream (one SkinVertex per vertex). Drawn with
    // the skinned pipelines when a DrawItem gives it a jointOffset.
    [[nodiscard]] MeshHandle addSkinnedMesh(std::span<const Vertex> vertices,
                                            std::span<const uint32_t> indices,
                                            std::span<const SkinVertex> skin);
    // rgba: 8-bit RGBA, row 0 = top, sRGB-authored (assets::ImageData fits).
    [[nodiscard]] TextureHandle addTexture(uint32_t width, uint32_t height,
                                           std::span<const uint8_t> rgba);
    // The built-in checker placeholder — valid from construction.
    [[nodiscard]] static TextureHandle defaultTexture() { return {0}; }
    // Built-in 1x1 white (ADR-030): untextured materials draw it times their
    // base colour, and it is the fallback when the texture table is full.
    [[nodiscard]] static TextureHandle whiteTexture() { return {1}; }
    // Live-texture capacity. Running out is NOT fatal: addTexture logs once
    // and returns whiteTexture() (the asset draws untextured, tint intact).
    static constexpr uint32_t kMaxTextures = 256;

    // Hot-reload path (ADR-016): waits for the GPU, swaps the resource in
    // place — every DrawItem referencing the handle sees the new data next
    // frame. Load-time quality; async streaming is a P8+ topic.
    void updateMesh(MeshHandle handle, std::span<const Vertex> vertices,
                    std::span<const uint32_t> indices);
    void updateTexture(TextureHandle handle, uint32_t width, uint32_t height,
                       std::span<const uint8_t> rgba);

    // Per-room lighting (VAULT week 5). color is the key light rgb * HDR
    // intensity; direction is surface -> light (need not be normalized, the
    // renderer normalizes). A zero direction keeps the current direction, so
    // callers can retint without re-specifying the angle. Persists across
    // frames until changed. Defaults to the warm facility key light.
    void setLighting(glm::vec3 color, glm::vec3 direction = glm::vec3(0.0f));

    // Local lights for the NEXT frames (persist until replaced). At most
    // kMaxPointLights are used; extras are dropped (caller sorts by relevance).
    static constexpr size_t kMaxPointLights = 16;
    void setPointLights(std::span<const PointLight> lights);
    // Depth fog: exponential, density per meter (0 disables). color is linear.
    void setFog(glm::vec3 color, float density);
    // Scene exposure multiplier applied before the ACES tone map (default 1).
    void setExposure(float exposure);
    // Bloom strength (0 disables; default ~0.6). Bright pixels (emissive, hot
    // speculars) bleed light — the carrier of VAULT's colour language.
    void setBloom(float intensity);
    // The sky behind everything (ADR-030); persists until changed. Pass a
    // default-constructed Sky to restore the indoor default.
    void setSky(const Sky& sky);

    // Joint palette capacity per frame (ADR-030): 2048 matrices = 32
    // characters of 64 joints. Extra matrices are dropped; a draw whose slice
    // does not fit falls back to its bind pose.
    static constexpr uint32_t kMaxJoints = 2048;

    // Renders and presents one frame: shadow pass over all items, then the
    // lit scene (then the UI hook, if set). Safe to call when minimized
    // (no-op) or with an empty span. `joints` is this frame's skinning
    // palette (model-space matrices) that DrawItem::jointOffset indexes.
    void drawFrame(const Camera& camera, std::span<const DrawItem> items,
                   std::span<const glm::mat4> joints = {});

    // Engine-internal (EditorUi). nullptr to detach. The hook must outlive
    // every drawFrame that runs while it is set.
    void setUiHook(internal::UiRenderHook* hook);

private:
    friend internal::RendererVkInfo internal::queryVkInfo(const Renderer&);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
