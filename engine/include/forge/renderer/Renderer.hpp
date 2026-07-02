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

class Renderer {
public:
    // Throws std::runtime_error if no eligible GPU / surface creation fails.
    Renderer(Window& window, VulkanContext& context);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    // Blocking upload to GPU-local memory (staging copy). Load-time API.
    // ONE mesh for now — replaced by real mesh handles with P5's asset
    // pipeline; don't design the zoo before the first animal.
    void uploadMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);

    // Renders and presents one frame. Safe to call when minimized (no-op)
    // and before uploadMesh (clears to the background color).
    void drawFrame(const Camera& camera, const glm::mat4& modelMatrix);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
