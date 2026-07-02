// engine/include/forge/platform/Input.hpp
//
// Per-window input state, fed by GLFW callbacks (see Window.cpp), read by
// game/editor code via window.input().
//
// Edge detection is LATCH-based: a press/release event sets a flag that
// survives until the next newFrame(), so a key tapped between two polls is
// never lost (the classic current-vs-previous array diff drops those).
//
// THREADING: written and read on the main thread only, inside/around
// Window::pollEvents(). No locks by design — if you're tempted to read input
// from another thread, the design conversation comes first.

#pragma once

#include <array>
#include <cstdint>

namespace forge {

// Values intentionally mirror GLFW keycodes 1:1 — no translation table to
// write or get wrong. This header stays GLFW-free; Input.cpp static_asserts
// the pairing so a GLFW renumbering becomes a compile error, not a bug.
enum class Key : int16_t {
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    D0 = 48,
    D1 = 49,
    D2 = 50,
    D3 = 51,
    D4 = 52,
    D5 = 53,
    D6 = 54,
    D7 = 55,
    D8 = 56,
    D9 = 57,
    Semicolon = 59,
    Equal = 61,
    A = 65,
    B = 66,
    C = 67,
    D = 68,
    E = 69,
    F = 70,
    G = 71,
    H = 72,
    I = 73,
    J = 74,
    K = 75,
    L = 76,
    M = 77,
    N = 78,
    O = 79,
    P = 80,
    Q = 81,
    R = 82,
    S = 83,
    T = 84,
    U = 85,
    V = 86,
    W = 87,
    X = 88,
    Y = 89,
    Z = 90,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    F1 = 290,
    F2 = 291,
    F3 = 292,
    F4 = 293,
    F5 = 294,
    F6 = 295,
    F7 = 296,
    F8 = 297,
    F9 = 298,
    F10 = 299,
    F11 = 300,
    F12 = 301,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    LeftSuper = 343,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
    RightSuper = 347,
    Menu = 348,
};

enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4,
};

class Input {
public:
    // ---- Game/editor-facing API ----
    [[nodiscard]] bool isKeyDown(Key key) const;
    [[nodiscard]] bool wasKeyPressed(Key key) const;  // edge: went down this frame
    [[nodiscard]] bool wasKeyReleased(Key key) const; // edge: went up this frame

    [[nodiscard]] bool isMouseDown(MouseButton button) const;
    [[nodiscard]] bool wasMousePressed(MouseButton button) const;
    [[nodiscard]] bool wasMouseReleased(MouseButton button) const;

    [[nodiscard]] double mouseX() const { return m_mouseX; }
    [[nodiscard]] double mouseY() const { return m_mouseY; }
    [[nodiscard]] double mouseDeltaX() const { return m_mouseDeltaX; } // this frame
    [[nodiscard]] double mouseDeltaY() const { return m_mouseDeltaY; }
    [[nodiscard]] double scrollDeltaY() const { return m_scrollDeltaY; } // this frame

    // ---- Engine-internal: driven by Window's GLFW callbacks. ----
    // Game code has no business calling these; they're public out of
    // pragmatism (C callbacks can't be friends). Treat as off-limits.
    void newFrame(); // clears per-frame latches/deltas; call BEFORE polling
    void onKey(int glfwKey, int glfwAction);
    void onMouseButton(int glfwButton, int glfwAction);
    void onCursorPos(double x, double y);
    void onScroll(double xOffset, double yOffset);

    // Capacity contract (asserted against GLFW_*_LAST in Input.cpp).
    static constexpr int kKeyCount = 349;  // GLFW_KEY_LAST + 1
    static constexpr int kButtonCount = 8; // GLFW_MOUSE_BUTTON_LAST + 1

private:
    std::array<bool, kKeyCount> m_keyDown{};
    std::array<bool, kKeyCount> m_keyPressed{}; // latches, cleared by newFrame()
    std::array<bool, kKeyCount> m_keyReleased{};

    std::array<bool, kButtonCount> m_mouseDown{};
    std::array<bool, kButtonCount> m_mousePressed{};
    std::array<bool, kButtonCount> m_mouseReleased{};

    double m_mouseX = 0.0, m_mouseY = 0.0;
    double m_mouseDeltaX = 0.0, m_mouseDeltaY = 0.0;
    double m_scrollDeltaY = 0.0;
    bool m_hasCursorSample = false; // avoids a huge fake delta on first motion
};

} // namespace forge
