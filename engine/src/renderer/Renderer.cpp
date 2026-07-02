// engine/src/renderer/Renderer.cpp
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

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace forge {

namespace {

// CPU records frame N+1 while the GPU draws frame N. Two is the sweet spot:
// three adds latency, one serializes CPU and GPU.
constexpr uint32_t kFramesInFlight = 2;

// Directional shadow map resolution (ADR-014). One map, one light, until a
// real scene demands cascades.
constexpr uint32_t kShadowMapSize = 2048;

// Per-frame GPU data (ADR-012). Layout mirrors the std140 block in
// mesh.vert/frag: mat4s then vec4s — nothing that std140 would repack.
struct FrameUbo {
    glm::mat4 viewProj;
    glm::mat4 lightViewProj; // world -> light clip (shadow render AND sample)
    glm::vec4 cameraPos;
    glm::vec4 lightDirection; // xyz: surface -> light, normalized
    glm::vec4 lightColor;     // rgb * intensity
};
static_assert(sizeof(FrameUbo) == 176, "must match the std140 block in mesh shaders");

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
                   VkImageUsageFlags usage, VkImage& outImage, VkDeviceMemory& outMemory) {
    const VkDevice dev = device.device();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
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

    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kFramesInFlight> descriptorSets{};
    std::array<GpuBuffer, kFramesInFlight> frameUbos;

    // Built-in placeholder albedo until P5 brings real assets (ADR-013).
    std::unique_ptr<VulkanTexture> albedoTexture;

    // Directional shadow map (ADR-014): persistent, NOT tied to the swapchain.
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE; // comparison sampler (PCF)

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE; // depth-only, light's POV
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // The one mesh (see uploadMesh contract in the header).
    GpuBuffer vertexBuffer;
    GpuBuffer indexBuffer;
    uint32_t indexCount = 0;

    // Per frame-in-flight: recording + acquire sync.
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    // Per swapchain IMAGE (not per frame): present waits on the semaphore
    // tied to the image being presented — sidesteps semaphore-reuse hazards.
    std::vector<VkSemaphore> renderFinished;

    uint32_t frameIndex = 0;
    bool needRecreate = false;

    Impl(Window& win, VulkanContext& ctx) : window(win), context(ctx) {}

    void createDepthResources();
    void destroyDepthResources();
    void createShadowResources();
    void createDescriptors();
    void createPipeline();
    void createShadowPipeline();
    void createFrameObjects();
    void createImageSemaphores();
    void recreateSwapchain();
    void record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frame,
                std::span<const glm::mat4> models) const;
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
                  shadowImage, shadowMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &viewInfo, nullptr, &shadowView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (shadow) failed");
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

    // Binding 0: per-frame UBO (both stages). Binding 1: albedo texture.
    // Binding 2: shadow map (comparison sampler).
    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &descriptorLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * kFramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
    layouts.fill(descriptorLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = kFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(dev, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets failed");
    }

    // One persistently-mapped UBO per frame in flight, so the CPU never
    // writes a buffer the GPU is still reading (the fence guards the slot).
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameUbos[i] =
            GpuBuffer(*device, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDescriptorBufferInfo bufferInfo{frameUbos[i].handle(), 0, sizeof(FrameUbo)};
        VkDescriptorImageInfo albedoInfo{albedoTexture->sampler(), albedoTexture->view(),
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo shadowInfo{shadowSampler, shadowView,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &albedoInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].pImageInfo = &shadowInfo;
        vkUpdateDescriptorSets(dev, 3, writes, 0, nullptr);
    }
}

void Renderer::Impl::createPipeline() {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        makeShaderModule(dev, shaders::kMeshVert, sizeof(shaders::kMeshVert));
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

    // Vertex layout: interleaved position + normal (forge::Vertex).
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attributes[3]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attributes[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attributes;

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

    // Per-draw data rides in push constants: model matrix = 64 of the 128
    // bytes Vulkan guarantees (ADR-012).
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }

    // Dynamic rendering (ADR-010): the pipeline declares attachment formats
    // directly — no VkRenderPass object anywhere in the engine.
    const VkFormat colorFormat = swapchain->format();
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

    const VkResult result =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(dev, vert, nullptr); // safe: pipeline keeps its own copy
    vkDestroyShaderModule(dev, frag, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }
}

void Renderer::Impl::createShadowPipeline() {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        makeShaderModule(dev, shaders::kShadowVert, sizeof(shaders::kShadowVert));

    // ONE stage: no fragment shader — the depth write is the whole job.
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    stage.module = vert;
    stage.pName = "main";

    // Same vertex buffer, but the shader consumes only position.
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                                offsetof(Vertex, position)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

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
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;

    const VkResult result =
        vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline);
    vkDestroyShaderModule(dev, vert, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines (shadow) failed");
    }
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
    destroyDepthResources();
    swapchain.reset(); // old swapchain must die before the surface is reused

    const VkExtent2D extent = framebufferExtent(window);
    swapchain = std::make_unique<VulkanSwapchain>(*device, surface, extent.width, extent.height);
    createDepthResources(); // depth must always match the swapchain extent
    createImageSemaphores();
    needRecreate = false;
    // Pipeline survives: viewport/scissor are dynamic and the attachment
    // formats do not change in practice (if they ever do, recreate it here).
}

void Renderer::Impl::record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frame,
                            std::span<const glm::mat4> models) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    const VkBuffer vb = vertexBuffer.handle();
    const VkDeviceSize vbOffset = 0;

    // ---- Pass 1: shadow map, from the light's point of view (ADR-014).
    // UNDEFINED discard is fine (cleared every frame) and doubles as the
    // frame-N/N+1 execution barrier on the shared map.
    transitionImage(
        cmd, shadowImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo shadowDepth{};
    shadowDepth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    shadowDepth.imageView = shadowView;
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
    const VkViewport shadowViewport{
        0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize),
        0.0f, 1.0f};
    const VkRect2D shadowScissor{{0, 0}, {kShadowMapSize, kShadowMapSize}};
    vkCmdSetViewport(cmd, 0, 1, &shadowViewport);
    vkCmdSetScissor(cmd, 0, 1, &shadowScissor);
    if (indexCount > 0 && !models.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSets[frame], 0, nullptr);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
        for (const glm::mat4& model : models) {
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(glm::mat4), &model);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
    }
    vkCmdEndRendering(cmd);

    // Depth writes done -> fragment shader reads (pass 2 samples the map).
    transitionImage(
        cmd, shadowImage, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    // ---- Pass 2: the scene, from the camera.
    // Swapchain image arrives in UNDEFINED; make it a color attachment.
    transitionImage(cmd, swapchain->image(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
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
    colorAttachment.imageView = swapchain->view(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // near-black blue

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

    if (indexCount > 0 && !models.empty()) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSets[frame], 0, nullptr);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &vbOffset);
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
        for (const glm::mat4& model : models) {
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(glm::mat4), &model);
            vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
        }
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
    m_impl->albedoTexture = std::make_unique<VulkanTexture>(*m_impl->device, m_impl->commandPool,
                                                            256, 256, makeCheckerTexels(256, 32));
    m_impl->createShadowResources();
    m_impl->createDescriptors();
    m_impl->createPipeline();
    m_impl->createShadowPipeline();
    FORGE_INFO("renderer ready: dynamic rendering, depth {}, {} frames in flight, "
               "256x256 checker albedo (9 mips), {}x{} shadow map",
               m_impl->depthFormat == VK_FORMAT_D32_SFLOAT ? "D32_SFLOAT" : "D+S fallback",
               kFramesInFlight, kShadowMapSize, kShadowMapSize);
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
        vkDestroyPipelineLayout(dev, m_impl->pipelineLayout, nullptr);
        vkDestroyDescriptorPool(dev, m_impl->descriptorPool, nullptr); // frees the sets
        vkDestroyDescriptorSetLayout(dev, m_impl->descriptorLayout, nullptr);
        vkDestroySampler(dev, m_impl->shadowSampler, nullptr);
        vkDestroyImageView(dev, m_impl->shadowView, nullptr);
        vkDestroyImage(dev, m_impl->shadowImage, nullptr);
        vkFreeMemory(dev, m_impl->shadowMemory, nullptr);
        m_impl->albedoTexture.reset();
        m_impl->vertexBuffer = GpuBuffer{};
        m_impl->indexBuffer = GpuBuffer{};
        for (auto& ubo : m_impl->frameUbos) {
            ubo = GpuBuffer{};
        }
        m_impl->destroyDepthResources();
    }
    m_impl->swapchain.reset(); // strict reverse order: swapchain, device, surface
    m_impl->device.reset();
    if (m_impl->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_impl->context.instance(), m_impl->surface, nullptr);
    }
}

