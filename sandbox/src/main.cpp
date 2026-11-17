// sandbox/src/main.cpp — P2 demo
// Order matters: ECS first (works on ANY machine, even headless CI), then the
// window (headless CI dies here, gracefully), then the renderer — which is
// OPTIONAL: no Vulkan means warn and keep running windowed.
// Window must precede VulkanContext: the surface extensions come from GLFW.

#include "forge/anim/Clip.hpp"
#include "forge/anim/Skeleton.hpp"
#include "forge/assets/Assets.hpp"
#include "forge/audio/Audio.hpp"
#include "forge/ecs/Registry.hpp"
#include "forge/forge.hpp"
#include "forge/fx/Particles.hpp"
#include "forge/physics/PhysicsWorld.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Model.hpp"
#include "forge/renderer/Renderer.hpp"
#include "forge/renderer/VulkanContext.hpp"
#include "forge/scripting/ScriptEngine.hpp"
#include "forge/ui/EditorUi.hpp"

#include <imgui.h>

// ImGuizmo requires imgui.h BEFORE it (it includes nothing itself) — the
// blank line keeps clang-format from alphabetizing them into a broken order.
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Transform {
    glm::vec3 position{0.0f};
};
struct Velocity {
    glm::vec3 value{0.0f};
};

void ecsDemo() {
    forge::ecs::Registry registry;

    const auto a = registry.create();
    const auto b = registry.create();
    const auto c = registry.create();
    registry.emplace<Transform>(a);
    registry.emplace<Velocity>(a, glm::vec3{1.0f, 0.0f, 0.0f});
    registry.emplace<Transform>(b);
    registry.emplace<Velocity>(b, glm::vec3{0.0f, 2.0f, 0.0f});
    registry.emplace<Transform>(c); // no Velocity: must be skipped by the view

    constexpr float dt = 0.1f;
    for (int step = 0; step < 3; ++step) {
        registry.each<Transform, Velocity>(
            [](forge::ecs::Entity, Transform& t, Velocity& v) { t.position += v.value * dt; });
    }
    registry.each<Transform>([](forge::ecs::Entity e, Transform& t) {
        FORGE_INFO("entity {}v{}: pos ({:.1f}, {:.1f}, {:.1f})", e.index, e.generation,
                   t.position.x, t.position.y, t.position.z);
    });

    // Generation safety, demonstrated: destroy b, recycle its slot.
    registry.destroy(b);
    const auto d = registry.create(); // same index as b, NEW generation
    FORGE_INFO("recycled slot: b was {}v{}, d is {}v{}, alive(b)={}", b.index, b.generation,
               d.index, d.generation, registry.alive(b));
}

// Headless physics smoke: proves Jolt init + simulation on ANY machine,
// including CI runners that never get past glfwInit. A cube dropped from
// 3 m must fall and come to rest on the slab (top at y = -0.75, so the
// cube's center settles near -0.25).
void physicsDemo() {
    forge::PhysicsWorld physics;
    physics.addBox({4.0f, 0.1f, 4.0f}, {0.0f, -0.85f, 0.0f}, forge::BodyType::Static);
    const auto cube =
        physics.addBox({0.5f, 0.5f, 0.5f}, {0.0f, 3.0f, 0.0f}, forge::BodyType::Dynamic, 0.4f);

    // Feed update() frame-sized time slices, like a real game loop would —
    // big gulps hit the anti-spiral clamp by design and simulate LESS time
    // than you asked for (found out the honest way; see the P4 devlog).
    const auto simulate = [&physics](float seconds) {
        constexpr float kFrame = 1.0f / 60.0f;
        for (float t = 0.0f; t < seconds; t += kFrame) {
            physics.update(kFrame);
        }
    };

    const float y0 = physics.bodyTransform(cube)[3].y;
    simulate(0.75f); // mid-fall
    const float yMid = physics.bodyTransform(cube)[3].y;
    simulate(3.0f); // bounced and settled
    const float yRest = physics.bodyTransform(cube)[3].y;
    FORGE_INFO("physics drop: y start {:.2f} -> mid-fall {:.2f} -> rest {:.2f}", y0, yMid, yRest);
}

// Headless asset smoke: importers must work on ANY machine, including CI —
// these run before the window and fail LOUDLY if the committed assets or
// the import path regress. Loaders log their own success lines.
void assetDemo() {
    const auto torus = forge::assets::loadMesh("assets/models/torus.obj");
    const auto crate = forge::assets::loadImage("assets/textures/crate.png");
    if (torus.indices.empty() || crate.rgba.empty()) {
        throw std::runtime_error("asset smoke: importer returned empty data");
    }
}

// Headless skeletal-animation smoke (ADR-030): a two-node skeleton and
// hand-built clips exercise STEP and LINEAR sampling, quaternion slerp,
// looping and the timed cross-fade — no file, no GPU, CI-safe.
void animDemo() {
    using namespace forge::anim;
    auto skeleton = std::make_shared<Skeleton>();
    skeleton->nodes.push_back({"root", -1, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1)});
    skeleton->nodes.push_back({"tip", 0, glm::vec3(0, 1, 0), glm::quat(1, 0, 0, 0), glm::vec3(1)});
    skeleton->jointNodes = {0, 1};
    skeleton->inverseBind = {glm::mat4(1.0f),
                             glm::translate(glm::mat4(1.0f), glm::vec3(0, -1, 0))};
    auto clips = std::make_shared<std::vector<SkeletalClip>>(2);
    SkeletalClip& slide = (*clips)[0]; // root x: STEP 0 -> 1 -> 2
    slide.name = "slide";
    slide.duration = 2.0f;
    slide.channels.push_back({});
    slide.channels[0].node = 0;
    slide.channels[0].translation = {{0.0f, 1.0f, 2.0f},
                                     {glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(2, 0, 0)},
                                     Interpolation::Step};
    SkeletalClip& turn = (*clips)[1]; // tip: LINEAR 0 -> 90 deg about Z
    turn.name = "turn";
    turn.duration = 1.0f;
    turn.channels.push_back({});
    turn.channels[0].node = 1;
    turn.channels[0].rotation = {
        {0.0f, 1.0f},
        {glm::quat(1, 0, 0, 0), glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1))},
        Interpolation::Linear};

    Animator animator(skeleton, clips);
    std::vector<glm::mat4> palette;
    const auto sample = [&]() {
        palette.clear();
        animator.appendPalette(palette);
        return palette;
    };
    const auto expect = [](bool ok, const char* what) {
        if (!ok) {
            throw std::runtime_error(std::string("anim smoke: ") + what);
        }
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
    expect(animator.play("slide", {.loop = true, .speed = 1.0f, .fade = 0.0f}), "play slide");
    animator.update(0.9f);
    expect(near(sample()[0][3].x, 0.0f), "STEP holds the previous key");
    animator.update(0.2f); // t = 1.1
    expect(near(sample()[0][3].x, 1.0f), "STEP jumps at the key");
    animator.update(1.0f); // t = 2.1 -> loops to 0.1
    expect(near(animator.time(), 0.1f) && near(sample()[0][3].x, 0.0f), "loop wraps");
    // Cross-fade 1 s from slide (root x = 0 at t 0.1..0.6) to turn.
    expect(animator.play("turn", {.loop = false, .speed = 1.0f, .fade = 1.0f}), "play turn");
    animator.update(0.5f); // fade 50 %, turn at t = 0.5 -> 45 deg, blended to 22.5 deg
    // Joint 1 maps the bind-space point (0,1,0)+(1,0,0) through the tip's
    // rotation: x = cos(22.5 deg) at 50 % of a fade toward 45 degrees.
    const glm::vec3 p = glm::vec3(sample()[1] * glm::vec4(1, 1, 0, 1));
    expect(near(p.x, std::cos(glm::radians(22.5f))) && near(p.y, 1.0f + std::sin(glm::radians(22.5f))),
           "cross-fade slerps halfway");
    animator.update(1.0f); // fade done; non-looping turn holds its end pose
    const glm::vec3 q = glm::vec3(sample()[1] * glm::vec4(1, 1, 0, 1));
    expect(near(q.x, 0.0f) && near(q.y, 2.0f) && animator.clipName() == "turn", "clamp at end");
    expect(!animator.play("missing"), "unknown clip rejected");
    FORGE_INFO("anim smoke: STEP/LINEAR sampling, slerp, loop and cross-fade OK");
}

