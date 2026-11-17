// engine/src/renderer/Renderer.cpp
#include "RendererInternal.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanCommon.hpp" // volk first: gives glfw3.h the Vulkan types below
#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"
#include "VulkanTexture.hpp"

#include "forge/core/Log.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Renderer.hpp"
#include "forge/renderer/VulkanContext.hpp"

// Second (and last) TU that includes GLFW, for exactly one call:
// glfwCreateWindowSurface. Documented as the ADR-004 exception in ADR-010.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "forge_gen/MeshShaders.hpp" // generated from checked-in .spv (ADR-011)

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace forge {

namespace {

// CPU records frame N+1 while the GPU draws frame N. Two is the sweet spot:
// three adds latency, one serializes CPU and GPU.
constexpr uint32_t kFramesInFlight = 2;

// Directional shadows (ADR-014, cascaded since ADR-033): one key light, three
// cascades in the layers of one D32 array image, each kShadowMapSize square.
// Each cascade is a sphere around its slice of the view frustum (radius
// depends only on fov/aspect/splits, so the map size in world units never
// changes as the camera turns) snapped to whole texels in light space (so
// it never swims as the camera moves).
constexpr uint32_t kShadowMapSize = 2048;
constexpr uint32_t kShadowCascades = 3; // must match kCascades in mesh.frag
// Shadows reach kShadowFar metres (fading out from kShadowFadeStart), or
// the camera's far plane if that is nearer. The Surface wants >= 60 m.
constexpr float kShadowFar = 72.0f;
constexpr float kShadowFadeStart = 60.0f;
// Practical split scheme: lambda blends logarithmic (1) and uniform (0)
// splits. 0.6 gives ~10 / 24 / 72 m: cascade 0 texels ~1.2 cm, finer than
// the old single 32 m map's 1.6 cm, so rooms keep their crisp shadows.
constexpr float kCascadeSplitLambda = 0.6f;
// Casters up to this far TOWARD the light beyond a cascade's sphere still
// land in its map (a 30 m tower under a 35-degree sun is ~55 m up-ray).
constexpr float kCasterReach = 120.0f;
// mesh.frag: cross-fade band at a cascade's map edge (fraction of the map),
// and the normal-offset bias in texels of the cascade sampled.
constexpr float kCascadeBlendBand = 0.06f;
constexpr float kShadowNormalOffsetTexels = 1.0f;

// Scene color target: linear, scene-referred HDR. Half-float keeps emissive
// values far above 1.0 intact for bloom and the tone mapper. (Color
// attachment + blend + linear filtering on this format are spec-mandatory.)
constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Bloom: a mip chain starting at half resolution. Six levels reach a blur
// radius of roughly a tenth of the screen at 720p; fewer are used when the
// window is too small to hold them.
constexpr uint32_t kMaxBloomMips = 6;
constexpr float kBloomThreshold = 1.0f; // scene-referred brightness where bloom starts
constexpr float kBloomKnee = 0.5f;      // soft-knee half-width around the threshold
// The chain is summed and averaged over its mips, which leaves thin emitters
// (most of whose energy lives in the first mips) faint; this gain is tuned so
// setBloom(0.6) gives a 3-pixel, radiance-6 laser beam a clear soft halo.
constexpr float kBloomGain = 2.0f;


// Hard cap on live textures — sizes the descriptor pool. Raised from 64 for
// imported model packs (ADR-030); exhaustion falls back to white, logged.
// Pool growth (or bindless) is the next step if packs outgrow it.
constexpr uint32_t kMaxTextures = Renderer::kMaxTextures;

// Per-frame GPU data (ADR-012). Layout mirrors the std140 block in
// mesh.vert/frag: mat4s then vec4s — nothing that std140 would repack.
// The vertex shaders declare only the leading members (through lightColor);
// mesh.frag declares the whole block.
struct FrameUbo {
    glm::mat4 viewProj;
    glm::mat4 lightViewProj; // world -> light clip (shadow render AND sample)
    glm::vec4 cameraPos;
    glm::vec4 lightDirection; // xyz: surface -> light, normalized
    glm::vec4 lightColor;     // rgb * intensity
    glm::vec4 fogColor;       // rgb linear, a: density per meter (0 = off)
    glm::vec4 ambientSky;     // hemisphere ambient, normal pointing up
    glm::vec4 ambientGround;  // hemisphere ambient, normal pointing down
    glm::vec4 counts;         // x: active point lights
    glm::vec4 pointPosRadius[Renderer::kMaxPointLights]; // xyz world pos, w radius
    glm::vec4 pointColor[Renderer::kMaxPointLights];     // rgb * HDR intensity
    // Sky tail (ADR-030): read only by sky.frag, so mesh.frag's prefix
    // declaration stays valid.
    glm::mat4 invViewProj; // clip -> world, for sky view rays
    glm::vec4 skyZenith;   // rgb
    glm::vec4 skyHorizon;  // rgb, a: panorama yaw (radians)
    glm::vec4 skyGround;   // rgb
    glm::vec4 skySun;      // rgb: disc radiance, a: cos(angular radius), 2 = no disc
    glm::vec4 skyParams;   // x: panorama on, y: its intensity, z: fog distance, w: halo
    // Cascade tail (ADR-033): read only by mesh.frag, which declares the
    // whole block. The shadow pass itself takes its matrix per draw.
    glm::mat4 cascadeViewProj[kShadowCascades]; // world -> cascade light clip
    glm::vec4 cascadeTexel; // xyz: world size of one texel per cascade
    glm::vec4 shadowParams; // x: fade start, y: fade end (m), z: blend band, w: normal offset
};
static_assert(sizeof(FrameUbo) ==
                  240 + 32 * Renderer::kMaxPointLights + 144 + 64 * kShadowCascades + 32,
              "must match the std140 blocks in mesh.frag / sky.frag");

// Per-draw push constants (ADR-012): 112 of the 128 bytes Vulkan guarantees.
// The static vertex stages read only `model`; mesh.frag reads emissive +
// tint (their w channels carry roughness + metallic, ADR-033); the skinned
// vertex stages (ADR-030) also read skin.x, the first joint of the draw's
// palette slice. In the shadow pass `model` is cascade view-proj * model.
struct DrawPush {
    glm::mat4 model;
    glm::vec4 emissive; // rgb: linear HDR radiance added after lighting, a: roughness
    glm::vec4 tint;     // rgb: albedo multiplier, a: metallic
    glm::uvec4 skin;    // x: joint offset (skinned pipelines only)
};
static_assert(sizeof(DrawPush) == 112, "must match the push_constant blocks (mesh*.vert/frag)");
constexpr VkShaderStageFlags kDrawPushStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Push block shared by the post shaders (bloom_down/up, composite): one vec4
// whose meaning is documented per shader.
struct PostPush {
    glm::vec4 params;
};

// Hemisphere ambient (mesh.frag): a small neutral floor plus a fraction of
// the room's key-light color, so the per-room mood set by setLighting() also
// tints the shadows. Ground = dimmer, slightly warm "bounce".
// Levels are tuned for the ACES curve, whose toe crushes far harder than the
// Reinhard it replaced: these keep key-light shadows about where they were.
constexpr glm::vec3 kAmbientSkyBase{0.040f, 0.046f, 0.060f};
constexpr float kAmbientSkyFromKey = 0.028f;
constexpr glm::vec3 kAmbientGroundBase{0.020f, 0.018f, 0.016f};
constexpr float kAmbientGroundFromKey = 0.012f;

// The REAL pixel size right now (HiDPI-safe, unlike the cached WindowDesc
// size). {0,0} while minimized — the caller must skip the frame.
VkExtent2D framebufferExtent(const Window& window) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window.nativeHandle(), &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

VkShaderModule makeShaderModule(VkDevice device, const uint32_t* words, size_t byteSize) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = byteSize;
    info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule failed");
    }
    return module;
}

VkSemaphore makeSemaphore(VkDevice device) {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    if (vkCreateSemaphore(device, &info, nullptr, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSemaphore failed");
    }
    return semaphore;
}

// D32 is universal on desktop; the fallbacks carry a stencil we don't use yet.
VkFormat findDepthFormat(VkPhysicalDevice gpu) {
    for (const VkFormat format :
         {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    throw std::runtime_error("no supported depth format");
}

bool formatHasStencil(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

// Device-local 2D image + memory (depth buffer, shadow map). Throws on failure.
void allocateImage(const VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format,
                   VkImageUsageFlags usage, VkImage& outImage, VkDeviceMemory& outMemory,
                   uint32_t mipLevels = 1, uint32_t arrayLayers = 1) {
    const VkDevice dev = device.device();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(dev, outImage, &requirements);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device.physical(), &memProps);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        const bool allowed = (requirements.memoryTypeBits & (1u << i)) != 0;
        const bool local =
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (allowed && local) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) {
        throw std::runtime_error("no device-local memory for image");
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(dev, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyImage(dev, outImage, nullptr);
        throw std::runtime_error("vkAllocateMemory (image) failed");
    }
    vkBindImageMemory(dev, outImage, outMemory, 0);
}

// Whole-image transition; the mip-aware version lives in vkutil (shared with
// VulkanTexture's mip-generation cascade).
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
                     VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess) {
    vkutil::transitionImage(cmd, image, aspect, 0, VK_REMAINING_MIP_LEVELS, from, to, srcStage,
                            srcAccess, dstStage, dstAccess);
}

// The engine's built-in placeholder albedo (ADR-013): an 8x8 checker in warm
// orange / off-white, generated at startup. stb_image + real files are P5's
// job; the sampling machinery is identical either way.
std::vector<uint8_t> makeCheckerTexels(uint32_t size, uint32_t checkSize) {
    std::vector<uint8_t> texels(static_cast<size_t>(size) * size * 4);
    constexpr uint8_t kA[4] = {235, 137, 66, 255};  // warm orange (sRGB)
    constexpr uint8_t kB[4] = {240, 236, 228, 255}; // off-white (sRGB)
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool odd = (((x / checkSize) + (y / checkSize)) & 1u) != 0;
            const uint8_t* c = odd ? kA : kB;
            uint8_t* dst = &texels[(static_cast<size_t>(y) * size + x) * 4];
            dst[0] = c[0];
            dst[1] = c[1];
            dst[2] = c[2];
            dst[3] = c[3];
        }
    }
    return texels;
}

