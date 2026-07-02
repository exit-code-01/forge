// engine/include/forge/ui/EditorUi.hpp
//
// Dear ImGui, engine-flavored (ADR-019): owns the ImGui context and its
// GLFW/Vulkan backends, and draws inside the Renderer's frame through the
// internal UI hook. ImGui itself never leaks through this header — apps
// that build panels include <imgui.h> themselves (the widget API IS the
// product; wrapping it would be a worse ImGui), but lifecycle, backends,
// and Vulkan plumbing live here, once.
//
// Frame contract: beginFrame() -> build panels with ImGui::* -> drawFrame()
// (the renderer invokes the hook, which Render()s and records draw data).
//
// Lifetimes: construct AFTER Window and Renderer, destroy BEFORE them
// (declaration order in the app does this naturally).

#pragma once

#include <memory>

namespace forge {

class Window;
class Renderer;

class EditorUi {
public:
    EditorUi(Window& window, Renderer& renderer);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;
    EditorUi(EditorUi&&) = delete;
    EditorUi& operator=(EditorUi&&) = delete;

    // Starts a UI frame. Call once per loop iteration, before any ImGui::*.
    void beginFrame();

    // True while the UI owns the keyboard/mouse (typing in a field, dragging
    // a widget) — gameplay input should be ignored then.
    [[nodiscard]] bool wantCaptureKeyboard() const;
    [[nodiscard]] bool wantCaptureMouse() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace forge
