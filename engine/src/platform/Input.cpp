// engine/src/platform/Input.cpp
#include "forge/platform/Input.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace forge {

// The contract behind the "no translation table" trick: our enum values ARE
// GLFW's. Spot-check the corners; if GLFW ever renumbers, this file refuses
// to compile and the bug never ships.
static_assert(static_cast<int>(Key::Space) == GLFW_KEY_SPACE);
static_assert(static_cast<int>(Key::D0) == GLFW_KEY_0);
static_assert(static_cast<int>(Key::A) == GLFW_KEY_A);
static_assert(static_cast<int>(Key::Z) == GLFW_KEY_Z);
static_assert(static_cast<int>(Key::Escape) == GLFW_KEY_ESCAPE);
static_assert(static_cast<int>(Key::F1) == GLFW_KEY_F1);
static_assert(static_cast<int>(Key::F12) == GLFW_KEY_F12);
static_assert(static_cast<int>(Key::LeftShift) == GLFW_KEY_LEFT_SHIFT);
static_assert(static_cast<int>(Key::Menu) == GLFW_KEY_MENU);
static_assert(Input::kKeyCount == GLFW_KEY_LAST + 1);
static_assert(static_cast<int>(MouseButton::Left) == GLFW_MOUSE_BUTTON_LEFT);
static_assert(static_cast<int>(MouseButton::Right) == GLFW_MOUSE_BUTTON_RIGHT);
static_assert(static_cast<int>(MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);
static_assert(Input::kButtonCount == GLFW_MOUSE_BUTTON_LAST + 1);

namespace {
constexpr bool validKey(Key key) {
    const auto k = static_cast<int>(key);
    return k >= 0 && k < Input::kKeyCount;
}
constexpr bool validButton(MouseButton button) {
    return static_cast<int>(button) < Input::kButtonCount;
}
} // namespace

bool Input::isKeyDown(Key key) const {
    return validKey(key) && m_keyDown[static_cast<size_t>(key)];
}
bool Input::wasKeyPressed(Key key) const {
    return validKey(key) && m_keyPressed[static_cast<size_t>(key)];
}
bool Input::wasKeyReleased(Key key) const {
    return validKey(key) && m_keyReleased[static_cast<size_t>(key)];
}

bool Input::isMouseDown(MouseButton button) const {
    return validButton(button) && m_mouseDown[static_cast<size_t>(button)];
}
bool Input::wasMousePressed(MouseButton button) const {
    return validButton(button) && m_mousePressed[static_cast<size_t>(button)];
}
bool Input::wasMouseReleased(MouseButton button) const {
    return validButton(button) && m_mouseReleased[static_cast<size_t>(button)];
}

void Input::newFrame() {
    m_keyPressed.fill(false);
    m_keyReleased.fill(false);
    m_mousePressed.fill(false);
    m_mouseReleased.fill(false);
    m_mouseDeltaX = 0.0;
    m_mouseDeltaY = 0.0;
    m_scrollDeltaY = 0.0;
}

void Input::onKey(int glfwKey, int glfwAction) {
    if (glfwKey < 0 || glfwKey >= kKeyCount) {
        return;
    } // GLFW_KEY_UNKNOWN == -1
    const auto k = static_cast<size_t>(glfwKey);
    if (glfwAction == GLFW_PRESS) {
        m_keyDown[k] = true;
        m_keyPressed[k] = true; // latch: survives even if released same frame
    } else if (glfwAction == GLFW_RELEASE) {
        m_keyDown[k] = false;
        m_keyReleased[k] = true;
    }
    // GLFW_REPEAT intentionally ignored: OS key-repeat is a text-input
    // concept. Gameplay reads isKeyDown; text fields (P7) get a char callback.
}

void Input::onMouseButton(int glfwButton, int glfwAction) {
    if (glfwButton < 0 || glfwButton >= kButtonCount) {
        return;
    }
    const auto b = static_cast<size_t>(glfwButton);
    if (glfwAction == GLFW_PRESS) {
        m_mouseDown[b] = true;
        m_mousePressed[b] = true;
    } else if (glfwAction == GLFW_RELEASE) {
        m_mouseDown[b] = false;
        m_mouseReleased[b] = true;
    }
}

void Input::onCursorPos(double x, double y) {
    if (m_hasCursorSample) {
        // Accumulate: multiple motion events per frame must sum, not overwrite.
        m_mouseDeltaX += x - m_mouseX;
        m_mouseDeltaY += y - m_mouseY;
    }
    m_mouseX = x;
    m_mouseY = y;
    m_hasCursorSample = true; // first sample produces no delta (no 0,0 jump)
}

void Input::onScroll(double /*xOffset*/, double yOffset) { m_scrollDeltaY += yOffset; }

} // namespace forge