// One shadow cascade for the frame (ADR-033). `viewProj` renders AND
// samples it; the light-space box (centre +- radius in x/y, zBottom..zTop
// in light-view z, larger z = nearer the light) also culls its casters.
struct Cascade {
    glm::mat4 viewProj{1.0f};
    glm::vec3 center{0.0f}; // light-view space, x/y snapped to whole texels
    float radius = 0.0f;
    float zTop = 0.0f;
    float zBottom = 0.0f;
    float texel = 0.0f; // world size of one shadow texel
};

// a * b on raw floats: runs once per shadow draw, and glm's operators are
// nested function calls in the debug build the game is tested in.
glm::mat4 multiplyRaw(const glm::mat4& a, const glm::mat4& b) {
    glm::mat4 out;
    const float* pa = glm::value_ptr(a);
    const float* pb = glm::value_ptr(b);
    float* po = glm::value_ptr(out);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            po[c * 4 + r] = pa[r] * pb[c * 4] + pa[4 + r] * pb[c * 4 + 1] +
                            pa[8 + r] * pb[c * 4 + 2] + pa[12 + r] * pb[c * 4 + 3];
        }
    }
    return out;
}

// Rotation-only light view looking along -dir from the origin: fixed for a
// given light direction, so a texel grid in its space ignores the camera.
glm::mat4 lightRotation(glm::vec3 dir) {
    const glm::vec3 up =
        std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(glm::vec3(0.0f), -dir, up);
}

// Bounding sphere of the view-frustum slice between view depths n and f.
// The slice is symmetric about the view axis, so the centre lies on it, at
// the depth equidistant from a near and a far corner — clamped to the far
// plane when the far cross-section alone is wider (wide FOVs). k = the
// cross-section's half-diagonal per metre of depth. Returns {depth, radius}.
std::pair<float, float> sliceSphere(float n, float f, float k) {
    const float depth = std::min((f + n) * (1.0f + k * k) * 0.5f, f);
    const float radius = std::sqrt((f - depth) * (f - depth) + k * k * f * f);
    return {depth, radius};
}

// Fits cascade `c` around the slice [n, f] of the camera's frustum.
Cascade fitCascade(const glm::mat4& lightRot, glm::vec3 camPos, glm::vec3 camForward, float n,
                   float f, float k) {
    const auto [depth, sphereRadius] = sliceSphere(n, f, k);
    // Quantize the radius UP to 1/8 of its power of two: the sprint FOV
    // change then re-grids a cascade once or twice instead of every frame.
    const float step = std::exp2(std::floor(std::log2(std::max(sphereRadius, 1e-3f))) - 3.0f);
    Cascade out;
    out.radius = std::ceil(sphereRadius / step) * step;
    out.texel = 2.0f * out.radius / static_cast<float>(kShadowMapSize);
    out.center = glm::vec3(lightRot * glm::vec4(camPos + camForward * depth, 1.0f));
    out.center.x = std::floor(out.center.x / out.texel) * out.texel;
    out.center.y = std::floor(out.center.y / out.texel) * out.texel;
    out.zTop = out.center.z + out.radius + kCasterReach;
    out.zBottom = out.center.z - out.radius;
    // glm::ortho (RH, zero-to-one): view z = -near -> 0, -far -> 1.
    const glm::mat4 proj =
        glm::ortho(out.center.x - out.radius, out.center.x + out.radius,
                   out.center.y - out.radius, out.center.y + out.radius, -out.zTop, -out.zBottom);
    out.viewProj = proj * lightRot;
    return out;
}

} // namespace

struct Renderer::Impl {
    Window& window;
    VulkanContext& context;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::unique_ptr<VulkanDevice> device;
    std::unique_ptr<VulkanSwapchain> swapchain;

    // Depth buffer: ONE image shared by all frames in flight — the per-frame
    // barrier in record() serializes access, so N copies buy nothing.
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    // Descriptors split by UPDATE FREQUENCY (ADR-017): set 0 = per-frame
    // (UBO + shadow map), set 1 = per-material (albedo).
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> frameSets{};
    std::array<GpuBuffer, kFramesInFlight> frameUbos;
    // Joint palettes (ADR-030): per frame in flight, persistently mapped,
    // set 0 binding 2 — same fence-guarded ownership as the frame UBO.
    std::array<GpuBuffer, kFramesInFlight> jointBuffers;
    uint32_t jointCount = 0; // matrices uploaded for the frame being recorded

    // GPU resource tables; handles are indices. Entry 0 of textures is the
    // built-in checker placeholder (ADR-013).
    struct MeshEntry {
        GpuBuffer vertexBuffer;
        GpuBuffer indexBuffer;
        GpuBuffer skinBuffer; // SkinVertex stream; empty for static meshes
        uint32_t indexCount = 0;
        uint32_t jointSpan = 0; // highest joint index referenced + 1 (skinned only)
        // Model-space bounding sphere (xyz centre, w radius) of the vertices,
        // for per-cascade caster culling (ADR-033). w < 0 = never culled.
        glm::vec4 bounds{0.0f, 0.0f, 0.0f, -1.0f};
    };
    std::vector<MeshEntry> meshes;
    struct TextureEntry {
        std::unique_ptr<VulkanTexture> texture;
        VkDescriptorSet set = VK_NULL_HANDLE; // per-material set (set 1)
    };
    std::vector<TextureEntry> textures;

    // Directional shadow map (ADR-014): persistent, NOT tied to the swapchain.
    // One layer per cascade (ADR-033): the array view is sampled, the
    // per-layer views are the depth attachments.
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE; // 2D_ARRAY, all cascades
    std::array<VkImageView, kShadowCascades> shadowLayerViews{};
    VkSampler shadowSampler = VK_NULL_HANDLE; // comparison sampler (PCF)
    // This frame's cascades, fitted in drawFrame and read by record().
    std::array<Cascade, kShadowCascades> cascades{};
    glm::mat4 lightRot{1.0f};
    // Light-space AABB of the camera frustum out to the shadow distance:
    // every receiver that can show a shadow lies inside it (caster culling).
    struct {
        glm::vec3 lo{0.0f};
        glm::vec3 hi{0.0f};
    } shadowReceivers;

    // HDR scene target (swapchain-sized, RGBA16F): the mesh pass renders
    // here; the composite pass tone-maps it into the swapchain image. ONE
    // image shared by all frames in flight, same reasoning as the depth buffer.
    VkImage hdrImage = VK_NULL_HANDLE;
    VkDeviceMemory hdrMemory = VK_NULL_HANDLE;
    VkImageView hdrView = VK_NULL_HANDLE;

    // Bloom chain: one RGBA16F image whose mip 0 is half the swapchain size.
    // Each mip has its own view (render target AND sample source) and its
    // own descriptor set (as a source).
    VkImage bloomImage = VK_NULL_HANDLE;
    VkDeviceMemory bloomMemory = VK_NULL_HANDLE;
    std::vector<VkImageView> bloomViews;
    std::vector<VkExtent2D> bloomExtents;
    std::vector<VkDescriptorSet> bloomSets; // [i] samples bloom mip i

    // Post chain plumbing. Sets here reference swapchain-sized targets shared
    // by all frames in flight, so they are (re)written only under waitIdle.
    VkSampler postSampler = VK_NULL_HANDLE;               // linear, clamp-to-edge
    VkDescriptorSetLayout postSetLayout = VK_NULL_HANDLE; // 2 combined image samplers
    VkDescriptorPool postPool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet = VK_NULL_HANDLE;     // HDR scene (bloom prefilter source)
    VkDescriptorSet compositeSet = VK_NULL_HANDLE; // HDR scene + bloom mip 0
    VkPipelineLayout postLayout = VK_NULL_HANDLE;
    VkPipeline bloomDownPipeline = VK_NULL_HANDLE;
    VkPipeline bloomUpPipeline = VK_NULL_HANDLE;   // additive blend
    VkPipeline compositePipeline = VK_NULL_HANDLE; // -> swapchain format

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE; // depth-only, light's POV
    VkPipeline skinnedPipeline = VK_NULL_HANDLE;       // ADR-030: + SkinVertex stream
    VkPipeline skinnedShadowPipeline = VK_NULL_HANDLE; // ...and its shadow twin
    VkPipeline skyPipeline = VK_NULL_HANDLE;           // ADR-030: fullscreen, behind all
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Per frame-in-flight: recording + acquire sync.
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    // Per swapchain IMAGE (not per frame): present waits on the semaphore
    // tied to the image being presented — sidesteps semaphore-reuse hazards.
    std::vector<VkSemaphore> renderFinished;

    uint32_t frameIndex = 0;
    bool needRecreate = false;
    bool textureCapWarned = false;
    internal::UiRenderHook* uiHook = nullptr; // borrowed; EditorUi outlives its use