void Renderer::uploadMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices) {
    Impl& impl = *m_impl;
    if (impl.indexCount != 0) {
        // One mesh until P5's asset pipeline. Replacing would need a
        // waitIdle + buffer swap — design that when assets exist, not before.
        throw std::runtime_error("uploadMesh may only be called once (P5 owns real meshes)");
    }
    const VkDeviceSize vertexBytes = vertices.size_bytes();
    const VkDeviceSize indexBytes = indices.size_bytes();
    impl.vertexBuffer =
        GpuBuffer(*impl.device, vertexBytes,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    impl.indexBuffer =
        GpuBuffer(*impl.device, indexBytes,
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    uploadToBuffer(*impl.device, impl.commandPool, impl.vertexBuffer.handle(), vertices.data(),
                   vertexBytes);
    uploadToBuffer(*impl.device, impl.commandPool, impl.indexBuffer.handle(), indices.data(),
                   indexBytes);
    impl.indexCount = static_cast<uint32_t>(indices.size());
    FORGE_INFO("mesh uploaded: {} vertices, {} indices ({} KiB on GPU)", vertices.size(),
               indices.size(), (vertexBytes + indexBytes + 1023) / 1024);
}

void Renderer::drawFrame(const Camera& camera, std::span<const glm::mat4> modelMatrices) {
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

    // Light matrices (ADR-014): ortho box around the demo scene, eye pushed
    // back along the light direction. NO Y-flip here — the shadow map is
    // rendered AND sampled through this same matrix, so the convention
    // cancels out; only the swapchain path needs the flip.
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.35f));
    const glm::mat4 lightView = glm::lookAt(lightDir * 10.0f, glm::vec3(0.0f), glm::vec3(0, 1, 0));
    const glm::mat4 lightProj = glm::ortho(-6.0f, 6.0f, -6.0f, 6.0f, 0.1f, 20.0f);

    FrameUbo ubo{};
    ubo.viewProj = proj * view;
    ubo.lightViewProj = lightProj * lightView;
    ubo.cameraPos = glm::vec4(camera.position, 1.0f);
    ubo.lightDirection = glm::vec4(lightDir, 0.0f);
    ubo.lightColor = glm::vec4(3.0f, 2.9f, 2.7f, 1.0f); // warm key light, HDR intensity
    impl.frameUbos[frame].writeBytes(&ubo, sizeof(ubo));

    // Reset the fence only once we know we WILL submit (else: deadlock).
    vkResetFences(dev, 1, &impl.inFlight[frame]);

    const VkCommandBuffer cmd = impl.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);
    impl.record(cmd, imageIndex, frame, modelMatrices);

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

} // namespace forge