// Unit cube, 24 vertices (4 per face so normals are hard), 36 indices.
// Faces are wound CCW seen from OUTSIDE; u cross v == n keeps that true.
void appendFace(std::vector<forge::Vertex>& vertices, std::vector<uint32_t>& indices, glm::vec3 n,
                glm::vec3 u, glm::vec3 v) {
    const auto base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({(n - u - v) * 0.5f, n, {0.0f, 1.0f}});
    vertices.push_back({(n + u - v) * 0.5f, n, {1.0f, 1.0f}});
    vertices.push_back({(n + u + v) * 0.5f, n, {1.0f, 0.0f}});
    vertices.push_back({(n - u + v) * 0.5f, n, {0.0f, 0.0f}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
}

void buildCube(std::vector<forge::Vertex>& vertices, std::vector<uint32_t>& indices) {
    const glm::vec3 x{1, 0, 0};
    const glm::vec3 y{0, 1, 0};
    const glm::vec3 z{0, 0, 1};
    appendFace(vertices, indices, z, x, y);   // +Z
    appendFace(vertices, indices, -z, -x, y); // -Z
    appendFace(vertices, indices, x, -z, y);  // +X
    appendFace(vertices, indices, -x, z, y);  // -X
    appendFace(vertices, indices, y, x, -z);  // +Y
    appendFace(vertices, indices, -y, x, z);  // -Y
}

// ---- Scene components (P7.2): the scene IS the ECS now. The editor's
// hierarchy and inspector operate on these; the renderer consumes the
// DrawItems built from them each frame.
struct Name {
    std::string value;
};
struct TransformC {
    glm::vec3 position{0.0f};
    glm::vec3 eulerDeg{0.0f}; // yaw/pitch/roll editing units; radians internally
    glm::vec3 scale{1.0f};
};
// Draws a MODEL (ADR-030): an index into the host's model table — one part
// for the cube and every legacy OBJ, many for imported glTF. texture/tint/
// emissive layer over the model's own materials by forge::ModelLook's rule.
struct MeshRendererC {
    uint32_t model = 0;
    forge::TextureHandle texture;
    glm::vec3 emissive{0.0f};     // linear HDR radiance added after lighting
    glm::vec3 tint{1.0f};         // albedo multiplier
    bool textureOverride = false; // a named texture replaces authored materials
    float roughness = forge::DrawItem::kDefaultRoughness; // ADR-033
    float metallic = forge::DrawItem::kDefaultMetallic;
};
// Entities spawned from an animated model (ADR-030) get one: forge.anim.*
// drives it, the frame loop advances it and uploads its joint palette.
struct AnimatorC {
    forge::anim::Animator animator;
};
struct BodyC {
    forge::BodyId id{};
    bool dynamic = false;
    bool kinematic = false; // scripted mover; physics owns its live transform
};

// std::getenv without MSVC's C4996 noise (read-only use at startup is fine).
const char* envOr(const char* name, const char* fallback) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return value != nullptr ? value : fallback;
}

glm::mat4 trs(const TransformC& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = glm::rotate(m, glm::radians(t.eulerDeg.y), {0.0f, 1.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.eulerDeg.x), {1.0f, 0.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.eulerDeg.z), {0.0f, 0.0f, 1.0f});
    return glm::scale(m, t.scale);
}

// trs()'s rotation (Y, then X, then Z) as a quaternion: colliders that turn
// with the visual (mesh colliders, ADR-032) must use exactly this order.
glm::quat trsRotation(const TransformC& t) {
    return glm::angleAxis(glm::radians(t.eulerDeg.y), glm::vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(glm::radians(t.eulerDeg.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
           glm::angleAxis(glm::radians(t.eulerDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
}

// assets/models/import.ini (ADR-030): per-model import corrections, one
// [stem] section per model — `scale = 0.01`, `rotate = -90 0 0` (degrees,
// X then Y then Z), `hide = NodeA, NodeB` (accessory meshes to skip). Missing
// file or section = identity import. Unknown keys are logged and ignored.
std::unordered_map<std::string, forge::assets::ImportOptions> loadImportManifest(
    const std::string& path) {
    std::unordered_map<std::string, forge::assets::ImportOptions> out;
    std::ifstream file(path);
    std::string line;
    std::string section;
    const auto trim = [](const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r");
        const auto last = text.find_last_not_of(" \t\r");
        return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
    };
    while (std::getline(file, line)) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto eq = line.find('=');
        if (section.empty() || eq == std::string::npos) {
            FORGE_WARN("{}: ignoring line '{}'", path, line);
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        std::istringstream value(trim(line.substr(eq + 1)));
        auto& options = out[section];
        if (key == "scale") {
            value >> options.scale;
        } else if (key == "rotate") {
            value >> options.rotationDeg.x >> options.rotationDeg.y >> options.rotationDeg.z;
        } else if (key == "hide") {
            std::string name;
            while (std::getline(value, name, ',')) {
                if (name = trim(name); !name.empty()) {
                    options.hide.push_back(name);
                }
            }
        } else {
            FORGE_WARN("{}: [{}] unknown key '{}'", path, section, key);
        }
    }
    return out;
}

// Headless scripting smoke: VM boot + a C++ <-> Lua round trip, no window
// needed — CI exercises Lua/sol2 on all three OSes.
void scriptDemo() {
    forge::ScriptEngine script;
    script.setGlobal("answer", 21);
    const bool ok =
        script.runString("forge.log('hello from ' .. _VERSION .. ', answer*2 = ' .. (answer * 2))");
    if (!ok) {
        throw std::runtime_error("script smoke failed");
    }
}

// ---- Week 10 (ship): the user's knobs + run progress, one tiny ini.
// key=value lines, no sections, unknown keys ignored — deliberately dumb.
struct GameSettings {
    float mouseSensitivity = 0.14f;
    bool invertY = false;
    float masterVolume = 1.0f;
    int savedCheckpoint = 1; // 1 == the start; >1 offers Continue on the title
    // Script-owned numbers (best time, data cores...) via forge.game.save.
    std::map<std::string, double> values;
};

constexpr const char* kSettingsPath = "settings.ini";

GameSettings loadSettings(const char* path) {
    GameSettings s;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "sensitivity") {
                s.mouseSensitivity = std::clamp(std::stof(value), 0.02f, 0.5f);
            } else if (key == "invert_y") {
                s.invertY = value == "1";
            } else if (key == "volume") {
                s.masterVolume = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "checkpoint") {
                s.savedCheckpoint = std::max(1, std::stoi(value));
            } else if (!key.empty()) {
                s.values[key] = std::stod(value); // script-owned numbers
            }
        } catch (const std::exception&) {
            // Malformed value: keep the default. Settings must never crash.
        }
    }
    return s;
}

void saveSettings(const char* path, const GameSettings& s) {
    std::ofstream out(path, std::ios::trunc);
    out << "sensitivity=" << s.mouseSensitivity << "\n"
        << "invert_y=" << (s.invertY ? 1 : 0) << "\n"
        << "volume=" << s.masterVolume << "\n"
        << "checkpoint=" << s.savedCheckpoint << "\n";
    for (const auto& [key, value] : s.values) {
        out << key << "=" << value << "\n";
    }
}

// Hot reload (P5.3): poll a file's mtime; on change, run the reload action.
// Polling beats OS file-watch APIs here: 3 platforms, ~zero code, and a
// half-second latency nobody perceives. A failed load is NOT fatal — tools
// write files non-atomically, so we may catch a half-written file; warn and
// retry on the next poll.
class FileWatcher {
public:
    explicit FileWatcher(std::filesystem::path path) : m_path(std::move(path)) {
        std::error_code ec;
        m_lastWrite = std::filesystem::last_write_time(m_path, ec);
    }

    template <typename Fn> void poll(Fn&& reload) {
        std::error_code ec;
        const auto stamp = std::filesystem::last_write_time(m_path, ec);
        if (ec || stamp == m_lastWrite) {
            return;
        }
        try {
            reload(m_path.string());
            m_lastWrite = stamp;
        } catch (const std::exception& e) {
            FORGE_WARN("hot reload deferred ({}): {}", m_path.string(), e.what());
        }
    }

private:
    std::filesystem::path m_path;
    std::filesystem::file_time_type m_lastWrite;
};

// ---- ADR-031 HUD pieces. The script owns the words; these own the pixels.
// Prompt: "E  Talk" -> the first word sits in an orange key cap.
void drawPrompt(ImDrawList* draw, ImVec2 at, const std::string& text) {
    const auto split = text.find(' ');
    const std::string key = text.substr(0, split);
    std::string rest = split == std::string::npos ? std::string() : text.substr(split);
    rest.erase(0, rest.find_first_not_of(' '));
    const ImVec2 ks = ImGui::CalcTextSize(key.c_str());
    const ImVec2 rs = ImGui::CalcTextSize(rest.c_str());
    const float capW = std::max(ks.x + 10.0f, ks.y + 6.0f);
    const float total = capW + (rest.empty() ? 0.0f : 8.0f + rs.x);
    const float x0 = at.x - total * 0.5f;
    draw->AddRectFilled({x0 - 8.0f, at.y - 5.0f}, {x0 + total + 8.0f, at.y + ks.y + 5.0f},
                        IM_COL32(0, 0, 0, 120), 5.0f);
    draw->AddRect({x0, at.y - 2.0f}, {x0 + capW, at.y + ks.y + 2.0f},
                  IM_COL32(255, 170, 60, 240), 3.0f, 0, 1.5f);
    draw->AddText({x0 + (capW - ks.x) * 0.5f, at.y}, IM_COL32(255, 190, 90, 255), key.c_str());
    if (!rest.empty()) {
        draw->AddText({x0 + capW + 8.0f, at.y}, IM_COL32(255, 255, 255, 230), rest.c_str());
    }
}

// Mission panel, right-aligned at (right, top): title + objective lines.
// "[x] " lines draw dimmed green with a filled box, "[ ] " white with an
// open box; anything else is plain grey text.
void drawMissionPanel(ImDrawList* draw, float right, float top, const std::string& title,
                      const std::vector<std::string>& lines) {
    if (title.empty()) {
        return;
    }
    constexpr float kPad = 10.0f;
    constexpr float kBox = 9.0f;
    constexpr float kIndent = kBox + 7.0f;
    const auto body = [](const std::string& l) {
        return (l.rfind("[x] ", 0) == 0 || l.rfind("[ ] ", 0) == 0) ? l.substr(4) : l;
    };
    float width = ImGui::CalcTextSize(title.c_str()).x;
    for (const std::string& l : lines) {
        width = std::max(width, kIndent + ImGui::CalcTextSize(body(l).c_str()).x);
    }
    const float lineH = ImGui::GetTextLineHeight() + 5.0f;
    const ImVec2 a{right - width - 2.0f * kPad, top};
    const ImVec2 b{right, top + 2.0f * kPad + lineH * static_cast<float>(lines.size() + 1)};
    draw->AddRectFilled(a, b, IM_COL32(0, 0, 0, 115), 5.0f);
    draw->AddText({a.x + kPad, a.y + kPad}, IM_COL32(255, 200, 110, 240), title.c_str());
    float y = a.y + kPad + lineH + 2.0f;
    for (const std::string& l : lines) {
        const bool done = l.rfind("[x] ", 0) == 0;
        const bool open = l.rfind("[ ] ", 0) == 0;
        const float by = y + (ImGui::GetTextLineHeight() - kBox) * 0.5f;
        const float bx = a.x + kPad;
        ImU32 col = IM_COL32(200, 200, 200, 170);
        if (done) {
            draw->AddRectFilled({bx, by}, {bx + kBox, by + kBox}, IM_COL32(90, 200, 120, 190),
                                2.0f);
            col = IM_COL32(120, 200, 140, 165);
        } else if (open) {
            draw->AddRect({bx, by}, {bx + kBox, by + kBox}, IM_COL32(255, 255, 255, 210), 2.0f,
                          0, 1.3f);
            col = IM_COL32(255, 255, 255, 225);
        }
        draw->AddText({bx + kIndent, y}, col, body(l).c_str());
        y += lineH;
    }
}

// One subtitle line, bottom-centre: speaker in amber, then the text.
void drawSayLine(ImDrawList* draw, ImVec2 display, const std::string& speaker,
                 const std::string& text, float alpha) {
    const std::string who = speaker.empty() ? std::string() : speaker + "  ";
    const ImVec2 ws = ImGui::CalcTextSize(who.c_str());
    const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    const float x0 = (display.x - ws.x - ts.x) * 0.5f;
    const float y0 = display.y - 92.0f;
    const auto a = [alpha](float v) { return static_cast<int>(v * alpha); };
    draw->AddRectFilled({x0 - 12.0f, y0 - 6.0f}, {x0 + ws.x + ts.x + 12.0f, y0 + ts.y + 6.0f},
                        IM_COL32(0, 0, 0, a(150.0f)), 5.0f);
    draw->AddText({x0, y0}, IM_COL32(255, 190, 90, a(255.0f)), who.c_str());
    draw->AddText({x0 + ws.x, y0}, IM_COL32(255, 255, 255, a(240.0f)), text.c_str());
}

} // namespace

int main() {
    const auto v = forge::version();
    FORGE_INFO("Forge Engine v{}.{}.{}", v.major, v.minor, v.patch);

    ecsDemo();
    physicsDemo();
    try {
        assetDemo();
        animDemo();
        scriptDemo();
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1; // broken assets/scripting are a real failure, headless or not
    }

    try {
        // FORGE_TITLE lets two instances coexist under the GUI-drive helper
        // scripts (they find the window by title).
        forge::Window window(
            {.title = envOr("FORGE_TITLE", "Forge Sandbox"), .width = 1280, .height = 720});
        // Dev switches (never set for players): FORGE_DEV polls build/_cmd.lua
        // as a file-drop console and leaves the cursor free; FORGE_AUTOPLAY
        // skips the title so scripted checks can run unattended.
        const bool devMode = envOr("FORGE_DEV", "")[0] != '\0';
        const bool autoPlay = envOr("FORGE_AUTOPLAY", "")[0] != '\0';

        // Declaration order == destruction order: renderer dies before the
        // context, context before the window. Do not reorder.
        std::unique_ptr<forge::VulkanContext> vulkan;
        std::unique_ptr<forge::Renderer> renderer;
        std::unique_ptr<forge::EditorUi> ui; // declared AFTER renderer: dies first
        try {
            vulkan = std::make_unique<forge::VulkanContext>(window.requiredVulkanExtensions());
            renderer = std::make_unique<forge::Renderer>(window, *vulkan);
            ui = std::make_unique<forge::EditorUi>(window, *renderer);
        } catch (const std::exception& e) {
            FORGE_WARN("renderer unavailable: {} — running windowed without rendering", e.what());
        }

        forge::MeshHandle cubeMesh{};
        forge::MeshHandle torusMesh{};
        forge::TextureHandle crateTexture{};
        forge::TextureHandle laserTexture{};
        forge::TextureHandle glassTexture{};
        forge::TextureHandle concreteTexture{};
        forge::TextureHandle floorTexture{};
        forge::TextureHandle metalTexture{};
        // Script-facing registries: scene.lua spawns MODELS and retints
        // TEXTURES by name (colour language, week 9; model registry,
        // ADR-027; multi-part models, ADR-030). The host owns what a name
        // means. models[0] is always the built-in unit cube.
        std::vector<forge::Model> models;
        std::unordered_map<std::string, uint32_t> modelByName;
        constexpr uint32_t cubeModel = 0;
        uint32_t torusModel = cubeModel;
        std::unordered_map<std::string, forge::TextureHandle> textureByName;
        if (renderer) {
            std::vector<forge::Vertex> vertices;
            std::vector<uint32_t> indices;
            buildCube(vertices, indices);
            cubeMesh = renderer->addMesh(vertices, indices);
            models.push_back(forge::singleMeshModel(cubeMesh, "cube"));
            modelByName["cube"] = cubeModel;

            // The import path, end to end: file -> CPU data -> GPU handles.
            // Every OBJ / glTF / GLB in assets/models/ is spawnable by its
            // filename stem (ADR-027, ADR-030) — new art lands with no host
            // edits; import.ini corrects units/up-axis per model. A file that
            // fails to import is logged and skipped, not fatal.
            const auto manifest = loadImportManifest("assets/models/import.ini");
            forge::TextureCache modelTextures; // a pack's shared atlas uploads once
            for (const auto& entry : std::filesystem::directory_iterator("assets/models")) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ext != ".obj" && ext != ".gltf" && ext != ".glb") {
                    continue;
                }
                const std::string stem = entry.path().stem().string();
                const auto options = manifest.find(stem);
                try {
                    const auto data = forge::assets::loadModel(
                        entry.path().string(), options != manifest.end()
                                                   ? options->second
                                                   : forge::assets::ImportOptions{});
                    modelByName[stem] = static_cast<uint32_t>(models.size());
                    models.push_back(forge::uploadModel(*renderer, data, stem, &modelTextures));
                } catch (const std::exception& e) {
                    FORGE_ERROR("model '{}' skipped: {}", stem, e.what());
                }
            }
            // The P5 hot-reload demo mesh: the torus OBJ's single part.
            torusModel = modelByName.at("torus");
            torusMesh = models[torusModel].parts.at(0).mesh;
            // Every PNG in assets/textures/ is addressable by its stem, like
            // the models (upgrade pass) — new art lands with no host edits.
            for (const auto& entry : std::filesystem::directory_iterator("assets/textures")) {
                if (entry.path().extension() != ".png") {
                    continue;
                }
                const auto img = forge::assets::loadImage(entry.path().string());
                textureByName[entry.path().stem().string()] =
                    renderer->addTexture(img.width, img.height, img.rgba);
            }
            crateTexture = textureByName.at("crate");
            laserTexture = textureByName.at("laser");
            glassTexture = textureByName.at("glass");
            concreteTexture = textureByName.at("concrete");
            floorTexture = textureByName.at("floor");
            metalTexture = textureByName.at("metal");
        }

        // Physics owns WHERE dynamic things are; the ECS owns WHAT exists.
        forge::PhysicsWorld physics;
        forge::ecs::Registry scene;

        // halfExtents == 0 means "no collider".
        std::function<void(uint32_t, forge::ecs::Entity)> spawnRegisterHook;
        // Name -> entity index: scripts address everything by name, hundreds
        // of times a frame; the old linear scan was the hottest loop in the
        // host. Duplicate names resolve to the most recent spawn.
        std::unordered_map<std::string, forge::ecs::Entity> entityByName;
        const auto spawnEntity = [&](std::string name, glm::vec3 position, glm::vec3 scale,
                                     uint32_t model, forge::TextureHandle texture,
                                     glm::vec3 halfExtents, bool dynamic,
                                     float restitution = 0.3f,
                                     const forge::SpawnOptions* options = nullptr) {
            const auto e = scene.create();
            entityByName[name] = e;
            scene.emplace<Name>(e, Name{std::move(name)});
            auto& t = scene.emplace<TransformC>(e);
            t.position = position;
            t.scale = scale;
            auto& mrc = scene.emplace<MeshRendererC>(e, MeshRendererC{model, texture});
            // Any named texture (not the checker fallback) is an override.
            mrc.textureOverride = texture.value != forge::Renderer::defaultTexture().value;
            if (model < models.size() && models[model].skeleton) {
                scene.emplace<AnimatorC>(
                    e, AnimatorC{forge::anim::Animator(models[model].skeleton,
                                                       models[model].clips)});
            }
            // Laser-textured things (beams, emitters) glow by default: the
            // beam is light, not a painted stick.
            if (renderer && texture.value == laserTexture.value && texture.value != 0) {
                mrc.emissive = {6.0f, 0.5f, 0.35f};
            }
            if (options != nullptr) {
                t.eulerDeg.y = options->yawDeg; // spawn yaw: visual + collider (ADR-032)
                mrc.roughness = std::clamp(options->roughness.value_or(mrc.roughness), 0.0f, 1.0f);
                mrc.metallic = std::clamp(options->metallic.value_or(mrc.metallic), 0.0f, 1.0f);
            }
            // collider = "mesh" (ADR-032): the model's own triangles, static.
            bool meshBody = false;
            if (options != nullptr && options->meshCollider) {
                const std::string& who = scene.get<Name>(e).value;
                const forge::Model* src = model < models.size() ? &models[model] : nullptr;
                if (dynamic || options->kinematic) {
                    FORGE_WARN("spawn '{}': mesh colliders are static only, using a box", who);
                } else if (src == nullptr || src->collisionIndices.empty()) {
                    FORGE_WARN("spawn '{}': model '{}' has no static triangles, using a box", who,
                               src != nullptr ? src->name : std::string("?"));
                } else if (const auto body = physics.createStaticMesh(
                               src->collisionVertices, src->collisionIndices, position,
                               trsRotation(t), scale, options->friction, restitution)) {
                    scene.emplace<BodyC>(e, BodyC{*body, false, false});
                    if (spawnRegisterHook) {
                        spawnRegisterHook(body->value, e);
                    }
                    meshBody = true;
                } else {
                    FORGE_WARN("spawn '{}': mesh collider failed, using a box", who);
                }
            }
            if (!meshBody && halfExtents != glm::vec3(0.0f)) {
                forge::BoxDesc desc;
                desc.halfExtents = halfExtents;
                desc.position = position;
                desc.restitution = restitution;
                desc.yawDeg = t.eulerDeg.y;
                const bool kinematic = options != nullptr && options->kinematic && !dynamic;
                desc.type = dynamic     ? forge::BodyType::Dynamic
                            : kinematic ? forge::BodyType::Kinematic
                                        : forge::BodyType::Static;
                if (options != nullptr) {
                    desc.friction = options->friction;
                    desc.density = options->density;
                    desc.lockRotation = options->lockRotation;
                }
                const auto body = physics.addBox(desc);
                scene.emplace<BodyC>(e, BodyC{body, dynamic, kinematic});
                if (spawnRegisterHook) {
                    spawnRegisterHook(body.value, e);
                }
            }
            return e;
        };

        // ---- VAULT week 1: the grey-box tutorial room. Floor top at y=0.
        const auto checker = forge::Renderer::defaultTexture();
        // Week 11 art pass: fixtures resolve real meshes; anything not in the
        // registry (or a renderer-less run) falls back to the unit cube.
        const auto meshOr = [&](const std::string& name) {
            const auto it = modelByName.find(name);
            return it != modelByName.end() ? it->second : cubeModel;
        };
        const auto texOr = [&](const std::string& name) {
            const auto it = textureByName.find(name);
            return it != textureByName.end() ? it->second : checker;
        };
        spawnEntity("Floor", {0.0f, -0.2f, 0.0f}, {14.0f, 0.4f, 14.0f}, cubeModel, concreteTexture,
                    {7.0f, 0.2f, 7.0f}, false);
        // North wall has the DOORWAY: two segments + a sliding Exit Door.
        spawnEntity("Wall NW", {-4.25f, 1.6f, -7.2f}, {6.3f, 3.6f, 0.4f}, cubeModel, concreteTexture,
                    {3.15f, 1.8f, 0.2f}, false);
        spawnEntity("Wall NE", {4.25f, 1.6f, -7.2f}, {6.3f, 3.6f, 0.4f}, cubeModel, concreteTexture,
                    {3.15f, 1.8f, 0.2f}, false);
        spawnEntity("Wall N Top", {0.0f, 3.0f, -7.2f}, {2.2f, 0.8f, 0.4f}, cubeModel,
                    concreteTexture, {1.1f, 0.4f, 0.2f}, false);
        // Door is 0.32 thick vs the 0.4 wall: recessed 4 cm in its frame so an
        // OPEN door's faces never sit coplanar with "Wall N Top" (z-fighting).
        spawnEntity("Exit Door", {0.0f, 1.25f, -7.2f}, {2.2f, 2.5f, 0.32f}, meshOr("door"),
                    crateTexture, {1.1f, 1.25f, 0.16f}, false);
        spawnEntity("Exit Pad", {0.0f, -0.2f, -8.6f}, {3.0f, 0.4f, 2.6f}, cubeModel, concreteTexture,
                    {1.5f, 0.2f, 1.3f}, false);
        // Glass pane just behind the exit door: throw a crate through it.
        spawnEntity("Glass", {0.0f, 1.05f, -7.9f}, {2.2f, 2.1f, 0.12f}, cubeModel, glassTexture,
                    {1.1f, 1.05f, 0.06f}, false);
        // Laser circuit across the room at chest height (indicator for now;
        // rooms 1-3 wire it into puzzles). Beam is visual-only, Lua-driven.
        spawnEntity("Laser Emitter", {-6.9f, 1.1f, -5.0f}, {0.3f, 0.3f, 0.3f}, meshOr("emitter"),
                    laserTexture, {0.15f, 0.15f, 0.15f}, false);
        spawnEntity("Laser Receiver", {6.9f, 1.1f, -5.0f}, {0.3f, 0.3f, 0.3f}, meshOr("receiver"),
                    laserTexture, {0.15f, 0.15f, 0.15f}, false);
        spawnEntity("Laser Beam", {0.0f, 1.1f, -5.0f}, {13.2f, 0.05f, 0.05f}, cubeModel,
                    laserTexture, glm::vec3(0.0f), false); // no collider
        spawnEntity("Wall S", {0.0f, 1.6f, 7.2f}, {14.8f, 3.6f, 0.4f}, cubeModel, concreteTexture,
                    {7.4f, 1.8f, 0.2f}, false);
        spawnEntity("Wall E", {7.2f, 1.6f, 0.0f}, {0.4f, 3.6f, 14.8f}, cubeModel, concreteTexture,
                    {0.2f, 1.8f, 7.4f}, false);
        spawnEntity("Wall W", {-7.2f, 1.6f, 0.0f}, {0.4f, 3.6f, 14.8f}, cubeModel, concreteTexture,
                    {0.2f, 1.8f, 7.4f}, false);
        // Step (walkable, tests step-offset) and ledge (needs the elevator).
        spawnEntity("Step", {3.0f, 0.15f, 2.0f}, {2.4f, 0.3f, 2.4f}, cubeModel, concreteTexture,
                    {1.2f, 0.15f, 1.2f}, false);
        spawnEntity("Ledge", {5.5f, 1.1f, -5.5f}, {3.0f, 2.2f, 3.0f}, cubeModel, concreteTexture,
                    {1.5f, 1.1f, 1.5f}, false);
        // Throwables — Crate A sits dead ahead of the spawn look direction.
        spawnEntity("Crate A", {0.0f, 0.4f, 2.5f}, {0.5f, 0.5f, 0.5f}, meshOr("crate"),
                    crateTexture, {0.25f, 0.25f, 0.25f}, true, 0.3f);
        spawnEntity("Crate B", {-1.2f, 0.4f, 1.2f}, {0.5f, 0.5f, 0.5f}, meshOr("crate"),
                    crateTexture, {0.25f, 0.25f, 0.25f}, true, 0.3f);
        spawnEntity("Crate C", {1.3f, 0.4f, 0.8f}, {0.5f, 0.5f, 0.5f}, meshOr("crate"),
                    crateTexture, {0.25f, 0.25f, 0.25f}, true, 0.3f);
        // Tutorial pressure plate (scene.lua animates it sinking) + the mascot.
        spawnEntity("Plate T", {-3.0f, 0.05f, 2.0f}, {1.4f, 0.1f, 1.4f}, meshOr("plate"),
                    crateTexture, {0.7f, 0.05f, 0.7f}, false);
        spawnEntity("Torus", {-3.5f, 0.27f, -3.5f}, {1.0f, 1.0f, 1.0f}, torusModel, crateTexture,
                    {0.85f, 0.25f, 0.85f}, false);
        // UNIT-7's gravity glove (week 11): the first-person viewmodel. No
        // collider — pure presentation, re-posed every frame from the camera.
        // Orange by the colour law: the glove IS the interact verb.
        const auto gloveEntity =
            spawnEntity("Glove", {0.0f, 1.0f, 5.0f}, {0.8f, 0.8f, 0.8f}, meshOr("glove"),
                        texOr("metal_orange"), glm::vec3(0.0f), false);

        forge::Audio audio;
        forge::fx::ParticleEmitter sparks;

        // The elevator: keyframe-animated platform (P8) rides up to the ledge.
        // Upgrade pass: a KINEMATIC body now — it carries its rider with a real
        // velocity instead of teleporting out from under them (ADR-020 debt).
        const forge::SpawnOptions platformOptions{.kinematic = true};
        const auto platformEntity =
            spawnEntity("Elevator", {3.2f, 0.15f, -5.5f}, {1.8f, 0.3f, 1.8f}, cubeModel,
                        crateTexture, {0.9f, 0.15f, 0.9f}, false, 0.3f, &platformOptions);
        forge::anim::Clip platformClip;
        platformClip.positionKeys = {
            {0.0f, {3.2f, 0.15f, -5.5f}},
            {3.0f, {3.2f, 2.05f, -5.5f}},
            {4.5f, {3.2f, 2.05f, -5.5f}}, // dwell at the top
            {7.5f, {3.2f, 0.15f, -5.5f}},
        };
        float animTime = 0.0f;

        // Glove viewmodel clips (week 11): keys live in CAMERA space
        // (x = right, y = up, z = along look) and both clips end back at
        // zero, so finishing a clip IS returning to the idle pose. These are
        // the first consumers of Clip's euler rotation curves (ADR-027).
        forge::anim::Clip gloveThrowClip;
        gloveThrowClip.positionKeys = {
            {0.0f, {0.0f, 0.0f, 0.0f}},
            {0.07f, {0.0f, 0.02f, 0.17f}},    // punch out with the release
            {0.17f, {0.0f, -0.015f, -0.05f}}, // recoil past neutral
            {0.34f, {0.0f, 0.0f, 0.0f}},
        };
        gloveThrowClip.eulerKeys = {
            {0.0f, {0.0f, 0.0f, 0.0f}},
            {0.07f, {-16.0f, 0.0f, 0.0f}}, // wrist snaps down with the punch
            {0.19f, {8.0f, 0.0f, -5.0f}},  // overshoot up, a hint of roll
            {0.34f, {0.0f, 0.0f, 0.0f}},
        };
        forge::anim::Clip gloveGrabClip;
        gloveGrabClip.positionKeys = {
            {0.0f, {0.0f, 0.0f, 0.0f}},
            {0.08f, {0.0f, -0.02f, -0.08f}}, // pull back toward the chest
            {0.22f, {0.0f, 0.0f, 0.0f}},
        };
        gloveGrabClip.eulerKeys = {
            {0.0f, {0.0f, 0.0f, 0.0f}},
            {0.08f, {11.0f, 0.0f, 4.0f}}, // wrist cocks up around the catch
            {0.22f, {0.0f, 0.0f, 0.0f}},
        };
        const forge::anim::Clip* gloveClip = nullptr;
        float gloveClipTime = 0.0f;
        float gloveClipAmp = 1.0f; // week 12: recoil scales with charge
        float gloveSwayTime = 0.0f;
        float sprintLean = 0.0f; // week 12 juice: the glove leans into a sprint

        // ---- The player: capsule controller + first-person camera.
        physics.createCharacter({0.0f, 1.0f, 5.0f}, 0.35f, 0.55f);
        struct Player {
            float yaw = 0.0f; // 0 faces -Z (toward the room)
            float pitch = 0.0f;
            forge::BodyId held{};
            bool holding = false;
            bool wasGrounded = true;
            float stepTimer = 0.0f;
            // Charged launch (week 12). The press-edge disambiguation lives
            // here: a press that GRABS must never throw on its release; a
            // press that starts while already holding is a CHARGE-press and
            // throws on release. Lua reads chargeTime and maps it to
            // throwSpeed (clamped below); tap-throws stay legacy 11 m/s.
            bool chargePress = false;
            float chargeTime = 0.0f;
            float throwSpeed = 11.0f;
            // Feel pass (upgrade): buffered jumps, head bob, landing dip.
            float jumpBuffer = 0.0f;
            float bobPhase = 0.0f;
            float bobAmp = 0.0f;
            float landDip = 0.0f;
            float fallSpeed = 0.0f;
            float stuckTime = 0.0f; // held crate wedged far from the glove
        } player;
        float cameraShake = 0.0f; // decays; scripts add via forge.camera.shake
        float shakeClock = 0.0f;
        forge::ecs::Entity aimEntity = forge::ecs::kNullEntity; // glove target glow
        // HUD extras (upgrade): toasts + named stat lines, script-driven.
        struct Toast {
            std::string text;
            float left = 0.0f;
        };
        std::vector<Toast> toasts;
        std::vector<std::pair<std::string, std::string>> stats;
        // Local lights by id; the nearest kMaxPointLights reach the renderer.
        std::unordered_map<std::string, forge::PointLight> lights;
        // The player's aim ray, script-visible via forge.player.look().
        glm::vec3 camEye{0.0f, 1.65f, 5.0f};
        glm::vec3 camLook{0.0f, 0.0f, -1.0f};
        // Reticle answer-back (week 12): Lua sets mode+value, we draw.
        std::string reticleMode = "default";
        float reticleValue = 0.0f;
        bool playMode = true; // TAB toggles into the editor

        // ---- Game state (week 6): a tiny menu FSM layered over play mode.
        // Title on launch; Esc pauses; the script signals Won. Gameplay sim
        // only advances in Playing — the other states freeze the world behind
        // the menu. The editor (TAB) is orthogonal: it's a dev tool, not a
        // game state, and keeps its own Esc = quit.
        enum class GameState { Title, Playing, Paused, Won };
        GameState gameState = autoPlay ? GameState::Playing : GameState::Title;
        std::string hudHint; // per-room objective line, set by scene.lua
        // Location/mission pass (ADR-031): interact prompt, mission panel,
        // one subtitle line, a scene fade and the view distance — all
        // script-driven, drawn here.
        std::string hudPrompt;
        std::string missionTitle;
        std::vector<std::string> missionLines;
        struct SayLine {
            std::string speaker;
            std::string text;
            float left = 0.0f;
            float total = 1.0f;
        } sayLine;
        float fadeAlpha = 0.0f;
        float viewDistance = 100.0f;

        // Week 10 (ship): user settings + run progress survive restarts.
        GameSettings settings = loadSettings(kSettingsPath);
        audio.setMasterVolume(settings.masterVolume);
        const auto cursorForState = [&]() {
            window.setCursorCaptured(!devMode && playMode && gameState == GameState::Playing);
        };
        cursorForState(); // start on the title screen: cursor free

        // Scripting: gameplay decisions move to Lua (ADR-018). Script-spawned
        // bodies become full scene entities — they show up in the hierarchy.
        forge::ScriptEngine script;
        script.bindPhysics(physics);
        script.bindInput(window.input());
        script.bindAudio(audio);
        script.bindFx(sparks);
        int scriptBoxCount = 0;
        script.onBoxSpawned = [&](forge::BodyId body, glm::vec3 halfExtents) {
            const auto e = scene.create();
            scene.emplace<Name>(e, Name{"Box " + std::to_string(++scriptBoxCount)});
            scene.emplace<TransformC>(e).scale = halfExtents * 2.0f;
            scene.emplace<MeshRendererC>(e, MeshRendererC{cubeModel, crateTexture, glm::vec3(0.0f),
                                                          glm::vec3(1.0f), true});
            scene.emplace<BodyC>(e, BodyC{body, true});
        };
        // (scene.lua is loaded below, after ALL bindings are installed.)

        // Gravity-glove pickup needs "which ENTITY is this body?".
        std::unordered_map<uint32_t, forge::ecs::Entity> bodyToEntity;
        scene.each<Name>([&](forge::ecs::Entity e, Name&) {
            if (auto* b = scene.tryGet<BodyC>(e)) {
                bodyToEntity[b->id.value] = e;
            }
        });
        // Grab fix: Lua-spawned entities (room crates!) must be glove-visible
        // too — registration now happens for every spawn path.
        spawnRegisterHook = [&bodyToEntity](uint32_t body, forge::ecs::Entity e) {
            bodyToEntity[body] = e;
        };
        const auto onSpawnedOld = script.onBoxSpawned;
        script.onBoxSpawned = [&, onSpawnedOld](forge::BodyId body, glm::vec3 he) {
            onSpawnedOld(body, he);
            scene.each<Name>([&](forge::ecs::Entity e, Name&) {
                if (auto* b = scene.tryGet<BodyC>(e); b != nullptr && b->id.value == body.value) {
                    bodyToEntity[body.value] = e;
                }
            });
        };

        // Week 2 scene hooks: Lua addresses entities by NAME (door logic
        // lives in scene.lua); the host resolves names and keeps colliders
        // synced. Linear lookup is fine at this entity count.
        const auto findByName = [&](const std::string& name) {
            const auto it = entityByName.find(name);
            if (it != entityByName.end() && scene.alive(it->second)) {
                return it->second;
            }
            return forge::ecs::kNullEntity;
        };
        script.onSetEntityPosition = [&](const std::string& name, glm::vec3 p) {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return;
            }
            auto& t = scene.get<TransformC>(e);
            auto* b = scene.tryGet<BodyC>(e);
            if (b != nullptr && b->kinematic) {
                // Platforms SWEEP to the target (riders are carried); a big
                // jump (a reset) is a warp, not a 100 m/s shove.
                const glm::vec3 at = glm::vec3(physics.bodyTransform(b->id)[3]);
                if (glm::length(p - at) > 2.0f) {
                    physics.teleport(b->id, p);
                } else {
                    physics.moveKinematic(b->id, p);
                }
                t.position = p;
                return;
            }
            if (b != nullptr && !b->dynamic && t.position == p) {
                return; // static and already there: skip the broadphase churn
            }
            t.position = p;
            if (b != nullptr) {
                physics.teleport(b->id, p);
            }
        };
        // Week 12 fix: dynamic bodies live in PHYSICS — TransformC only holds
        // their spawn pose. getPosition must return the live body transform,
        // or every script read of a moving crate is frozen at its spawn point
        // (this also silently broke the week-6 crate-void respawn check).
        script.onGetEntityPosition = [&](const std::string& name) {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return glm::vec3(0.0f);
            }
            if (auto* b = scene.tryGet<BodyC>(e); b != nullptr && (b->dynamic || b->kinematic)) {
                return glm::vec3(physics.bodyTransform(b->id)[3]);
            }
            return scene.get<TransformC>(e).position;
        };
        script.onSetEntityScale = [&](const std::string& name, glm::vec3 sc) {
            const auto e = findByName(name);
            if (scene.alive(e)) {
                scene.get<TransformC>(e).scale = sc;
            }
        };
        // Character/prop orientation (ADR-027): visual transform only —
        // colliders stay axis-aligned, so scripts rotate colliderless parts.
        script.onSetEntityRotation = [&](const std::string& name, glm::vec3 eulerDeg) {
            const auto e = findByName(name);
            if (scene.alive(e)) {
                scene.get<TransformC>(e).eulerDeg = eulerDeg;
            }
        };
        script.onDestroyEntity = [&](const std::string& name) {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return;
            }
            if (auto* b = scene.tryGet<BodyC>(e)) {
                if (player.holding && player.held.value == b->id.value) {
                    player.holding = false; // never keep a handle to a dead body
                    player.chargePress = false;
                }
                bodyToEntity.erase(b->id.value);
                physics.removeBody(b->id);
            }
            if (aimEntity == e) {
                aimEntity = forge::ecs::kNullEntity;
            }
            entityByName.erase(name);
            scene.destroy(e);
        };
        script.onSpawnEntity = [&](const std::string& name, glm::vec3 pos, glm::vec3 sc,
                                   glm::vec3 half, const std::string& texture, bool dynamic,
                                   const std::string& mesh, const forge::SpawnOptions& options) {
            const auto it = textureByName.find(texture);
            const auto mi = modelByName.find(mesh);
            spawnEntity(name, pos, sc, mi != modelByName.end() ? mi->second : cubeModel,
                        it != textureByName.end() ? it->second : checker, half, dynamic,
                        options.restitution, &options);
        };
        // ---- Upgrade pass seams (ADR-029).
        script.onSetEntityEmissive = [&](const std::string& name, glm::vec3 emissive) {
            if (const auto e = findByName(name); scene.alive(e)) {
                if (auto* mr = scene.tryGet<MeshRendererC>(e)) {
                    mr->emissive = emissive;
                }
            }
        };
        script.onSetEntityTint = [&](const std::string& name, glm::vec3 tint) {
            if (const auto e = findByName(name); scene.alive(e)) {
                if (auto* mr = scene.tryGet<MeshRendererC>(e)) {
                    mr->tint = tint;
                }
            }
        };
        script.onSetEntityMaterial = [&](const std::string& name, std::optional<float> roughness,
                                         std::optional<float> metallic) { // ADR-033
            if (const auto e = findByName(name); scene.alive(e)) {
                if (auto* mr = scene.tryGet<MeshRendererC>(e)) {
                    mr->roughness = roughness ? std::clamp(*roughness, 0.0f, 1.0f) : mr->roughness;
                    mr->metallic = metallic ? std::clamp(*metallic, 0.0f, 1.0f) : mr->metallic;
                }
            }
        };
        // ---- Skeletal animation seams (ADR-030).
        script.onAnimPlay = [&](const std::string& name, const std::string& clip,
                                const forge::AnimPlayOptions& o) {
            const auto e = findByName(name);
            auto* a = scene.alive(e) ? scene.tryGet<AnimatorC>(e) : nullptr;
            if (a == nullptr) {
                return false;
            }
            const bool ok =
                a->animator.play(clip, forge::anim::PlayOptions{o.loop, o.speed, o.fade, o.restart});
            if (!ok) {
                FORGE_WARN("anim.play: '{}' has no clip '{}'", name, clip);
            }
            return ok;
        };
        script.onAnimClips = [&](const std::string& modelName) {
            std::vector<std::string> names;
            const auto it = modelByName.find(modelName);
            if (it != modelByName.end() && models[it->second].clips) {
                for (const auto& clip : *models[it->second].clips) {
                    names.push_back(clip.name);
                }
            }
            return names;
        };
        script.onAnimTime =
            [&](const std::string& name) -> std::optional<std::pair<float, float>> {
            const auto e = findByName(name);
            const auto* a = scene.alive(e) ? scene.tryGet<AnimatorC>(e) : nullptr;
            if (a == nullptr || !a->animator.playing()) {
                return std::nullopt;
            }
            return std::make_pair(a->animator.time(), a->animator.duration());
        };
        script.onEntityExists = [&](const std::string& name) {
            return scene.alive(findByName(name));
        };
        script.onBodyName = [&](uint32_t body) {
            const auto it = bodyToEntity.find(body);
            if (it == bodyToEntity.end() || !scene.alive(it->second)) {
                return std::string();
            }
            return scene.get<Name>(it->second).value;
        };
        script.onHasModel = [&](const std::string& model) {
            return modelByName.find(model) != modelByName.end();
        };
        script.onBodyId = [&](const std::string& name) -> uint32_t {
            const auto e = findByName(name);
            if (!scene.alive(e)) {
                return 0;
            }
            const auto* b = scene.tryGet<BodyC>(e);
            return b != nullptr ? b->id.value : 0;
        };
        script.onPushPlayer = [&](glm::vec3 velocity) { physics.pushCharacter(velocity); };
        script.onPlayerVelocity = [&]() { return physics.characterVelocity(); };
        script.onPlayerGrounded = [&]() { return physics.characterGrounded(); };
        script.onPlayerHolding = [&]() {
            if (!player.holding) {
                return std::string();
            }
            const auto it = bodyToEntity.find(player.held.value);
            return it != bodyToEntity.end() && scene.alive(it->second)
                       ? scene.get<Name>(it->second).value
                       : std::string();
        };
        script.onPlayerDrop = [&]() {
            player.holding = false;
            player.chargePress = false;
            player.chargeTime = 0.0f;
            player.throwSpeed = 11.0f;
        };
        script.onSetLight = [&](const std::string& id, glm::vec3 position, glm::vec3 color,
                                float radius) {
            lights[id] = forge::PointLight{position, radius, color};
        };
        script.onClearLight = [&](const std::string& id) { lights.erase(id); };
        // Sky (ADR-030): scripts describe it; the host resolves the panorama
        // name against the texture registry. Unknown names keep the gradient.
        script.onSetSky = [&](const forge::SkyOptions& o) {
            if (!renderer) {
                return;
            }
            forge::Sky sky; // the renderer's defaults, overridden field by field
            sky.zenith = o.zenith.value_or(sky.zenith);
            sky.horizon = o.horizon.value_or(sky.horizon);
            sky.ground = o.ground.value_or(sky.ground);
            sky.sunSize = o.sunSize.value_or(sky.sunSize);
            sky.sunIntensity = o.sunIntensity.value_or(sky.sunIntensity);
            sky.sunHalo = o.sunHalo.value_or(sky.sunHalo);
            sky.panoramaIntensity = o.textureIntensity.value_or(sky.panoramaIntensity);
            sky.panoramaYaw = o.textureYaw.value_or(sky.panoramaYaw);
            if (!o.texture.empty()) {
                if (const auto it = textureByName.find(o.texture); it != textureByName.end()) {
                    sky.panorama = it->second;
                } else {
                    FORGE_WARN("set_sky: no texture '{}', keeping the gradient", o.texture);
                }
            }
            renderer->setSky(sky);
        };
        script.onSetFog = [&](glm::vec3 color, float density) {
            if (renderer) {
                renderer->setFog(color, density);
            }
        };
        script.onSetAmbient = [&](glm::vec3 sky, glm::vec3 ground, float weight) { // ADR-033
            if (renderer) {
                renderer->setAmbient(sky, ground, weight);
            }
        };
        script.onSetExposure = [&](float exposure) {
            if (renderer) {
                renderer->setExposure(exposure);
            }
        };
        script.onSetBloom = [&](float intensity) {
            if (renderer) {
                renderer->setBloom(intensity);
            }
        };
        script.onToast = [&](const std::string& text, float seconds) {
            if (toasts.size() >= 4) {
                toasts.erase(toasts.begin());
            }
            toasts.push_back({text, seconds});
        };
        script.onSetStat = [&](const std::string& key, const std::string& value) {
            for (auto it = stats.begin(); it != stats.end(); ++it) {
                if (it->first == key) {
                    if (value.empty()) {
                        stats.erase(it);
                    } else {
                        it->second = value;
                    }
                    return;
                }
            }
            if (!value.empty()) {
                stats.emplace_back(key, value);
            }
        };
        script.onCameraShake = [&](float amount) {
            cameraShake = std::min(cameraShake + amount, 1.5f);
        };
        script.onSaveValue = [&](const std::string& key, double value) {
            // Reserved keys belong to the host's own settings.
            if (key == "sensitivity" || key == "invert_y" || key == "volume" ||
                key == "checkpoint" || key.find('=') != std::string::npos) {
                return;
            }
            const auto it = settings.values.find(key);
            if (it == settings.values.end() || it->second != value) {
                settings.values[key] = value;
                saveSettings(kSettingsPath, settings);
            }
        };
        script.onLoadValue = [&](const std::string& key) {
            const auto it = settings.values.find(key);
            return it != settings.values.end() ? it->second : 0.0;
        };
        // Colour language (week 9): scripts retint by name — doors flip
        // red/green, plates orange/green. Unknown names are a no-op.
        script.onSetEntityTexture = [&](const std::string& name, const std::string& texture) {
            const auto e = findByName(name);
            const auto it = textureByName.find(texture);
            if (scene.alive(e) && it != textureByName.end()) {
                if (auto* mr = scene.tryGet<MeshRendererC>(e)) {
                    mr->texture = it->second;
                    mr->textureOverride = true; // explicit: wins over model materials
                }
            }
        };
        script.onPlayerPosition = [&]() { return physics.characterPosition(); };
        // Aim + charge seams (week 12): the same eye/look the glove uses, the
        // charge-press timer, and the script-tuned launch speed (clamped to
        // the 11..25 design range; reset to legacy after every throw).
        script.onPlayerLook = [&]() { return std::make_pair(camEye, camLook); };
        script.onPlayerCharge = [&]() { return player.chargePress ? player.chargeTime : 0.0f; };
        script.onSetThrowSpeed = [&](float speed) {
            player.throwSpeed = std::clamp(speed, 11.0f, 25.0f);
        };
        // Reticle answer-back (week 12): Lua owns when, we own the pixels.
        script.onSetReticle = [&](const std::string& mode, float value) {
            reticleMode = mode;
            reticleValue = std::clamp(value, 0.0f, 1.0f);
        };
        // Per-room lighting pass (week 5): scene.lua tints the key light by
        // player z. Renderer holds it across frames; zero dir = keep the angle.
        script.onSetLighting = [&](glm::vec3 color, glm::vec3 dir) {
            if (renderer) { // bugfix: no-Vulkan runs crashed on the first frame
                renderer->setLighting(color, dir);
            }
        };
        // Respawn/checkpoint (week 6): the script warps the player; the host
        // owns the character controller (zeroes velocity on the teleport).
        script.onSetPlayerPosition = [&](glm::vec3 p) {
            physics.setCharacterPosition(p);
            player.holding = false; // a warp drops whatever you were carrying
            player.chargePress = false;
            player.chargeTime = 0.0f;
        };
        // Run complete (week 6): the script fires this once the finale clears.
        // The host raises the win screen and frees the cursor for the menu.
        script.onWin = [&]() {
            gameState = GameState::Won;
            cursorForState();
        };
        // Objective hint (week 7): the script owns the words, we own the pixels.
        script.onSetHint = [&](const std::string& text) { hudHint = text; };
        // Save seam (week 10): the script reports the furthest checkpoint;
        // we persist it. The title menu turns it into a Continue button.
        script.onSaveCheckpoint = [&](int checkpoint) {
            if (checkpoint != settings.savedCheckpoint) {
                settings.savedCheckpoint = checkpoint;
                saveSettings(kSettingsPath, settings);
            }
        };
        // Location/mission pass (ADR-031).
        script.onSetPrompt = [&](const std::string& text) { hudPrompt = text; };
        script.onSetMission = [&](const std::string& title, const std::vector<std::string>& lines) {
            missionTitle = title;
            missionLines = lines;
        };
        script.onSay = [&](const std::string& speaker, const std::string& text, float seconds) {
            sayLine = {speaker, text, std::max(seconds, 0.1f), std::max(seconds, 0.1f)};
        };
        script.onSetFade = [&](float alpha) { fadeAlpha = std::clamp(alpha, 0.0f, 1.0f); };
        script.onIsDev = [&]() { return devMode; };
        script.onSetViewDistance = [&](float meters) {
            viewDistance = std::clamp(meters, 20.0f, 1000.0f);
        };
        script.onSetPlayerYaw = [&](float yawDeg, float pitchDeg) {
            player.yaw = yawDeg;
            player.pitch = std::clamp(pitchDeg, -89.0f, 89.0f);
        };
        script.bindScene();
        const std::string mainScript = "assets/scripts/scene.lua";
        script.runFile(mainScript);

        // Editor state: selection, simulation control, orbit camera.
        forge::ecs::Entity selected = forge::ecs::kNullEntity;
        bool paused = false;
        bool stepOnce = false;
        float camYaw = 39.0f;
        float camPitch = 17.0f;
        float camDist = 7.0f;
        const glm::vec3 camTarget{0.0f, 0.3f, 0.0f};

        // Edit assets/textures/crate.png, assets/models/torus.obj, or
        // assets/scripts/scene.lua while this runs — changes land in ~0.5 s.
        FileWatcher crateWatch{"assets/textures/crate.png"};
        FileWatcher torusWatch{"assets/models/torus.obj"};
        FileWatcher scriptWatch{mainScript};
        // Script modules (ADR-031): every forge.include'd file is watched too.
        // A changed module re-runs the MAIN script, which re-includes it —
        // modules extend the main script's tables, so a lone re-run would
        // append its data twice.
        std::vector<FileWatcher> includeWatches;
        const auto syncIncludeWatches = [&]() {
            const auto& files = script.includedFiles();
            while (includeWatches.size() < files.size()) {
                includeWatches.emplace_back(files[includeWatches.size()]);
            }
        };
        syncIncludeWatches();

        bool jointBudgetWarned = false;
        auto lastTime = std::chrono::steady_clock::now();
        auto lastPoll = lastTime;
        auto lastDevPoll = lastTime;
        auto& input = window.input();
        while (!window.shouldClose()) {
            window.pollEvents();
            // ---- Menu FSM input. In the editor (TAB), Esc keeps its old
            // meaning: quit. In play mode, Esc drives the menu: Title/Won ->
            // quit, Playing <-> Paused. Enter starts a run from Title/Won.
            if (input.wasKeyPressed(forge::Key::Escape)) {
                if (!playMode) {
                    window.requestClose();
                } else if (gameState == GameState::Playing) {
                    gameState = GameState::Paused;
                    cursorForState();
                } else if (gameState == GameState::Paused) {
                    gameState = GameState::Playing;
                    cursorForState();
                } else { // Title or Won
                    window.requestClose();
                }
            }
            if (playMode && input.wasKeyPressed(forge::Key::Enter) &&
                (gameState == GameState::Title || gameState == GameState::Won)) {
                if (gameState == GameState::Won) {
                    script.runString("resetGame()"); // soft restart the run
                }
                gameState = GameState::Playing;
                cursorForState();
            }
            // C on the title: continue from the saved checkpoint (week 10).
            if (playMode && gameState == GameState::Title && settings.savedCheckpoint > 1 &&
                input.wasKeyPressed(forge::Key::C)) {
                gameState = GameState::Playing;
                cursorForState();
                script.runString("resumeAt(" + std::to_string(settings.savedCheckpoint) + ")");
            }
            if (input.wasKeyPressed(forge::Key::Tab)) {
                playMode = !playMode;
                player.holding = false; // no stale grabs across mode switches
                player.chargePress = false;
                player.chargeTime = 0.0f;
                cursorForState();       // editor frees the cursor; play recaptures
            }

            // Gameplay simulation runs only while actually playing. In the
            // editor, the old Pause checkbox still governs; the menu states
            // (Title/Paused/Won) freeze the world behind the overlay.
            const bool simRun = playMode ? (gameState == GameState::Playing && !paused) : !paused;

            const auto now = std::chrono::steady_clock::now();
            // Clamp: a hitch (window drag, breakpoint) must not hand gameplay a
            // multi-second step — timers and springs explode on those.
            const float dt =
                std::min(std::chrono::duration<float>(now - lastTime).count(), 0.1f);
            lastTime = now;

            forge::Camera camera{};
            camera.farPlane = viewDistance; // script-set (outdoor locations, ADR-031)
            bool aimGrabbable = false; // crosshair affordance: "this will grab"
            if (playMode) {
                // ---- First-person: mouse-look + WASD through the capsule.
                // Mouse-look only while actually playing — a frozen menu must
                // not swing the camera. The look vector is still computed each
                // frame so the frozen scene renders from the right pose.
                if (simRun) {
                    const float sens = settings.mouseSensitivity;
                    const float ySign = settings.invertY ? -1.0f : 1.0f;
                    player.yaw += static_cast<float>(input.mouseDeltaX()) * sens;
                    player.pitch = std::clamp(
                        player.pitch - static_cast<float>(input.mouseDeltaY()) * sens * ySign,
                        -89.0f, 89.0f);
                }
                const float py = glm::radians(player.yaw);
                const float pp = glm::radians(player.pitch);
                const glm::vec3 look{std::cos(pp) * std::sin(py), std::sin(pp),
                                     -std::cos(pp) * std::cos(py)};
                const glm::vec3 forward{std::sin(py), 0.0f, -std::cos(py)};
                const glm::vec3 right{std::cos(py), 0.0f, std::sin(py)};

                // Movement, jumping and footsteps only advance while playing;
                // a paused/title/won world is frozen behind its menu.
                if (simRun) {
                    glm::vec3 move{0.0f};
                    if (input.isKeyDown(forge::Key::W)) {
                        move += forward;
                    }
                    if (input.isKeyDown(forge::Key::S)) {
                        move -= forward;
                    }
                    if (input.isKeyDown(forge::Key::D)) {
                        move += right;
                    }
                    if (input.isKeyDown(forge::Key::A)) {
                        move -= right;
                    }
                    if (move != glm::vec3(0.0f)) {
                        move = glm::normalize(move);
                    }
                    const float speed = input.isKeyDown(forge::Key::LeftShift) ? 7.0f : 4.5f;
                    // Jump buffering: a press up to 0.12 s early still fires on
                    // touchdown; coyote time (in PhysicsWorld) covers late ones.
                    if (input.wasKeyPressed(forge::Key::Space)) {
                        player.jumpBuffer = 0.12f;
                    } else {
                        player.jumpBuffer = std::max(player.jumpBuffer - dt, 0.0f);
                    }
                    if (physics.moveCharacter(move * speed, player.jumpBuffer > 0.0f, dt)) {
                        player.jumpBuffer = 0.0f;
                        audio.play("assets/sounds/jump.wav");
                    }

                    // Landing + footsteps ride the grounded state's edges. The
                    // landing answers in proportion to the fall: louder thud,
                    // deeper camera dip.
                    const bool grounded = physics.characterGrounded();
                    if (grounded && !player.wasGrounded) {
                        const float hit = std::clamp(player.fallSpeed / 9.0f, 0.0f, 1.0f);
                        if (hit > 0.12f) {
                            audio.play("assets/sounds/land.wav", 0.45f + 0.55f * hit,
                                       1.1f - 0.25f * hit);
                            player.landDip = std::max(player.landDip, 0.16f * hit);
                        }
                    }
                    if (!grounded) {
                        player.fallSpeed = std::max(-physics.characterVelocity().y, 0.0f);
                    } else {
                        player.fallSpeed = 0.0f;
                    }
                    player.wasGrounded = grounded;
                    const bool stepping = grounded && move != glm::vec3(0.0f);
                    if (stepping) {
                        player.stepTimer -= dt * (speed > 5.0f ? 1.5f : 1.0f);
                        if (player.stepTimer <= 0.0f) {
                            // +-6% pitch per step: no machine-gun footsteps.
                            const float jitter =
                                0.94f + 0.12f * std::fmod(player.bobPhase * 0.37f + 0.5f, 1.0f);
                            audio.play("assets/sounds/step.wav", 0.8f, jitter);
                            player.stepTimer = 0.38f;
                        }
                        player.bobPhase += dt * (speed > 5.0f ? 12.5f : 8.3f);
                    } else {
                        player.stepTimer = 0.12f; // first step lands quickly
                    }
                    const float blend = std::min(dt * 9.0f, 1.0f);
                    player.bobAmp += ((stepping ? 1.0f : 0.0f) - player.bobAmp) * blend;
                    player.landDip *= std::exp(-dt * 9.0f);
                    cameraShake *= std::exp(-dt * 7.0f);
                    shakeClock += dt;
                }

                // Head bob + landing dip + shake: the camera has a body now.
                // Carrying damps the bob (a steady load reads as weight).
                const float bobScale = player.bobAmp * (player.holding ? 0.55f : 1.0f);
                glm::vec3 eye = physics.characterPosition() +
                                glm::vec3(0.0f, 0.65f - player.landDip, 0.0f);
                eye.y += std::sin(player.bobPhase * 2.0f) * 0.022f * bobScale;
                eye += right * (std::cos(player.bobPhase) * 0.012f * bobScale);
                if (cameraShake > 0.002f) {
                    eye += right * (std::sin(shakeClock * 71.0f) * 0.02f * cameraShake);
                    eye.y += std::sin(shakeClock * 93.0f + 1.3f) * 0.02f * cameraShake;
                }
                camera.position = eye;
                camera.target = eye + look;
                camera.fovYDegrees = 60.0f + 5.0f * sprintLean; // sprint opens the lens
                audio.setListener(eye, right);
                camEye = eye; // script-visible aim ray (forge.player.look)
                camLook = look;

                // QoL (week 7): light up the crosshair when the glove WOULD
                // grab — same raycast + dynamic-body test the click uses.
                aimEntity = forge::ecs::kNullEntity;
                if (simRun && !player.holding) {
                    if (const auto hit = physics.raycast(eye, look, 3.5f)) {
                        const auto it = bodyToEntity.find(hit->body.value);
                        auto* bc =
                            it != bodyToEntity.end() ? scene.tryGet<BodyC>(it->second) : nullptr;
                        aimGrabbable = bc != nullptr && bc->dynamic;
                        if (aimGrabbable) {
                            aimEntity = it->second; // glows in the draw list
                        }
                    }
                }

                // ---- Gravity glove: grab / carry / charge / launch / drop.
                // Week 12: the throw moved from press-edge to RELEASE-edge so
                // a held LMB charges. A press while holding arms the charge;
                // the release that ends the GRAB press must never throw.
                if (simRun && input.wasMousePressed(forge::MouseButton::Left)) {
                    if (player.holding) { // charge-press: throw arms on release
                        player.chargePress = true;
                        player.chargeTime = 0.0f;
                    } else if (const auto hit = physics.raycast(eye, look, 3.5f)) {
                        const auto it = bodyToEntity.find(hit->body.value);
                        auto* bc =
                            it != bodyToEntity.end() ? scene.tryGet<BodyC>(it->second) : nullptr;
                        if (bc != nullptr && bc->dynamic) {
                            player.held = hit->body;
                            player.holding = true;
                            audio.play("assets/sounds/grab.wav");
                            gloveClip = &gloveGrabClip; // wrist catch on the pull
                            gloveClipTime = 0.0f;
                            FORGE_INFO("glove: grabbed {}", scene.get<Name>(it->second).value);
                        } else {
                            FORGE_INFO("glove: hit body {} (not grabbable)", hit->body.value);
                        }
                    } else {
                        FORGE_INFO(
                            "glove: miss (eye {:.1f},{:.1f},{:.1f} look {:.2f},{:.2f},{:.2f})",
                            eye.x, eye.y, eye.z, look.x, look.y, look.z);
                    }
                }
                if (simRun && player.holding && player.chargePress) {
                    player.chargeTime += dt; // Lua maps this to throwSpeed
                }
                if (simRun && player.holding && player.chargePress &&
                    input.wasMouseReleased(forge::MouseButton::Left)) {
                    // Launch at the script-tuned speed. Recoil, sound and the
                    // clip amplitude all scale with how hard the shot was.
                    // The throw is a velocity ASSIGNMENT (mass-independent by
                    // construction), so mass is read back in via size: bigger
                    // held things leave the glove slower. The heavy cube
                    // (scale 1.2) launches at ~0.55x — a full charge barely
                    // clears R13's pit; a tap flops at its feet.
                    const float charge01 = (player.throwSpeed - 11.0f) / 14.0f;
                    float massFactor = 1.0f;
                    if (const auto itH = bodyToEntity.find(player.held.value);
                        itH != bodyToEntity.end()) {
                        const float sx = scene.get<TransformC>(itH->second).scale.x;
                        massFactor = std::clamp(0.5f / std::max(sx, 0.01f), 0.55f, 1.0f);
                    }
                    physics.setLinearVelocity(player.held,
                                              look * (player.throwSpeed * massFactor));
                    player.holding = false;
                    player.chargePress = false;
                    player.chargeTime = 0.0f;
                    player.throwSpeed = 11.0f; // failsafe: next tap is legacy
                    audio.play("assets/sounds/throw.wav", 0.7f + 0.5f * charge01);
                    sparks.burst(eye + look * 1.2f, 12 + static_cast<int>(20.0f * charge01));
                    gloveClip = &gloveThrowClip; // viewmodel punches with it
                    gloveClipTime = 0.0f;
                    gloveClipAmp = 0.5f + 1.1f * charge01; // tap nudges, full charge kicks
                }
                if (simRun && player.holding && input.wasMousePressed(forge::MouseButton::Right)) {
                    physics.setLinearVelocity(player.held, {0.0f, 0.0f, 0.0f}); // gentle drop
                    player.holding = false;
                    player.chargePress = false; // an armed charge dies with the drop
                    player.chargeTime = 0.0f;
                    player.throwSpeed = 11.0f;
                    audio.play("assets/sounds/grab.wav"); // same blip, release direction
                }
                if (simRun && player.holding) {
                    // Velocity spring: mass-independent, fights gravity for
                    // free, and held objects PUSH against obstacles instead
                    // of teleporting through them — that is the whole trick.
                    const glm::vec3 target = eye + look * 1.9f;
                    const glm::vec3 at = glm::vec3(physics.bodyTransform(player.held)[3]);
                    glm::vec3 springVel = (target - at) * 12.0f;
                    const float springLen = glm::length(springVel);
                    if (springLen > 14.0f) {
                        springVel *= 14.0f / springLen;
                    }
                    // Bugfix (prop-surfing): a held crate under your own feet
                    // lifted you with it — infinite flight over every wall.
                    // Standing on what you carry makes the glove let go.
                    const glm::vec3 feet = physics.characterPosition() - glm::vec3(0, 0.9f, 0);
                    const glm::vec3 d = at - feet;
                    const bool underFeet =
                        d.y < 0.1f && d.y > -1.0f && d.x * d.x + d.z * d.z < 0.55f * 0.55f;
                    // Bugfix (wedged carry): a crate stuck behind geometry far
                    // from the glove used to strain forever; it drops instead.
                    player.stuckTime =
                        glm::length(target - at) > 2.6f ? player.stuckTime + dt : 0.0f;
                    if (underFeet || player.stuckTime > 0.6f) {
                        physics.setLinearVelocity(player.held, {0.0f, 0.0f, 0.0f});
                        player.holding = false;
                        player.chargePress = false;
                        player.chargeTime = 0.0f;
                        player.throwSpeed = 11.0f;
                        player.stuckTime = 0.0f;
                        audio.play("assets/sounds/grab.wav", 0.7f, 0.8f);
                    } else {
                        physics.setLinearVelocity(player.held, springVel);
                    }
                } else {
                    player.stuckTime = 0.0f;
                }

                // ---- UNIT-7's glove rides the camera (week 11). Offsets are
                // camera-space (right/up/look); the throw/grab clips add their
                // sampled position + euler curves on top of the base pose.
                {
                    const glm::vec3 upv = glm::normalize(glm::cross(right, look));
                    const bool walking =
                        simRun && physics.characterGrounded() &&
                        (input.isKeyDown(forge::Key::W) || input.isKeyDown(forge::Key::A) ||
                         input.isKeyDown(forge::Key::S) || input.isKeyDown(forge::Key::D));
                    if (simRun) {
                        // Sway keeps a faint pulse at rest — a robot idles too.
                        gloveSwayTime += dt * (walking ? 1.0f : 0.35f);
                        gloveClipTime += dt;
                    }
                    const float swayAmp = walking ? 1.0f : 0.35f;
                    glm::vec3 off = player.holding
                                        ? glm::vec3(0.24f, -0.2f, 0.68f) // reach toward the load
                                        : glm::vec3(0.28f, -0.26f, 0.62f);
                    off.x += std::cos(gloveSwayTime * 3.6f) * 0.008f * swayAmp;
                    off.y += std::sin(gloveSwayTime * 7.2f) * 0.010f * swayAmp;
                    // Sprint lean (week 12 juice): the glove drops and rolls
                    // while sprinting — the whole launch pass reads heavier.
                    const bool sprinting = walking && input.isKeyDown(forge::Key::LeftShift);
                    sprintLean += ((sprinting ? 1.0f : 0.0f) - sprintLean) *
                                  std::min(dt * 8.0f, 1.0f);
                    off.y -= 0.05f * sprintLean;
                    // Charge pose (week 12): wrist coils back with the charge;
                    // at full power the glove trembles — the kinesthetic tell
                    // that pairs with the audio plateau + click.
                    const float charge01 = (player.throwSpeed - 11.0f) / 14.0f;
                    off.z -= 0.08f * charge01;
                    off.y += 0.02f * charge01;
                    if (charge01 > 0.99f) {
                        off.x += std::sin(gloveSwayTime * 90.0f) * 0.002f;
                        off.y += std::cos(gloveSwayTime * 73.0f) * 0.002f;
                    }
                    glm::vec3 clipEuler{0.0f};
                    if (gloveClip != nullptr) {
                        off += gloveClip->samplePosition(gloveClipTime, false) * gloveClipAmp;
                        clipEuler = gloveClip->sampleEuler(gloveClipTime, false) * gloveClipAmp;
                        if (gloveClipTime >= gloveClip->duration()) {
                            gloveClip = nullptr; // clips end at zero == idle pose
                            gloveClipAmp = 1.0f;
                        }
                    }
                    auto& gt = scene.get<TransformC>(gloveEntity);
                    gt.position = eye + right * off.x + upv * off.y + look * off.z;
                    // Resting pose leans a few degrees in and down — a hand at
                    // ease, not a rifle sight. Clip curves add on top; charge
                    // cocks the wrist up, sprint rolls the forearm in.
                    gt.eulerDeg = glm::vec3(player.pitch - 4.0f + 9.0f * charge01,
                                            -player.yaw - 10.0f, 8.0f * sprintLean) +
                                  clipEuler;
                }
            } else {
                // ---- Editor orbit camera (unchanged from P7).
                if (!(ui && ui->wantCaptureMouse())) {
                    if (input.isMouseDown(forge::MouseButton::Right)) {
                        camYaw += static_cast<float>(input.mouseDeltaX()) * 0.4f;
                        camPitch =
                            std::clamp(camPitch + static_cast<float>(input.mouseDeltaY()) * 0.4f,
                                       -85.0f, 85.0f);
                    }
                    camDist = std::clamp(
                        camDist * std::pow(0.92f, static_cast<float>(input.scrollDeltaY())), 2.0f,
                        30.0f);
                }
                const float yawR = glm::radians(camYaw);
                const float pitchR = glm::radians(camPitch);
                camera.position =
                    camTarget + camDist * glm::vec3(std::sin(yawR) * std::cos(pitchR),
                                                    std::sin(pitchR),
                                                    std::cos(yawR) * std::cos(pitchR));
                camera.target = camTarget;
            }

            if (ui) {
                ui->beginFrame();
                ImGuizmo::BeginFrame();

                if (playMode && gameState == GameState::Playing) {
                    // Minimal HUD: crosshair (filled while holding) + hints.
                    // Only the live game shows it; the menus own the screen.
                    const ImGuiIO& io = ImGui::GetIO();
                    ImDrawList* draw = ImGui::GetForegroundDrawList();
                    const ImVec2 center{io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};
                    if (reticleMode == "command" && reticleValue > 0.01f) {
                        // SPARK-blue flash: an F command was acknowledged.
                        const auto a = static_cast<int>(230.0f * reticleValue);
                        draw->AddCircle(center, 8.0f, IM_COL32(90, 170, 255, a), 0, 3.0f);
                    }
                    if (player.holding) {
                        draw->AddCircleFilled(center, 5.0f, IM_COL32(255, 220, 120, 230));
                        if (reticleMode == "default" && reticleValue > 0.01f) {
                            // Charge bloom: the ring grows with the charge and
                            // flares white at full power (the visual click).
                            const bool full = reticleValue >= 0.999f;
                            draw->AddCircle(center, 7.0f + 9.0f * reticleValue,
                                            full ? IM_COL32(255, 255, 255, 255)
                                                 : IM_COL32(255, 200, 90, 220),
                                            0, full ? 3.0f : 2.0f);
                        }
                    } else if (aimGrabbable) { // amber ring: the glove will grab
                        draw->AddCircle(center, 6.0f, IM_COL32(255, 220, 120, 240), 0, 2.5f);
                    } else {
                        draw->AddCircle(center, 5.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
                    }
                    // Per-room objective hint (scene.lua sets it), top-centred.
                    if (!hudHint.empty()) {
                        const ImVec2 sz = ImGui::CalcTextSize(hudHint.c_str());
                        const ImVec2 at{(io.DisplaySize.x - sz.x) * 0.5f, 34.0f};
                        draw->AddRectFilled({at.x - 8.0f, at.y - 4.0f},
                                            {at.x + sz.x + 8.0f, at.y + sz.y + 4.0f},
                                            IM_COL32(0, 0, 0, 90), 4.0f);
                        draw->AddText(at, IM_COL32(255, 255, 255, 205), hudHint.c_str());
                    }
                    // ---- ADR-031 HUD: fade (behind every HUD element), interact
                    // prompt under the crosshair, mission panel top-right, one
                    // subtitle line bottom-centre.
                    if (fadeAlpha > 0.001f) {
                        ImGui::GetBackgroundDrawList()->AddRectFilled(
                            {0.0f, 0.0f}, io.DisplaySize,
                            IM_COL32(0, 0, 0, static_cast<int>(255.0f * fadeAlpha)));
                    }
                    if (!hudPrompt.empty()) {
                        drawPrompt(draw, {center.x, center.y + 44.0f}, hudPrompt);
                    }
                    drawMissionPanel(draw, io.DisplaySize.x - 12.0f, 12.0f, missionTitle,
                                     missionLines);
                    if (sayLine.left > 0.0f) {
                        drawSayLine(draw, io.DisplaySize, sayLine.speaker, sayLine.text,
                                    std::clamp(sayLine.left / 0.4f, 0.0f, 1.0f));
                        sayLine.left -= simRun ? dt : 0.0f;
                    }
                    // Context-sensitive legend: the verbs that apply right now.
                    draw->AddText({12.0f, io.DisplaySize.y - 26.0f}, IM_COL32(255, 255, 255, 160),
                                  player.holding
                                      ? "LMB hold = CHARGE, release = THROW    RMB set down    "
                                        "F send SPARK to catch"
                                      : "WASD move  SHIFT sprint  SPACE jump  LMB grab  "
                                        "Q park drone  F send drone  E use  R respawn  "
                                        "ESC pause");
                    // Stat lines (script-owned: timer, data cores), top-left.
                    float statY = 12.0f;
                    for (const auto& [key, value] : stats) {
                        const std::string line = key + "  " + value;
                        draw->AddText({12.0f, statY}, IM_COL32(255, 255, 255, 150), line.c_str());
                        statY += 18.0f;
                    }
                    // Toasts: transient, centred under the hint, fade out.
                    float toastY = 66.0f;
                    for (auto& toast : toasts) {
                        const float alpha = std::clamp(toast.left / 0.4f, 0.0f, 1.0f);
                        const ImVec2 sz = ImGui::CalcTextSize(toast.text.c_str());
                        const ImVec2 at{(io.DisplaySize.x - sz.x) * 0.5f, toastY};
                        draw->AddRectFilled({at.x - 10.0f, at.y - 5.0f},
                                            {at.x + sz.x + 10.0f, at.y + sz.y + 5.0f},
                                            IM_COL32(10, 30, 50, static_cast<int>(150 * alpha)),
                                            4.0f);
                        draw->AddText(at, IM_COL32(150, 215, 255, static_cast<int>(240 * alpha)),
                                      toast.text.c_str());
                        toastY += sz.y + 14.0f;
                        toast.left -= simRun ? dt : 0.0f;
                    }
                    std::erase_if(toasts, [](const Toast& t) { return t.left <= 0.0f; });
                }

                // ---- Menu overlays (week 6). A centred window per non-playing
                // state. Buttons mirror the keyboard shortcuts so mouse works
                // too. Restart soft-resets the run via the Lua resetGame().
                if (playMode && gameState != GameState::Playing) {
                    const ImGuiIO& io = ImGui::GetIO();
                    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                                            ImGuiCond_Always, {0.5f, 0.5f});
                    ImGui::SetNextWindowSize({320.0f, 0.0f}, ImGuiCond_Always);
                    ImGui::Begin("##menu", nullptr,
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
                    const auto startRun = [&](bool reset) {
                        if (reset) {
                            script.runString("resetGame()");
                        }
                        gameState = GameState::Playing;
                        cursorForState();
                    };
                    if (gameState == GameState::Title) {
                        ImGui::TextUnformatted("VAULT");
                        ImGui::Separator();
                        ImGui::TextWrapped("A physics puzzle in seventeen rooms and the valley "
                                           "above them. Carry crates, charge the glove, ring "
                                           "the bells, ride the belts, bend the light - and "
                                           "trust your drone to catch what you throw.");
                        ImGui::Spacing();
                        if (ImGui::Button("Play", {-1.0f, 0.0f})) {
                            startRun(false);
                        }
                        // Saved progress (week 10): resume the run where the
                        // last session's furthest checkpoint left off.
                        if (settings.savedCheckpoint > 1 &&
                            ImGui::Button("Continue", {-1.0f, 0.0f})) {
                            startRun(false);
                            script.runString("resumeAt(" +
                                             std::to_string(settings.savedCheckpoint) + ")");
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled(settings.savedCheckpoint > 1
                                                ? "ENTER play   C continue   ESC quit"
                                                : "ENTER play   ESC quit");
                    } else if (gameState == GameState::Paused) {
                        ImGui::TextUnformatted("Paused");
                        ImGui::Separator();
                        if (ImGui::Button("Resume", {-1.0f, 0.0f})) {
                            gameState = GameState::Playing;
                            cursorForState();
                        }
                        if (ImGui::Button("Restart", {-1.0f, 0.0f})) {
                            startRun(true);
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        // Settings (week 10): the shippable three. Resolution/
                        // quality/rebinding are scope-cut with reasons (ADR-026).
                        ImGui::Spacing();
                        ImGui::TextDisabled("Settings");
                        ImGui::Separator();
                        bool changed = false;
                        changed |= ImGui::SliderFloat("sensitivity", &settings.mouseSensitivity,
                                                      0.05f, 0.35f, "%.2f");
                        changed |= ImGui::Checkbox("invert Y", &settings.invertY);
                        if (ImGui::SliderFloat("volume", &settings.masterVolume, 0.0f, 1.0f,
                                               "%.2f")) {
                            audio.setMasterVolume(settings.masterVolume);
                            changed = true;
                        }
                        if (changed) {
                            saveSettings(kSettingsPath, settings);
                        }
                        if (!missionTitle.empty()) { // ADR-031: the tracked mission
                            ImGui::Spacing();
                            ImGui::TextDisabled("Mission");
                            ImGui::Separator();
                            ImGui::TextUnformatted(missionTitle.c_str());
                            for (const std::string& line : missionLines) {
                                const bool done = line.rfind("[x] ", 0) == 0;
                                if (done) {
                                    ImGui::TextColored({0.45f, 0.8f, 0.5f, 0.8f}, "%s",
                                                       line.c_str());
                                } else {
                                    ImGui::TextUnformatted(line.c_str());
                                }
                            }
                        }
                        if (!stats.empty()) {
                            ImGui::Spacing();
                            ImGui::TextDisabled("This run");
                            ImGui::Separator();
                            for (const auto& [key, value] : stats) {
                                ImGui::Text("%s  %s", key.c_str(), value.c_str());
                            }
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("ESC resume");
                    } else { // Won
                        ImGui::TextUnformatted("SIGNAL SENT");
                        ImGui::Separator();
                        ImGui::TextWrapped("UNIT-7 climbed out of the vault and woke the "
                                           "relay. Thanks for playing.");
                        ImGui::Spacing();
                        for (const auto& [key, value] : stats) {
                            ImGui::Text("%s  %s", key.c_str(), value.c_str());
                        }
                        ImGui::Spacing();
                        if (ImGui::Button("Play again", {-1.0f, 0.0f})) {
                            startRun(true);
                        }
                        if (ImGui::Button("Quit", {-1.0f, 0.0f})) {
                            window.requestClose();
                        }
                        ImGui::Spacing();
                        ImGui::TextDisabled("ENTER play again   ESC quit");
                    }
                    ImGui::End();
                }

                if (!playMode) {
                    // ---- Scene panel: simulation control + hierarchy.
                    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({250.0f, 400.0f}, ImGuiCond_FirstUseEver);
                    ImGui::Begin("Scene");
                    ImGui::Text("%.1f fps (%.2f ms)", 1.0 / static_cast<double>(dt),
                                static_cast<double>(dt) * 1000.0);
                    ImGui::Checkbox("Pause", &paused);
                    ImGui::SameLine();
                    if (ImGui::Button("Step")) {
                        stepOnce = true;
                    }
                    ImGui::Separator();
                    scene.each<Name>([&](forge::ecs::Entity e, Name& name) {
                        ImGui::PushID(static_cast<int>(e.index));
                        if (ImGui::Selectable(name.value.c_str(), selected == e)) {
                            selected = e;
                        }
                        ImGui::PopID();
                    });
                    ImGui::Separator();
                    ImGui::TextDisabled("RMB drag: orbit / wheel: zoom");
                    ImGui::TextDisabled("E: box rain / SPACE: kick (Lua)");
                    ImGui::End();

                    // ---- Inspector panel: the selected entity's components.
                    ImGui::SetNextWindowPos({1280.0f - 292.0f, 12.0f}, ImGuiCond_FirstUseEver);
                    ImGui::SetNextWindowSize({280.0f, 300.0f}, ImGuiCond_FirstUseEver);
                    ImGui::Begin("Inspector");
                    if (!scene.alive(selected)) {
                        ImGui::TextDisabled("select an entity in Scene");
                    } else {
                        auto& name = scene.get<Name>(selected);
                        char nameBuf[64]{};
                        std::snprintf(nameBuf, sizeof(nameBuf), "%s", name.value.c_str());
                        if (ImGui::InputText("name", nameBuf, sizeof(nameBuf))) {
                            entityByName.erase(name.value);
                            name.value = nameBuf;
                            entityByName[name.value] = selected;
                        }
                        auto& t = scene.get<TransformC>(selected);
                        auto* body = scene.tryGet<BodyC>(selected);
                        if (body != nullptr && (body->dynamic || body->kinematic)) {
                            // Physics owns this transform: edit = teleport.
                            glm::vec3 p = glm::vec3(physics.bodyTransform(body->id)[3]);
                            if (ImGui::DragFloat3("position", &p.x, 0.05f)) {
                                physics.teleport(body->id, p);
                            }
                            ImGui::TextDisabled("rotation/scale: physics-driven");
                            if (ImGui::Button("Kick")) {
                                physics.addLinearVelocity(body->id, {0.6f, 6.0f, 0.4f});
                            }
                        } else {
                            const bool moved = ImGui::DragFloat3("position", &t.position.x, 0.05f);
                            ImGui::DragFloat3("rotation", &t.eulerDeg.x, 0.5f);
                            ImGui::DragFloat3("scale", &t.scale.x, 0.02f);
                            if (moved && body != nullptr) {
                                physics.teleport(body->id, t.position); // collider follows
                            }
                        }
                    }
                    ImGui::End();

                    // ---- Translate gizmo on the selection (P7.3). ImGuizmo
                    // flips Y itself (expects GL-style NDC), so it gets the
                    // UNFLIPPED projection — hand it ours and everything drags
                    // upside-down. Same fov/near/far the renderer uses.
                    if (scene.alive(selected)) {
                        const ImGuiIO& io = ImGui::GetIO();
                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
                        const glm::mat4 view =
                            glm::lookAt(camera.position, camera.target, glm::vec3(0, 1, 0));
                        const glm::mat4 proj = glm::perspective(glm::radians(camera.fovYDegrees),
                                                                io.DisplaySize.x / io.DisplaySize.y,
                                                                camera.nearPlane, camera.farPlane);

                        auto& t = scene.get<TransformC>(selected);
                        auto* body = scene.tryGet<BodyC>(selected);
                        glm::mat4 model = (body != nullptr && (body->dynamic || body->kinematic))
                                              ? physics.bodyTransform(body->id) *
                                                    glm::scale(glm::mat4(1.0f), t.scale)
                                              : trs(t);
                        if (ImGuizmo::Manipulate(&view[0][0], &proj[0][0], ImGuizmo::TRANSLATE,
                                                 ImGuizmo::WORLD, &model[0][0])) {
                            const glm::vec3 newPosition{model[3]};
                            t.position = newPosition;
                            if (body != nullptr) {
                                physics.teleport(body->id, newPosition);
                            }
                        }
                    }
                } // !playMode (editor panels + gizmo)
            }

            // Simulation control: simRun freezes gameplay+physics behind a
            // menu (or the editor's Pause). Step runs exactly one 60 Hz tick.
            // Typing in the UI suppresses gameplay input (the script polls keys).
            const bool typing = ui && ui->wantCaptureKeyboard();
            if (simRun) {
                if (!typing) {
                    script.update(dt); // gameplay decides first...
                }
                physics.update(dt); // ...then the world reacts
            } else if (stepOnce) {
                stepOnce = false;
                physics.update(1.0f / 60.0f);
            }
            audio.update(); // reap finished one-shot voices (loops persist)

            // Keyframe animation drives the platform; the collider rides
            // along via teleport (a kinematic body is the proper follow-up,
            // noted in ADR-020). A frozen sim freezes animation too.
            if (simRun || stepOnce) {
                animTime += dt;
            }
            if (scene.alive(platformEntity)) {
                auto& pt = scene.get<TransformC>(platformEntity);
                pt.position = platformClip.samplePosition(animTime);
                if (auto* pb = scene.tryGet<BodyC>(platformEntity)) {
                    physics.moveKinematic(pb->id, pt.position);
                }
            }
            sparks.update(simRun ? dt : 0.0f);
            // Skeletal animation advances with the sim (frozen behind menus),
            // after the script had its say this frame.
            if (simRun) {
                scene.each<AnimatorC>(
                    [&](forge::ecs::Entity, AnimatorC& a) { a.animator.update(dt); });
            }

            if (renderer && now - lastPoll > std::chrono::milliseconds(500)) {
                lastPoll = now;
                crateWatch.poll([&](const std::string& path) {
                    const auto img = forge::assets::loadImage(path);
                    renderer->updateTexture(crateTexture, img.width, img.height, img.rgba);
                });
                torusWatch.poll([&](const std::string& path) {
                    const auto data = forge::assets::loadMesh(path);
                    renderer->updateMesh(torusMesh, data.vertices, data.indices);
                });
                scriptWatch.poll([&](const std::string& path) {
                    if (script.runFile(path)) {
                        FORGE_INFO("script hot-reloaded: {}", path);
                    }
                });
                bool moduleChanged = false;
                for (FileWatcher& w : includeWatches) {
                    w.poll([&](const std::string& path) {
                        FORGE_INFO("script module changed: {}", path);
                        moduleChanged = true;
                    });
                }
                if (moduleChanged && script.runFile(mainScript)) {
                    FORGE_INFO("script hot-reloaded: {} (module change)", mainScript);
                }
                syncIncludeWatches();
            }
            // Dev console (FORGE_DEV): drop Lua into build/_cmd.lua and it runs
            // once — teleports, state dumps, scripted checks without a GUI.
            if (devMode && now - lastDevPoll > std::chrono::milliseconds(200)) {
                lastDevPoll = now;
                const std::filesystem::path cmdPath{"build/_cmd.lua"};
                std::error_code ec;
                if (std::filesystem::exists(cmdPath, ec)) {
                    std::stringstream code;
                    code << std::ifstream(cmdPath).rdbuf();
                    std::filesystem::remove(cmdPath, ec);
                    FORGE_INFO("dev console: running {} bytes", code.str().size());
                    script.runString(code.str());
                }
            }

            if (renderer) {
                // The frame's draw list, straight from the scene: physics
                // owns dynamic transforms, TransformC owns everything else.
                std::vector<forge::DrawItem> items;
                // Every animated entity's palette, back to back (ADR-030).
                std::vector<glm::mat4> joints;
                scene.each<TransformC, MeshRendererC>([&](forge::ecs::Entity e, TransformC& t,
                                                          MeshRendererC& mr) {
                    glm::mat4 model;
                    if (auto* body = scene.tryGet<BodyC>(e);
                        body != nullptr && (body->dynamic || body->kinematic)) {
                        model =
                            physics.bodyTransform(body->id) * glm::scale(glm::mat4(1.0f), t.scale);
                    } else {
                        model = trs(t);
                    }
                    glm::vec3 emissive = mr.emissive;
                    if (e == aimEntity) {
                        emissive += glm::vec3(0.30f, 0.19f, 0.05f); // "the glove will grab"
                    }
                    uint32_t jointOffset = forge::DrawItem::kNoJoints;
                    if (const auto* a = scene.tryGet<AnimatorC>(e); a != nullptr) {
                        if (joints.size() + a->animator.jointCount() <=
                            forge::Renderer::kMaxJoints) {
                            jointOffset = static_cast<uint32_t>(joints.size());
                            a->animator.appendPalette(joints);
                        } else if (!jointBudgetWarned) {
                            jointBudgetWarned = true; // the rest draw in bind pose
                            FORGE_WARN("joint palette full ({} matrices): extra animated "
                                       "entities draw in bind pose",
                                       forge::Renderer::kMaxJoints);
                        }
                    }
                    if (mr.model < models.size()) {
                        forge::appendModelDraws(
                            items, models[mr.model], model,
                            forge::ModelLook{mr.texture, mr.textureOverride, emissive, mr.tint,
                                             mr.roughness, mr.metallic},
                            jointOffset);
                    }
                });
                // Local lights: nearest-first to the camera, capped.
                std::vector<forge::PointLight> frameLights;
                frameLights.reserve(lights.size());
                for (const auto& [id, light] : lights) {
                    frameLights.push_back(light);
                }
                const auto distSq = [&](const forge::PointLight& l) {
                    const glm::vec3 d = l.position - camera.position;
                    return glm::dot(d, d);
                };
                if (frameLights.size() > forge::Renderer::kMaxPointLights) {
                    std::partial_sort(
                        frameLights.begin(),
                        frameLights.begin() + forge::Renderer::kMaxPointLights, frameLights.end(),
                        [&](const forge::PointLight& a, const forge::PointLight& b) {
                            return distSq(a) < distSq(b);
                        });
                    frameLights.resize(forge::Renderer::kMaxPointLights);
                }
                renderer->setPointLights(frameLights);
                sparks.appendDrawItems(items, cubeMesh, crateTexture);
                renderer->drawFrame(camera, items, joints);
            }
        }
    } catch (const std::exception& e) {
        FORGE_ERROR("fatal: {}", e.what());
        return 1;
    }

    FORGE_INFO("clean shutdown");
    return 0;
}