    // Per-room lighting (week 5): app-settable key light. Defaults match the
    // long-standing warm facility key so scenes that never call setLighting()
    // look exactly as before. Direction is surface -> light.
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.35f));
    glm::vec3 lightColor{3.0f, 2.9f, 2.7f}; // rgb * HDR intensity

    // Local lights / fog / post settings: app-settable, persist until changed.
    std::array<PointLight, Renderer::kMaxPointLights> pointLights{};
    uint32_t pointLightCount = 0;
    glm::vec3 fogColor{0.0f};
    float fogDensity = 0.0f; // per meter; 0 = off
    float exposure = 1.0f;
    float bloomIntensity = 0.6f;
    Sky sky; // ADR-030; defaults = the indoor facility sky
    // Ambient override (ADR-033): weight 0 = the key-derived default.
    glm::vec3 ambientSky{0.0f};
    glm::vec3 ambientGround{0.0f};
    float ambientWeight = 0.0f;

    Impl(Window& win, VulkanContext& ctx) : window(win), context(ctx) {}

    void createDepthResources();
    void destroyDepthResources();
    void createShadowResources();
    void createDescriptors();
    void createPipelineLayout();
    [[nodiscard]] VkPipeline makeScenePipeline(bool skinned) const;
    [[nodiscard]] VkPipeline makeShadowPipeline(bool skinned) const;
    [[nodiscard]] VkPipeline makeSkyPipeline() const;
    void createPostResources();
    void destroyPostResources();
    void createPostTargets();
    void destroyPostTargets();
    [[nodiscard]] VkPipeline makePostPipeline(const uint32_t* fragWords, size_t fragBytes,
                                              VkFormat colorFormat, bool additive) const;
    void createFrameObjects();
    void createImageSemaphores();
    void recreateSwapchain();
    void writeMaterialSet(TextureEntry& entry) const;
    void buildMeshEntry(std::span<const Vertex> vertices, std::span<const uint32_t> indices,
                        std::span<const SkinVertex> skin, MeshEntry& out) const;
    void recordBloom(VkCommandBuffer cmd) const;
    void record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frame,
                std::span<const DrawItem> items) const;
};

void Renderer::Impl::createDepthResources() {
    const VkDevice dev = device->device();
    depthFormat = findDepthFormat(device->physical());
    allocateImage(*device, swapchain->extent().width, swapchain->extent().height, depthFormat,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthMemory);

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (formatHasStencil(depthFormat)) {
        aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &viewInfo, nullptr, &depthView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (depth) failed");
    }
}

void Renderer::Impl::createShadowResources() {
    const VkDevice dev = device->device();
    // D32 unconditionally: findDepthFormat proved depth support exists, and
    // every desktop GPU we admit (1.3+) samples D32.
    allocateImage(*device, kShadowMapSize, kShadowMapSize, VK_FORMAT_D32_SFLOAT,
                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  shadowImage, shadowMemory, 1, kShadowCascades);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades};
    if (vkCreateImageView(dev, &viewInfo, nullptr, &shadowView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (shadow) failed");
    }
    // One single-layer view per cascade: what each cascade's pass renders to.
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    for (uint32_t c = 0; c < kShadowCascades; ++c) {
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, c, 1};
        if (vkCreateImageView(dev, &viewInfo, nullptr, &shadowLayerViews[c]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView (shadow cascade) failed");
        }
    }

    // Comparison sampler: LINEAR + compare = hardware 2x2 PCF per tap.
    // CLAMP_TO_BORDER with a WHITE border reads "beyond the map = lit".
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; // lit if ref <= stored
    if (vkCreateSampler(dev, &samplerInfo, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (shadow) failed");
    }
}

void Renderer::Impl::destroyDepthResources() {
    const VkDevice dev = device->device();
    vkDestroyImageView(dev, depthView, nullptr);
    vkDestroyImage(dev, depthImage, nullptr);
    vkFreeMemory(dev, depthMemory, nullptr);
    depthView = VK_NULL_HANDLE;
    depthImage = VK_NULL_HANDLE;
    depthMemory = VK_NULL_HANDLE;
}

void Renderer::Impl::createDescriptors() {
    const VkDevice dev = device->device();

    // SET 0 (per-frame): binding 0 UBO, binding 1 shadow map, binding 2 the
    // joint palette storage buffer (skinned vertex stages, ADR-030).
    VkDescriptorSetLayoutBinding frameBindings[3]{};
    frameBindings[0].binding = 0;
    frameBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBindings[0].descriptorCount = 1;
    frameBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    frameBindings[1].binding = 1;
    frameBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    frameBindings[1].descriptorCount = 1;
    frameBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    frameBindings[2].binding = 2;
    frameBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    frameBindings[2].descriptorCount = 1;
    frameBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
    frameLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    frameLayoutInfo.bindingCount = 3;
    frameLayoutInfo.pBindings = frameBindings;
    if (vkCreateDescriptorSetLayout(dev, &frameLayoutInfo, nullptr, &frameSetLayout) !=
        VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (frame) failed");
    }

    // SET 1 (per-material): binding 0 albedo.
    VkDescriptorSetLayoutBinding materialBinding{};
    materialBinding.binding = 0;
    materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBinding.descriptorCount = 1;
    materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = 1;
    materialLayoutInfo.pBindings = &materialBinding;
    if (vkCreateDescriptorSetLayout(dev, &materialLayoutInfo, nullptr, &materialSetLayout) !=
        VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (material) failed");
    }

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight + kMaxTextures};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight + kMaxTextures;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
    layouts.fill(frameSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = kFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(dev, &allocInfo, frameSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets failed");
    }

    // One persistently-mapped UBO per frame in flight, so the CPU never
    // writes a buffer the GPU is still reading (the fence guards the slot).
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameUbos[i] =
            GpuBuffer(*device, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDescriptorBufferInfo bufferInfo{frameUbos[i].handle(), 0, sizeof(FrameUbo)};
        VkDescriptorImageInfo shadowInfo{shadowSampler, shadowView,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        constexpr VkDeviceSize kJointBytes = sizeof(glm::mat4) * Renderer::kMaxJoints;
        jointBuffers[i] =
            GpuBuffer(*device, kJointBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDescriptorBufferInfo jointInfo{jointBuffers[i].handle(), 0, kJointBytes};
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = frameSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = frameSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadowInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = frameSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].pBufferInfo = &jointInfo;
        vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);
    }
}

// Allocates (once) and writes the per-material set for a texture entry.
void Renderer::Impl::writeMaterialSet(TextureEntry& entry) const {
    const VkDevice dev = device->device();
    if (entry.set == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &materialSetLayout;
        if (vkAllocateDescriptorSets(dev, &allocInfo, &entry.set) != VK_SUCCESS) {
            throw std::runtime_error("out of texture descriptor sets (kMaxTextures)");
        }
    }
    VkDescriptorImageInfo albedoInfo{entry.texture->sampler(), entry.texture->view(),
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = entry.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &albedoInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
}

// Per-draw data rides in push constants (ADR-012): model matrix + emissive
// + tint (+ joint offset), one range visible to both stages. Shared by the
// scene, shadow and sky pipelines so descriptor sets bind identically.
void Renderer::Impl::createPipelineLayout() {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = kDrawPushStages;
    pushRange.offset = 0;
    pushRange.size = sizeof(DrawPush);
    const VkDescriptorSetLayout setLayouts[] = {frameSetLayout, materialSetLayout};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device->device(), &layoutInfo, nullptr, &pipelineLayout) !=
        VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }
}

namespace {

// Vertex input for the mesh pipelines: binding 0 = forge::Vertex; skinned
// adds binding 1 = forge::SkinVertex (joints u16x4 at location 3, weights
// at location 4). `positionOnly` = the shadow variant (location 0 only).
struct VertexInputDesc {
    std::array<VkVertexInputBindingDescription, 2> bindings{};
    std::array<VkVertexInputAttributeDescription, 5> attributes{};
    VkPipelineVertexInputStateCreateInfo info{};

    VertexInputDesc(bool skinned, bool positionOnly) {
        bindings[0] = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        bindings[1] = {1, sizeof(SkinVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        uint32_t count = 0;
        attributes[count++] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        if (!positionOnly) {
            attributes[count++] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
            attributes[count++] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        }
        if (skinned) {
            attributes[count++] = {3, 1, VK_FORMAT_R16G16B16A16_UINT,
                                   offsetof(SkinVertex, joints)};
            attributes[count++] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                                   offsetof(SkinVertex, weights)};
        }
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        info.vertexBindingDescriptionCount = skinned ? 2u : 1u;
        info.pVertexBindingDescriptions = bindings.data();
        info.vertexAttributeDescriptionCount = count;
        info.pVertexAttributeDescriptions = attributes.data();
    }
    VertexInputDesc(const VertexInputDesc&) = delete; // info points into *this
    VertexInputDesc& operator=(const VertexInputDesc&) = delete;
};

} // namespace

VkPipeline Renderer::Impl::makeScenePipeline(bool skinned) const {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        skinned ? makeShaderModule(dev, shaders::kMeshSkinnedVert, sizeof(shaders::kMeshSkinnedVert))
                : makeShaderModule(dev, shaders::kMeshVert, sizeof(shaders::kMeshVert));
    const VkShaderModule frag =
        makeShaderModule(dev, shaders::kMeshFrag, sizeof(shaders::kMeshFrag));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // Vertex layout: interleaved position + normal + uv (forge::Vertex),
    // plus the SkinVertex stream for the skinned variant.
    const VertexInputDesc vertexInput(skinned, false);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport/scissor are DYNAMIC: resizing never rebuilds the pipeline.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    constexpr VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
    // Empirically verified with normal-visualization (P3.1 debug session):
    // geometry authored CCW-outward + our projection Y-flip (ADR-012) nets
    // out to COUNTER_CLOCKWISE front faces on screen. Sign bookkeeping across
    // lookAt/Y-flip/viewport is exactly the kind of thing you verify with
    // pixels, not derivations — flip this and every mesh renders inside-out.
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // 0..1 range, near is smaller

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    // Dynamic rendering (ADR-010): the pipeline declares attachment formats
    // directly — no VkRenderPass object anywhere in the engine.
    // The mesh pass targets the HDR scene buffer, never the swapchain.
    const VkFormat colorFormat = kHdrFormat;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput.info;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline result = VK_NULL_HANDLE;
    const VkResult created =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result);
    vkDestroyShaderModule(dev, vert, nullptr); // safe: pipeline keeps its own copy
    vkDestroyShaderModule(dev, frag, nullptr);
    if (created != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }
    return result;
}

