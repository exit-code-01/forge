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
    // rgba: 8-bit RGBA, row 0 = top, sRGB-authored (assets::ImageData fits).
    [[nodiscard]] TextureHandle addTexture(uint32_t width, uint32_t height,
                                           std::span<const uint8_t> rgba);
    // The built-in checker placeholder — valid from construction.
    [[nodiscard]] static TextureHandle defaultTexture() { return {0}; }

    // Hot-reload path (ADR-016): waits for the GPU, swaps the resource in
    // place — every DrawItem referencing the handle sees the new data next
    // frame. Load-time quality; async streaming is a P8+ topic.
    void updateMesh(MeshHandle handle, std::span<const Vertex> vertices,
                    std::span<const uint32_t> indices);
    void updateTexture(TextureHandle handle, uint32_t width, uint32_t height,
                       std::span<const uint8_t> rgba);

    // Renders and presents one frame: shadow pass over all items, then the
    // lit scene (then the UI hook, if set). Safe to call when minimized
    // (no-op) or with an empty span.
    void drawFrame(const Camera& camera, std::span<const DrawItem> items);

    // Engine-internal (EditorUi). nullptr to detach. The hook must outlive
    // every drawFrame that runs while it is set.
    void setUiHook(internal::UiRenderHook* hook);

private:
    friend internal::RendererVkInfo internal::queryVkInfo(const Renderer&);
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