VkPipeline Renderer::Impl::makeShadowPipeline(bool skinned) const {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        skinned ? makeShaderModule(dev, shaders::kShadowSkinnedVert,
                                   sizeof(shaders::kShadowSkinnedVert))
                : makeShaderModule(dev, shaders::kShadowVert, sizeof(shaders::kShadowVert));

    // ONE stage: no fragment shader — the depth write is the whole job.
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vert;
    stage.pName = "main";

    // Same vertex buffers, but the shader consumes only position (+ skin).
    const VertexInputDesc vertexInput(skinned, true);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    constexpr VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    // No culling: thin receivers (the ground slab) leak light if we cull the
    // "wrong" side, and the depth bias below already handles acne.
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    // Rasterizer-level depth bias: the canonical acne fix. Bias at RENDER
    // time beats shader-side epsilon because it slope-scales per triangle.
    raster.depthBiasEnable = VK_TRUE;
    raster.depthBiasConstantFactor = 1.25f;
    raster.depthBiasSlopeFactor = 1.75f;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Zero color attachments; same pipelineLayout so descriptor sets bind
    // identically across both passes.
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &stage;
    pipelineInfo.pVertexInputState = &vertexInput.info;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline result = VK_NULL_HANDLE;
    const VkResult created =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result);
    vkDestroyShaderModule(dev, vert, nullptr);
    if (created != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines (shadow) failed");
    }
    return result;
}

// Sky (ADR-030): the fullscreen triangle into the HDR scene pass, before any
// mesh. No depth test or write — meshes simply draw over it — but the
// pipeline still declares the pass's depth format (dynamic rendering must
// match the attachments that are bound). Same layout as the mesh pipelines:
// set 0 = frame UBO (sky tail), set 1 = the panorama's material set.
VkPipeline Renderer::Impl::makeSkyPipeline() const {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        makeShaderModule(dev, shaders::kFullscreenVert, sizeof(shaders::kFullscreenVert));
    const VkShaderModule frag = makeShaderModule(dev, shaders::kSkyFrag, sizeof(shaders::kSkyFrag));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{}; // gl_VertexIndex only
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    constexpr VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const VkFormat colorFormat = kHdrFormat;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline result = VK_NULL_HANDLE;
    const VkResult created =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result);
    vkDestroyShaderModule(dev, vert, nullptr);
    vkDestroyShaderModule(dev, frag, nullptr);
    if (created != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines (sky) failed");
    }
    return result;
}

// Post pipelines (bloom down/up, composite): a fullscreen triangle, no vertex
// input, no depth, one color attachment. `additive` = ONE/ONE blending (the
// bloom upsample accumulates onto the mip it renders into).
VkPipeline Renderer::Impl::makePostPipeline(const uint32_t* fragWords, size_t fragBytes,
                                            VkFormat colorFormat, bool additive) const {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        makeShaderModule(dev, shaders::kFullscreenVert, sizeof(shaders::kFullscreenVert));
    const VkShaderModule frag = makeShaderModule(dev, fragWords, fragBytes);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{}; // none: gl_VertexIndex only
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    constexpr VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (additive) {
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = postLayout;

    VkPipeline result = VK_NULL_HANDLE;
    const VkResult created =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &result);
    vkDestroyShaderModule(dev, vert, nullptr);
    vkDestroyShaderModule(dev, frag, nullptr);
    if (created != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines (post) failed");
    }
    return result;
}

// Everything in the post chain that does NOT depend on the swapchain size:
// sampler, set layout, pool, pipeline layout, pipelines.
void Renderer::Impl::createPostResources() {
    const VkDevice dev = device->device();

    // Linear + clamp-to-edge: the bloom filters rely on bilinear taps, and
    // clamping stops the screen border from wrapping light across edges.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // views are single-mip
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(dev, &samplerInfo, nullptr, &postSampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (post) failed");
    }

    // One layout for every post pass: binding 0 = primary source, binding 1 =
    // secondary (only composite.frag reads it: the bloom result).
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &postSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (post) failed");
    }

    // Own pool: reset wholesale whenever the targets are rebuilt (resize).
    constexpr uint32_t kPostSets = kMaxBloomMips + 2; // bloom mips + scene + composite
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kPostSets * 2};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kPostSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &postPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool (post) failed");
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PostPush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &postSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &postLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout (post) failed");
    }

    bloomDownPipeline = makePostPipeline(shaders::kBloomDownFrag, sizeof(shaders::kBloomDownFrag),
                                         kHdrFormat, false);
    bloomUpPipeline = makePostPipeline(shaders::kBloomUpFrag, sizeof(shaders::kBloomUpFrag),
                                       kHdrFormat, true);
    // The swapchain format does not change across recreation in practice
    // (same assumption the UI pipeline makes); if it ever does, rebuild this.
    compositePipeline = makePostPipeline(shaders::kCompositeFrag, sizeof(shaders::kCompositeFrag),
                                         swapchain->format(), false);
}

void Renderer::Impl::destroyPostResources() {
    const VkDevice dev = device->device();
    vkDestroyPipeline(dev, bloomDownPipeline, nullptr);
    vkDestroyPipeline(dev, bloomUpPipeline, nullptr);
    vkDestroyPipeline(dev, compositePipeline, nullptr);
    vkDestroyPipelineLayout(dev, postLayout, nullptr);
    vkDestroyDescriptorPool(dev, postPool, nullptr);
    vkDestroyDescriptorSetLayout(dev, postSetLayout, nullptr);
    vkDestroySampler(dev, postSampler, nullptr);
}

// The swapchain-sized half of the post chain: HDR scene target, bloom mip
// chain, and the descriptor sets that point at them. Called at startup and
// after every swapchain recreation (always under waitIdle).
void Renderer::Impl::createPostTargets() {
    const VkDevice dev = device->device();
    const VkExtent2D extent = swapchain->extent();

    allocateImage(*device, extent.width, extent.height, kHdrFormat,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, hdrImage,
                  hdrMemory);
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = hdrImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kHdrFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &viewInfo, nullptr, &hdrView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (hdr) failed");
    }

    // Bloom chain: mip 0 is half resolution; keep halving while the short
    // side stays at least a few texels (tiny windows get a shorter chain).
    const uint32_t baseWidth = std::max(extent.width / 2, 1u);
    const uint32_t baseHeight = std::max(extent.height / 2, 1u);
    uint32_t mipCount = 1;
    while (mipCount < kMaxBloomMips && (std::min(baseWidth, baseHeight) >> mipCount) >= 4) {
        ++mipCount;
    }
    allocateImage(*device, baseWidth, baseHeight, kHdrFormat,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, bloomImage,
                  bloomMemory, mipCount);
    bloomViews.assign(mipCount, VK_NULL_HANDLE);
    bloomExtents.resize(mipCount);
    for (uint32_t i = 0; i < mipCount; ++i) {
        bloomExtents[i] = {std::max(baseWidth >> i, 1u), std::max(baseHeight >> i, 1u)};
        viewInfo.image = bloomImage;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};
        if (vkCreateImageView(dev, &viewInfo, nullptr, &bloomViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView (bloom) failed");
        }
    }

    // Sets: [0] scene, [1] composite, [2..] one per bloom mip.
    vkResetDescriptorPool(dev, postPool, 0);
    std::vector<VkDescriptorSetLayout> layouts(mipCount + 2, postSetLayout);
    std::vector<VkDescriptorSet> sets(layouts.size(), VK_NULL_HANDLE);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = postPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(dev, &allocInfo, sets.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (post) failed");
    }
    sceneSet = sets[0];
    compositeSet = sets[1];
    bloomSets.assign(sets.begin() + 2, sets.end());

    const auto writeSet = [&](VkDescriptorSet set, VkImageView primary, VkImageView secondary) {
        const VkDescriptorImageInfo images[2] = {
            {postSampler, primary, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
            {postSampler, secondary, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        };
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 2; // bindings 0 and 1 are consecutive
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = images;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    };
    // Binding 1 is only read by composite.frag; the other sets just need it
    // to hold something valid, so it mirrors binding 0.
    writeSet(sceneSet, hdrView, hdrView);
    writeSet(compositeSet, hdrView, bloomViews[0]);
    for (uint32_t i = 0; i < mipCount; ++i) {
        writeSet(bloomSets[i], bloomViews[i], bloomViews[i]);
    }
}

void Renderer::Impl::destroyPostTargets() {
    const VkDevice dev = device->device();
    for (const VkImageView view : bloomViews) {
        vkDestroyImageView(dev, view, nullptr);
    }
    bloomViews.clear();
    bloomExtents.clear();
    bloomSets.clear(); // the sets themselves die with the pool reset
    vkDestroyImage(dev, bloomImage, nullptr);
    vkFreeMemory(dev, bloomMemory, nullptr);
    vkDestroyImageView(dev, hdrView, nullptr);
    vkDestroyImage(dev, hdrImage, nullptr);
    vkFreeMemory(dev, hdrMemory, nullptr);
    bloomImage = VK_NULL_HANDLE;
    bloomMemory = VK_NULL_HANDLE;
    hdrView = VK_NULL_HANDLE;
    hdrImage = VK_NULL_HANDLE;
    hdrMemory = VK_NULL_HANDLE;
    sceneSet = VK_NULL_HANDLE;
    compositeSet = VK_NULL_HANDLE;
}

void Renderer::Impl::createFrameObjects() {
    const VkDevice dev = device->device();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = device->graphicsFamily();
    if (vkCreateCommandPool(dev, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(dev, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // frame 0 must not wait forever
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        imageAvailable[i] = makeSemaphore(dev);
        if (vkCreateFence(dev, &fenceInfo, nullptr, &inFlight[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFence failed");
        }
    }
    createImageSemaphores();
}

void Renderer::Impl::createImageSemaphores() {
    for (uint32_t i = 0; i < swapchain->imageCount(); ++i) {
        renderFinished.push_back(makeSemaphore(device->device()));
    }
}

void Renderer::Impl::recreateSwapchain() {
    device->waitIdle();
    for (const VkSemaphore s : renderFinished) {
        vkDestroySemaphore(device->device(), s, nullptr);
    }
    renderFinished.clear();
    destroyPostTargets();
    destroyDepthResources();
    swapchain.reset(); // old swapchain must die before the surface is reused

    const VkExtent2D extent = framebufferExtent(window);
    swapchain = std::make_unique<VulkanSwapchain>(*device, surface, extent.width, extent.height);
    createDepthResources(); // depth must always match the swapchain extent
    createPostTargets();    // ...and so must the HDR scene target + bloom chain
    createImageSemaphores();
    needRecreate = false;
    // Pipeline survives: viewport/scissor are dynamic and the attachment
    // formats do not change in practice (if they ever do, recreate it here).
}

// Bloom (dual filter): bright-pass + 13-tap downsample of the HDR scene into
// mip 0, plain 13-tap downsamples down the chain, then 9-tap tent upsamples
// back up, each ADDED onto the mip it lands on. Expects hdrImage in
// SHADER_READ_ONLY; leaves bloom mip 0 in SHADER_READ_ONLY for the composite.
void Renderer::Impl::recordBloom(VkCommandBuffer cmd) const {
    const auto mipCount = static_cast<uint32_t>(bloomViews.size());
    // Source scope = the previous frame's last uses of this shared image.
    constexpr VkPipelineStageFlags2 kPrevStages =
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    constexpr VkAccessFlags2 kPrevAccess =
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    if (bloomIntensity <= 0.0f) {
        // Disabled: composite.frag skips the bloom taps entirely, but the
        // image bound to its sampler must still be in the layout the
        // descriptor promises. Contents stay undefined and unread.
        vkutil::transitionImage(cmd, bloomImage, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kPrevStages, kPrevAccess,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        return;
    }

    // Every mip is fully overwritten by its downsample: discard old contents.
    vkutil::transitionImage(cmd, bloomImage, VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            kPrevStages, kPrevAccess,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    const auto toSampled = [&](uint32_t mip) {
        vkutil::transitionImage(cmd, bloomImage, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    };
    // One fullscreen draw into bloom mip `dstMip`, sampling `sourceSet`.
    const auto drawInto = [&](uint32_t dstMip, VkAttachmentLoadOp loadOp, VkDescriptorSet sourceSet,
                              const PostPush& push) {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = bloomViews[dstMip];
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = loadOp;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = {{0, 0}, bloomExtents[dstMip]};
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &attachment;
        vkCmdBeginRendering(cmd, &info);
        const VkViewport viewport{0.0f,
                                  0.0f,
                                  static_cast<float>(bloomExtents[dstMip].width),
                                  static_cast<float>(bloomExtents[dstMip].height),
                                  0.0f,
                                  1.0f};
        const VkRect2D scissor{{0, 0}, bloomExtents[dstMip]};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout, 0, 1, &sourceSet,
                                0, nullptr);
        vkCmdPushConstants(cmd, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush),
                           &push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    };
    const auto texelSize = [](VkExtent2D extent) {
        return glm::vec2(1.0f / static_cast<float>(extent.width),
                         1.0f / static_cast<float>(extent.height));
    };

    // Down: scene -> mip 0 (thresholded), then mip i-1 -> mip i.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomDownPipeline);
    for (uint32_t i = 0; i < mipCount; ++i) {
        const bool prefilter = i == 0;
        const glm::vec2 srcTexel = texelSize(prefilter ? swapchain->extent() : bloomExtents[i - 1]);
        const PostPush push{glm::vec4(srcTexel, prefilter ? kBloomThreshold : 0.0f, kBloomKnee)};
        drawInto(i, VK_ATTACHMENT_LOAD_OP_DONT_CARE, prefilter ? sceneSet : bloomSets[i - 1], push);
        toSampled(i);
    }

    // Up: mip i += tent(mip i+1), smallest first.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomUpPipeline);
    for (uint32_t i = mipCount - 1; i-- > 0;) {
        vkutil::transitionImage(cmd, bloomImage, VK_IMAGE_ASPECT_COLOR_BIT, i, 1,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        const PostPush push{glm::vec4(texelSize(bloomExtents[i + 1]), 0.0f, 0.0f)};
        drawInto(i, VK_ATTACHMENT_LOAD_OP_LOAD, bloomSets[i + 1], push);
        toSampled(i);
    }
}

void Renderer::Impl::record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frame,
                            std::span<const DrawItem> items) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Skinned draw = a skinned mesh whose palette slice fits in this frame's
    // upload; anything else (incl. kNoJoints) takes the static path, which
    // draws a skinned mesh in its bind pose.
    const auto isSkinned = [&](const DrawItem& item) {
        const MeshEntry& mesh = meshes[item.mesh.value];
        return mesh.jointSpan > 0 && item.jointOffset != DrawItem::kNoJoints &&
               item.jointOffset <= jointCount && mesh.jointSpan <= jointCount - item.jointOffset;
    };
    // Binds the static or skinned variant of `pair` only when it changes.
    VkPipeline bound = VK_NULL_HANDLE;
    const auto bindFor = [&](bool skinned, VkPipeline staticPipe, VkPipeline skinnedPipe) {
        const VkPipeline want = skinned ? skinnedPipe : staticPipe;
        if (want != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
            bound = want;
        }
    };
    const VkDeviceSize vbOffsets[2] = {0, 0};
    // `transform` is what the vertex stage sees as push.model: the item's
    // model matrix in the scene pass, cascade view-proj * model in shadows.
    const auto drawItemMesh = [&](const DrawItem& item, bool skinned, const glm::mat4& transform) {
        const MeshEntry& mesh = meshes[item.mesh.value];
        const VkBuffer vbs[2] = {mesh.vertexBuffer.handle(),
                                 skinned ? mesh.skinBuffer.handle() : VK_NULL_HANDLE};
        vkCmdBindVertexBuffers(cmd, 0, skinned ? 2u : 1u, vbs, vbOffsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
        const DrawPush push{transform, glm::vec4(item.emissive, item.roughness),
                            glm::vec4(item.tint, item.metallic),
                            glm::uvec4(skinned ? item.jointOffset : 0u, 0u, 0u, 0u)};
        vkCmdPushConstants(cmd, pipelineLayout, kDrawPushStages, 0, sizeof(DrawPush), &push);
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
    };

    // ---- Pass 1: shadow cascades, from the light's point of view (ADR-014,
    // ADR-033). Casters: everything but bright emitters — laser beams and
    // light strips ARE light, and a beam casting a shadow line reads as a
    // bug. Each caster's world bounding sphere (static meshes only: a
    // skinned pose can leave its bind-pose bounds) is culled in light space:
    // first against the light-space box of everything the camera can see
    // shadowed (shadowReceivers: no shadow can land in view from outside
    // it), then per cascade against that cascade's box. Far-off geometry
    // (the other location, 400 m away) costs no shadow draws at all.
    // Plain float math on purpose: this runs per item per frame, and glm's
    // operators are function calls in the debug build.
    struct Caster {
        const DrawItem* item;
        bool skinned;
        bool cullable;
        float x, y, z; // light-view space centre
        float radius;
    };
    std::vector<Caster> casters;
    casters.reserve(items.size());
    std::vector<Caster> skinnedCasters; // drawn last: one pipeline switch per cascade
    const float* rot = glm::value_ptr(lightRot);
    for (const DrawItem& item : items) {
        const glm::vec3& e = item.emissive;
        if (item.mesh.value >= meshes.size() || e.r > 1.0f || e.g > 1.0f || e.b > 1.0f) {
            continue;
        }
        Caster caster{&item, isSkinned(item), false, 0.0f, 0.0f, 0.0f, 0.0f};
        const float* b = glm::value_ptr(meshes[item.mesh.value].bounds);
        if (caster.skinned || b[3] < 0.0f) {
            (caster.skinned ? skinnedCasters : casters).push_back(caster);
            continue;
        }
        const float* m = glm::value_ptr(item.model); // column-major: m[col * 4 + row]
        const float wx = m[0] * b[0] + m[4] * b[1] + m[8] * b[2] + m[12];
        const float wy = m[1] * b[0] + m[5] * b[1] + m[9] * b[2] + m[13];
        const float wz = m[2] * b[0] + m[6] * b[1] + m[10] * b[2] + m[14];
        caster.x = rot[0] * wx + rot[4] * wy + rot[8] * wz + rot[12];
        caster.y = rot[1] * wx + rot[5] * wy + rot[9] * wz + rot[13];
        caster.z = rot[2] * wx + rot[6] * wy + rot[10] * wz + rot[14];
        const float sx = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
        const float sy = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
        const float sz = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
        caster.radius = b[3] * std::sqrt(std::max(sx, std::max(sy, sz)));
        caster.cullable = true;
        const float r = caster.radius;
        if (caster.x + r < shadowReceivers.lo.x || caster.x - r > shadowReceivers.hi.x ||
            caster.y + r < shadowReceivers.lo.y || caster.y - r > shadowReceivers.hi.y ||
            caster.z + r < shadowReceivers.lo.z) {
            continue; // its shadow falls nowhere the camera can see
        }
        casters.push_back(caster);
    }
    casters.insert(casters.end(), skinnedCasters.begin(), skinnedCasters.end());

    // UNDEFINED discard is fine (cleared every frame) and doubles as the
    // frame-N/N+1 execution barrier on the shared map (all layers).
    transitionImage(
        cmd, shadowImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    const VkViewport shadowViewport{
        0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize),
        0.0f, 1.0f};
    const VkRect2D shadowScissor{{0, 0}, {kShadowMapSize, kShadowMapSize}};
    if (!casters.empty()) { // set 0: the skinned shadow stage reads the joint palette
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &frameSets[frame], 0, nullptr);
    }
    for (uint32_t c = 0; c < kShadowCascades; ++c) {
        const Cascade& cascade = cascades[c];
        VkRenderingAttachmentInfo shadowDepth{};
        shadowDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        shadowDepth.imageView = shadowLayerViews[c];
        shadowDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // sampled by pass 2
        shadowDepth.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo shadowRendering{};
        shadowRendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        shadowRendering.renderArea = {{0, 0}, {kShadowMapSize, kShadowMapSize}};
        shadowRendering.layerCount = 1;
        shadowRendering.pDepthAttachment = &shadowDepth;

        vkCmdBeginRendering(cmd, &shadowRendering);
        vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
        vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
        for (const Caster& caster : casters) {
            if (caster.cullable) {
                const float reach = cascade.radius + caster.radius;
                if (std::abs(caster.x - cascade.center.x) > reach ||
                    std::abs(caster.y - cascade.center.y) > reach ||
                    caster.z - caster.radius > cascade.zTop ||
                    caster.z + caster.radius < cascade.zBottom) {
                    continue;
                }
            }
            bindFor(caster.skinned, shadowPipeline, skinnedShadowPipeline);
            // No material set: depth doesn't care about albedo.
            drawItemMesh(*caster.item, caster.skinned,
                         multiplyRaw(cascade.viewProj, caster.item->model));
        }
        vkCmdEndRendering(cmd);
    }

    // Depth writes done -> fragment shader reads (pass 2 samples the map).
    transitionImage(
        cmd, shadowImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // ---- Pass 2: the scene, from the camera, into the HDR target.
    // Cleared every frame, so UNDEFINED is honest; the source scope covers
    // the previous frame's uses of this shared image (attachment writes, then
    // sampling by bloom/composite), which makes this the frame-N/N+1 barrier.
    transitionImage(
        cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    // Depth: contents are cleared every frame, so UNDEFINED is honest AND
    // this doubles as the frame-N/N+1 execution barrier on the shared image.
    VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (formatHasStencil(depthFormat)) {
        depthAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    transitionImage(
        cmd, depthImage, depthAspect, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = hdrView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // The sky pass covers every pixel, so the colour target needs no clear.
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = depthView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // nobody reads it after
    depthAttachment.clearValue.depthStencil = {1.0f, 0};        // far plane

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, swapchain->extent()};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderingInfo);
    const VkViewport viewport{0.0f,
                              0.0f,
                              static_cast<float>(swapchain->extent().width),
                              static_cast<float>(swapchain->extent().height),
                              0.0f,
                              1.0f};
    const VkRect2D scissor{{0, 0}, swapchain->extent()};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Sky first (ADR-030): every pixel, no depth — the meshes draw over it.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
    bound = skyPipeline;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &frameSets[frame], 0, nullptr);
    const uint32_t panorama = sky.panorama.value < textures.size()
                                  ? sky.panorama.value
                                  : Renderer::whiteTexture().value;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                            &textures[panorama].set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (!items.empty()) {
        for (const DrawItem& item : items) {
            if (item.mesh.value >= meshes.size()) {
                continue; // stale/foreign handle: draw nothing rather than crash
            }
            const bool skinned = isSkinned(item);
            bindFor(skinned, pipeline, skinnedPipeline);
            // Set 1 rebinds per item; sorting draws by texture is the P8-era
            // optimization this layout is already shaped for.
            const uint32_t texture =
                item.texture.value < textures.size() ? item.texture.value : 0u;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                                    &textures[texture].set, 0, nullptr);
            drawItemMesh(item, skinned, item.model);
        }
    }
    vkCmdEndRendering(cmd);

    // Scene done: the post chain samples it from here on.
    transitionImage(cmd, hdrImage, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // ---- Pass 3: bloom chain (leaves bloom mip 0 sampleable either way).
    recordBloom(cmd);

    // ---- Pass 4: composite into the swapchain image — bloom add, exposure,
    // ACES tone map — then the UI on top, un-tonemapped, in the same pass.
    // The fullscreen triangle overwrites every pixel: UNDEFINED + DONT_CARE.
    transitionImage(cmd, swapchain->image(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo swapAttachment{};
    swapAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    swapAttachment.imageView = swapchain->view(imageIndex);
    swapAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo compositeInfo{};
    compositeInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    compositeInfo.renderArea = {{0, 0}, swapchain->extent()};
    compositeInfo.layerCount = 1;
    compositeInfo.colorAttachmentCount = 1;
    compositeInfo.pColorAttachments = &swapAttachment; // NO depth: see queryVkInfo

    vkCmdBeginRendering(cmd, &compositeInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, postLayout, 0, 1, &compositeSet,
                            0, nullptr);
    // Every mip of the chain was summed into mip 0; dividing by the count
    // keeps "intensity" meaning the same thing at any window size.
    const float bloomStrength =
        bloomIntensity * kBloomGain / static_cast<float>(bloomViews.size());
    const PostPush compositePush{glm::vec4(1.0f / static_cast<float>(bloomExtents[0].width),
                                           1.0f / static_cast<float>(bloomExtents[0].height),
                                           bloomStrength, exposure)};
    vkCmdPushConstants(cmd, postLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush),
                       &compositePush);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    if (uiHook != nullptr) {
        uiHook->record(cmd); // editor UI draws last, crisp, over the tone-mapped scene
    }
    vkCmdEndRendering(cmd);

    // Hand the image to the presentation engine.
    transitionImage(cmd, swapchain->image(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                    VK_ACCESS_2_NONE);

    vkEndCommandBuffer(cmd);
}

Renderer::Renderer(Window& window, VulkanContext& context)
    : m_impl(std::make_unique<Impl>(window, context)) {
    // GLFW picks the right platform surface extension (win32/xlib/wayland/…)
    // so we don't ifdef per OS.
    if (glfwCreateWindowSurface(context.instance(), window.nativeHandle(), nullptr,
                                &m_impl->surface) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface failed");
    }
    m_impl->device = std::make_unique<VulkanDevice>(context.instance(), m_impl->surface);

    const VkExtent2D extent = framebufferExtent(window);
    m_impl->swapchain = std::make_unique<VulkanSwapchain>(*m_impl->device, m_impl->surface,
                                                          extent.width, extent.height);
    m_impl->createDepthResources();
    m_impl->createFrameObjects(); // command pool first: texture upload needs it
    m_impl->createShadowResources();
    m_impl->createDescriptors();
    m_impl->createPipelineLayout();
    m_impl->pipeline = m_impl->makeScenePipeline(false);
    m_impl->skinnedPipeline = m_impl->makeScenePipeline(true);
    m_impl->shadowPipeline = m_impl->makeShadowPipeline(false);
    m_impl->skinnedShadowPipeline = m_impl->makeShadowPipeline(true);
    m_impl->skyPipeline = m_impl->makeSkyPipeline();
    m_impl->createPostResources();
    m_impl->createPostTargets();
    // Texture 0 = the built-in checker placeholder (ADR-013), created through
    // the same public path every real asset uses.
    const TextureHandle checker = addTexture(256, 256, makeCheckerTexels(256, 32));
    (void)checker; // handle 0 by construction; defaultTexture() documents it
    // Texture 1 = plain white (ADR-030): untextured materials and the
    // texture-table-full fallback. whiteTexture() documents it.
    constexpr uint8_t kWhite[4] = {255, 255, 255, 255};
    const TextureHandle white = addTexture(1, 1, kWhite);
    (void)white;
    FORGE_INFO("renderer ready: dynamic rendering, depth {}, {} frames in flight, "
               "{} x {}x{} shadow cascades to {} m, RGBA16F HDR scene + {}-mip bloom + "
               "ACES composite, checker placeholder as texture 0",
               m_impl->depthFormat == VK_FORMAT_D32_SFLOAT ? "D32_SFLOAT" : "D+S fallback",
               kFramesInFlight, kShadowCascades, kShadowMapSize, kShadowMapSize, kShadowFar,
               m_impl->bloomViews.size());
}

Renderer::~Renderer() {
    const VkDevice dev = m_impl->device ? m_impl->device->device() : VK_NULL_HANDLE;
    if (dev != VK_NULL_HANDLE) {
        m_impl->device->waitIdle(); // never destroy what the GPU still reads
        for (const VkSemaphore s : m_impl->renderFinished) {
            vkDestroySemaphore(dev, s, nullptr);
        }
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            vkDestroySemaphore(dev, m_impl->imageAvailable[i], nullptr);
            vkDestroyFence(dev, m_impl->inFlight[i], nullptr);
        }
        vkDestroyCommandPool(dev, m_impl->commandPool, nullptr);
        vkDestroyPipeline(dev, m_impl->pipeline, nullptr);
        vkDestroyPipeline(dev, m_impl->shadowPipeline, nullptr);
        vkDestroyPipeline(dev, m_impl->skinnedPipeline, nullptr);
        vkDestroyPipeline(dev, m_impl->skinnedShadowPipeline, nullptr);
        vkDestroyPipeline(dev, m_impl->skyPipeline, nullptr);
        m_impl->destroyPostTargets();
        m_impl->destroyPostResources();
        vkDestroyPipelineLayout(dev, m_impl->pipelineLayout, nullptr);
        vkDestroyDescriptorPool(dev, m_impl->descriptorPool, nullptr); // frees ALL sets
        vkDestroyDescriptorSetLayout(dev, m_impl->frameSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(dev, m_impl->materialSetLayout, nullptr);
        vkDestroySampler(dev, m_impl->shadowSampler, nullptr);
        vkDestroyImageView(dev, m_impl->shadowView, nullptr);
        for (const VkImageView view : m_impl->shadowLayerViews) {
            vkDestroyImageView(dev, view, nullptr);
        }
        vkDestroyImage(dev, m_impl->shadowImage, nullptr);
        vkFreeMemory(dev, m_impl->shadowMemory, nullptr);
        m_impl->textures.clear(); // VulkanTextures need the device alive
        m_impl->meshes.clear();   // ditto for their GpuBuffers
        for (auto& ubo : m_impl->frameUbos) {
            ubo = GpuBuffer{};
        }
        for (auto& joints : m_impl->jointBuffers) {
            joints = GpuBuffer{};
        }
        m_impl->destroyDepthResources();
    }
    m_impl->swapchain.reset(); // strict reverse order: swapchain, device, surface
    m_impl->device.reset();
    if (m_impl->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_impl->context.instance(), m_impl->surface, nullptr);
    }
}

// Shared by addMesh and updateMesh: build device-local buffers via staging.
void Renderer::Impl::buildMeshEntry(std::span<const Vertex> vertices,
                                    std::span<const uint32_t> indices,
                                    std::span<const SkinVertex> skin, MeshEntry& out) const {
    const VkDeviceSize vertexBytes = vertices.size_bytes();
    const VkDeviceSize indexBytes = indices.size_bytes();
    out.vertexBuffer = GpuBuffer(
        *device, vertexBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    out.indexBuffer = GpuBuffer(*device, indexBytes,
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    uploadToBuffer(*device, commandPool, out.vertexBuffer.handle(), vertices.data(), vertexBytes);
    uploadToBuffer(*device, commandPool, out.indexBuffer.handle(), indices.data(), indexBytes);
    out.indexCount = static_cast<uint32_t>(indices.size());
    // Bounding sphere for shadow-caster culling (ADR-033): the AABB's centre
    // and the farthest vertex from it — loose but cheap, and conservative.
    out.bounds = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
    if (!vertices.empty()) {
        glm::vec3 lo = vertices.front().position;
        glm::vec3 hi = lo;
        for (const Vertex& v : vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
        }
        const glm::vec3 center = 0.5f * (lo + hi);
        float radiusSq = 0.0f;
        for (const Vertex& v : vertices) {
            const glm::vec3 d = v.position - center;
            radiusSq = std::max(radiusSq, glm::dot(d, d));
        }
        out.bounds = glm::vec4(center, std::sqrt(radiusSq));
    }
    out.skinBuffer = GpuBuffer{};
    out.jointSpan = 0;
    if (!skin.empty()) {
        if (skin.size() != vertices.size()) {
            throw std::runtime_error("skinned mesh: skin stream must match the vertex count");
        }
        out.skinBuffer = GpuBuffer(*device, skin.size_bytes(),
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        uploadToBuffer(*device, commandPool, out.skinBuffer.handle(), skin.data(),
                       skin.size_bytes());
        for (const SkinVertex& v : skin) {
            for (const uint16_t joint : v.joints) {
                out.jointSpan = std::max(out.jointSpan, static_cast<uint32_t>(joint) + 1u);
            }
        }
    }
}

MeshHandle Renderer::addMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices) {
    Impl& impl = *m_impl;
    Impl::MeshEntry entry;
    impl.buildMeshEntry(vertices, indices, {}, entry);
    impl.meshes.push_back(std::move(entry));
    const auto handle = static_cast<uint32_t>(impl.meshes.size() - 1);
    FORGE_INFO("mesh[{}] uploaded: {} vertices, {} indices ({} KiB on GPU)", handle,
               vertices.size(), indices.size(),
               (vertices.size_bytes() + indices.size_bytes() + 1023) / 1024);
    return {handle};
}

MeshHandle Renderer::addSkinnedMesh(std::span<const Vertex> vertices,
                                    std::span<const uint32_t> indices,
                                    std::span<const SkinVertex> skin) {
    Impl& impl = *m_impl;
    Impl::MeshEntry entry;
    impl.buildMeshEntry(vertices, indices, skin, entry);
    impl.meshes.push_back(std::move(entry));
    const auto handle = static_cast<uint32_t>(impl.meshes.size() - 1);
    FORGE_INFO("mesh[{}] uploaded (skinned, {} joints): {} vertices, {} indices", handle,
               impl.meshes.back().jointSpan, vertices.size(), indices.size());
    return {handle};
}

void Renderer::updateMesh(MeshHandle handle, std::span<const Vertex> vertices,
                          std::span<const uint32_t> indices) {
    Impl& impl = *m_impl;
    impl.device->waitIdle(); // in-flight frames may still read the old buffers
    impl.buildMeshEntry(vertices, indices, {}, impl.meshes.at(handle.value));
    FORGE_INFO("mesh[{}] hot-reloaded: {} vertices, {} indices", handle.value, vertices.size(),
               indices.size());
}

TextureHandle Renderer::addTexture(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
    Impl& impl = *m_impl;
    // Full table: degrade, never throw (a model pack must not kill a frame).
    // Handles 0 and 1 are created in the constructor, so the fallback exists.
    if (impl.textures.size() >= kMaxTextures) {
        if (!impl.textureCapWarned) {
            impl.textureCapWarned = true;
            FORGE_WARN("texture table full ({} textures): further textures draw as white",
                       kMaxTextures);
        }
        return whiteTexture();
    }
    Impl::TextureEntry entry;
    entry.texture =
        std::make_unique<VulkanTexture>(*impl.device, impl.commandPool, width, height, rgba);
    impl.writeMaterialSet(entry);
    impl.textures.push_back(std::move(entry));
    const auto handle = static_cast<uint32_t>(impl.textures.size() - 1);
    FORGE_INFO("texture[{}] uploaded: {}x{}", handle, width, height);
    return {handle};
}

void Renderer::updateTexture(TextureHandle handle, uint32_t width, uint32_t height,
                             std::span<const uint8_t> rgba) {
    Impl& impl = *m_impl;
    Impl::TextureEntry& entry = impl.textures.at(handle.value);
    impl.device->waitIdle(); // the old image may be mid-sample on the GPU
    entry.texture =
        std::make_unique<VulkanTexture>(*impl.device, impl.commandPool, width, height, rgba);
    impl.writeMaterialSet(entry); // same set, new image view
    FORGE_INFO("texture[{}] hot-reloaded: {}x{}", handle.value, width, height);
}

void Renderer::drawFrame(const Camera& camera, std::span<const DrawItem> items,
                         std::span<const glm::mat4> joints) {
    Impl& impl = *m_impl;
    const VkDevice dev = impl.device->device();

    // Minimized: zero-sized framebuffer, nothing to render into.
    const VkExtent2D extent = framebufferExtent(impl.window);
    if (extent.width == 0 || extent.height == 0) {
        return;
    }
    if (impl.needRecreate) {
        impl.recreateSwapchain();
    }

    const uint32_t frame = impl.frameIndex;
    vkWaitForFences(dev, 1, &impl.inFlight[frame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquire =
        vkAcquireNextImageKHR(dev, impl.swapchain->handle(), UINT64_MAX, impl.imageAvailable[frame],
                              VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        impl.recreateSwapchain();
        return; // retry next frame with the new swapchain
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    // The fence proved the GPU is done with this frame slot — its UBO is
    // safely writable now. Aspect comes from the LIVE swapchain, so resize
    // never stretches the projection.
    const glm::mat4 view = glm::lookAt(camera.position, camera.target, glm::vec3(0.0f, 1.0f, 0.0f));
    const float aspect = static_cast<float>(impl.swapchain->extent().width) /
                         static_cast<float>(impl.swapchain->extent().height);
    glm::mat4 proj = glm::perspective(glm::radians(camera.fovYDegrees), aspect, camera.nearPlane,
                                      camera.farPlane);
    proj[1][1] *= -1.0f; // GL's Y-up clip space -> Vulkan's Y-down (ADR-012)

    // Shadow cascades (ADR-014, ADR-033). The key light comes from Impl
    // (app-settable per room, week 5). The cascades cover the camera's own
    // frustum out to kShadowFar, split by the practical scheme; each one is
    // the bounding sphere of its slice, snapped to whole texels in a light
    // space that only rotates with the light (fitCascade). NO Y-flip — each
    // map is rendered AND sampled through the same matrix, so the convention
    // cancels out; only the swapchain path needs the flip.
    const glm::vec3 lightDir = impl.lightDir;
    impl.lightRot = lightRotation(lightDir);
    {
        glm::vec3 forward = camera.target - camera.position;
        const float forwardLen = glm::length(forward);
        forward = forwardLen > 1e-6f ? forward / forwardLen : glm::vec3(0.0f, 0.0f, -1.0f);
        const float nearZ = std::max(camera.nearPlane, 0.01f);
        const float farZ = std::max(std::min(kShadowFar, camera.farPlane), nearZ + 1.0f);
        const float tanHalfY = std::tan(glm::radians(camera.fovYDegrees) * 0.5f);
        const float k = tanHalfY * std::sqrt(1.0f + aspect * aspect); // half-diagonal / depth
        float sliceNear = nearZ;
        for (uint32_t c = 0; c < kShadowCascades; ++c) {
            const float p = static_cast<float>(c + 1) / static_cast<float>(kShadowCascades);
            const float logSplit = nearZ * std::pow(farZ / nearZ, p);
            const float uniformSplit = nearZ + (farZ - nearZ) * p;
            const float sliceFar =
                kCascadeSplitLambda * logSplit + (1.0f - kCascadeSplitLambda) * uniformSplit;
            impl.cascades[c] =
                fitCascade(impl.lightRot, camera.position, forward, sliceNear, sliceFar, k);
            sliceNear = sliceFar;
        }
        // The frustum [nearZ, farZ] in light space, as an AABB (caster culling).
        const glm::vec3 worldUp = std::abs(forward.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                                               : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
        const glm::vec3 up = glm::cross(right, forward);
        impl.shadowReceivers.lo = glm::vec3(std::numeric_limits<float>::max());
        impl.shadowReceivers.hi = glm::vec3(std::numeric_limits<float>::lowest());
        for (const float depth : {nearZ, farZ}) {
            for (const float sx : {-1.0f, 1.0f}) {
                for (const float sy : {-1.0f, 1.0f}) {
                    const glm::vec3 corner = camera.position + forward * depth +
                                             right * (sx * tanHalfY * aspect * depth) +
                                             up * (sy * tanHalfY * depth);
                    const glm::vec3 ls = glm::vec3(impl.lightRot * glm::vec4(corner, 1.0f));
                    impl.shadowReceivers.lo = glm::min(impl.shadowReceivers.lo, ls);
                    impl.shadowReceivers.hi = glm::max(impl.shadowReceivers.hi, ls);
                }
            }
        }
        // 1 m of slack: mesh.frag's normal offset moves lookups a little.
        impl.shadowReceivers.lo -= glm::vec3(1.0f);
        impl.shadowReceivers.hi += glm::vec3(1.0f);
    }

    FrameUbo ubo{};
    ubo.viewProj = proj * view;
    ubo.lightViewProj = impl.cascades[0].viewProj;
    ubo.cameraPos = glm::vec4(camera.position, 1.0f);
    ubo.lightDirection = glm::vec4(lightDir, 0.0f);
    ubo.lightColor = glm::vec4(impl.lightColor, 1.0f); // rgb * HDR intensity
    ubo.fogColor = glm::vec4(impl.fogColor, impl.fogDensity);
    // Hemisphere ambient: the key-derived default, blended toward the
    // script's colours by ambientWeight (0 = exactly the default, ADR-033).
    const glm::vec3 defaultSky = kAmbientSkyBase + impl.lightColor * kAmbientSkyFromKey;
    const glm::vec3 defaultGround = kAmbientGroundBase + impl.lightColor * kAmbientGroundFromKey;
    ubo.ambientSky = glm::vec4(
        impl.ambientWeight > 0.0f ? glm::mix(defaultSky, impl.ambientSky, impl.ambientWeight)
                                  : defaultSky,
        0.0f);
    ubo.ambientGround = glm::vec4(
        impl.ambientWeight > 0.0f ? glm::mix(defaultGround, impl.ambientGround, impl.ambientWeight)
                                  : defaultGround,
        0.0f);
    ubo.counts = glm::vec4(static_cast<float>(impl.pointLightCount), 0.0f, 0.0f, 0.0f);
    for (uint32_t i = 0; i < impl.pointLightCount; ++i) {
        const PointLight& light = impl.pointLights[i];
        ubo.pointPosRadius[i] = glm::vec4(light.position, light.radius);
        ubo.pointColor[i] = glm::vec4(light.color, 0.0f);
    }
    // Sky tail (ADR-030). The sun disc tracks the key light: its direction
    // AND its colour (setLighting), scaled by the sky's sunIntensity.
    const Sky& sky = impl.sky;
    const bool panorama = sky.panorama.value != Sky::kNoPanorama &&
                          sky.panorama.value < impl.textures.size();
    ubo.invViewProj = glm::inverse(ubo.viewProj);
    ubo.skyZenith = glm::vec4(sky.zenith, 0.0f);
    ubo.skyHorizon = glm::vec4(sky.horizon, glm::radians(sky.panoramaYaw));
    ubo.skyGround = glm::vec4(sky.ground, 0.0f);
    ubo.skySun = glm::vec4(impl.lightColor * sky.sunIntensity,
                           sky.sunSize > 0.0f ? std::cos(glm::radians(sky.sunSize)) : 2.0f);
    ubo.skyParams = glm::vec4(panorama ? 1.0f : 0.0f, sky.panoramaIntensity, camera.farPlane,
                              sky.sunHalo);
    // Cascade tail (ADR-033). The fade scales down with a near far plane, so
    // shadows still fade out before the geometry ends.
    for (uint32_t c = 0; c < kShadowCascades; ++c) {
        ubo.cascadeViewProj[c] = impl.cascades[c].viewProj;
        ubo.cascadeTexel[static_cast<glm::length_t>(c)] = impl.cascades[c].texel;
    }
    const float shadowEnd = std::min(kShadowFar, camera.farPlane);
    ubo.shadowParams = glm::vec4(kShadowFadeStart * (shadowEnd / kShadowFar), shadowEnd,
                                 kCascadeBlendBand, kShadowNormalOffsetTexels);
    impl.frameUbos[frame].writeBytes(&ubo, sizeof(ubo));
    // Joint palette (ADR-030): same fence-guarded slot as the UBO. Overflow
    // is clipped; draws whose slice no longer fits fall back to bind pose.
    impl.jointCount = static_cast<uint32_t>(std::min<size_t>(joints.size(), kMaxJoints));
    if (impl.jointCount > 0) {
        impl.jointBuffers[frame].writeBytes(joints.data(), sizeof(glm::mat4) * impl.jointCount);
    }

    // Reset the fence only once we know we WILL submit (else: deadlock).
    vkResetFences(dev, 1, &impl.inFlight[frame]);

    const VkCommandBuffer cmd = impl.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);
    impl.record(cmd, imageIndex, frame, items);

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = impl.imageAvailable[frame];
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = impl.renderFinished[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    if (vkQueueSubmit2(impl.device->graphicsQueue(), 1, &submit, impl.inFlight[frame]) !=
        VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit2 failed");
    }

    const VkSwapchainKHR swapchainHandle = impl.swapchain->handle();
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &impl.renderFinished[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchainHandle;
    present.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(impl.device->presentQueue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR ||
        acquire == VK_SUBOPTIMAL_KHR) {
        impl.needRecreate = true; // rebuild at the TOP of the next frame
    } else if (presented != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    impl.frameIndex = (frame + 1) % kFramesInFlight;
}

void Renderer::setUiHook(internal::UiRenderHook* hook) { m_impl->uiHook = hook; }

void Renderer::setLighting(glm::vec3 color, glm::vec3 direction) {
    m_impl->lightColor = color;
    // A zero direction means "keep the current angle" — retint without
    // re-specifying geometry. Normalize here so callers pass a raw direction.
    if (glm::dot(direction, direction) > 1e-6f) {
        m_impl->lightDir = glm::normalize(direction);
    }
}

void Renderer::setPointLights(std::span<const PointLight> lights) {
    Impl& impl = *m_impl;
    const size_t count = std::min(lights.size(), kMaxPointLights);
    uint32_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        if (lights[i].radius > 0.0f) { // a zero-radius light lights nothing
            impl.pointLights[used++] = lights[i];
        }
    }
    impl.pointLightCount = used;
}

void Renderer::setFog(glm::vec3 color, float density) {
    m_impl->fogColor = glm::max(color, glm::vec3(0.0f));
    m_impl->fogDensity = std::max(density, 0.0f);
}

void Renderer::setExposure(float exposure) { m_impl->exposure = std::max(exposure, 0.0f); }

void Renderer::setBloom(float intensity) { m_impl->bloomIntensity = std::max(intensity, 0.0f); }

void Renderer::setSky(const Sky& sky) {
    Sky clean = sky;
    clean.zenith = glm::max(clean.zenith, glm::vec3(0.0f));
    clean.horizon = glm::max(clean.horizon, glm::vec3(0.0f));
    clean.ground = glm::max(clean.ground, glm::vec3(0.0f));
    clean.sunSize = std::clamp(clean.sunSize, 0.0f, 45.0f);
    clean.sunIntensity = std::max(clean.sunIntensity, 0.0f);
    clean.sunHalo = std::max(clean.sunHalo, 0.0f);
    clean.panoramaIntensity = std::max(clean.panoramaIntensity, 0.0f);
    m_impl->sky = clean;
}

void Renderer::setAmbient(glm::vec3 sky, glm::vec3 ground, float weight) {
    m_impl->ambientSky = glm::max(sky, glm::vec3(0.0f));
    m_impl->ambientGround = glm::max(ground, glm::vec3(0.0f));
    m_impl->ambientWeight = std::clamp(weight, 0.0f, 1.0f);
}

void Renderer::resetAmbient() {
    m_impl->ambientSky = glm::vec3(0.0f);
    m_impl->ambientGround = glm::vec3(0.0f);
    m_impl->ambientWeight = 0.0f;
}

namespace internal {
RendererVkInfo queryVkInfo(const Renderer& renderer) {
    const Renderer::Impl& impl = *renderer.m_impl;
    RendererVkInfo info{};
    info.instance = impl.context.instance();
    info.physicalDevice = impl.device->physical();
    info.device = impl.device->device();
    info.graphicsQueue = impl.device->graphicsQueue();
    info.graphicsFamily = impl.device->graphicsFamily();
    info.colorFormat = impl.swapchain->format();
    // The UI draws in the composite pass: swapchain color only, NO depth
    // attachment — its pipeline must be created to match.
    info.depthFormat = VK_FORMAT_UNDEFINED;
    info.imageCount = impl.swapchain->imageCount();
    return info;
}
} // namespace internal

} // namespace forge
