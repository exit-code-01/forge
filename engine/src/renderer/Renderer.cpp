// engine/src/renderer/Renderer.cpp
#include "VulkanBuffer.hpp"
#include "VulkanCommon.hpp" // volk first: gives glfw3.h the Vulkan types below
#include "VulkanDevice.hpp"
#include "VulkanSwapchain.hpp"

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

// Per-frame GPU data (ADR-012). Layout mirrors the std140 block in
// mesh.vert/frag: mat4 then vec4s — nothing that std140 would repack.
struct FrameUbo {
    glm::mat4 viewProj;
    glm::vec4 cameraPos;
    glm::vec4 lightDirection; // xyz: surface -> light, normalized
    glm::vec4 lightColor;     // rgb * intensity
};
static_assert(sizeof(FrameUbo) == 112, "must match the std140 block in mesh shaders");

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

// Sync2 image-layout transition, the only barrier shape P2/P3 need.
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
                     VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
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

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
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
    void createDescriptors();
    void createPipeline();
    void createFrameObjects();
    void createImageSemaphores();
    void recreateSwapchain();
    void record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frame,
                const glm::mat4& model) const;
};

void Renderer::Impl::createDepthResources() {
    const VkDevice dev = device->device();
    depthFormat = findDepthFormat(device->physical());

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent = {swapchain->extent().width, swapchain->extent().height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &imageInfo, nullptr, &depthImage) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage (depth) failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(dev, depthImage, &requirements);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(device->physical(), &memProps);
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
        throw std::runtime_error("no device-local memory for depth buffer");
    }
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    if (vkAllocateMemory(dev, &allocInfo, nullptr, &depthMemory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (depth) failed");
    }
    vkBindImageMemory(dev, depthImage, depthMemory, 0);

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

    // One binding: the per-frame UBO, visible to both stages.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &descriptorLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kFramesInFlight;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
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
    VkVertexInputAttributeDescription attributes[2]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 2;
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
                            const glm::mat4& model) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

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

    if (indexCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                &descriptorSets[frame], 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
                           &model);
        const VkBuffer vb = vertexBuffer.handle();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
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
    m_impl->createDescriptors();
    m_impl->createPipeline();
    m_impl->createFrameObjects();
    FORGE_INFO("renderer ready: dynamic rendering, depth {}, {} frames in flight",
               m_impl->depthFormat == VK_FORMAT_D32_SFLOAT ? "D32_SFLOAT" : "D+S fallback",
               kFramesInFlight);
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
        vkDestroyPipelineLayout(dev, m_impl->pipelineLayout, nullptr);
        vkDestroyDescriptorPool(dev, m_impl->descriptorPool, nullptr); // frees the sets
        vkDestroyDescriptorSetLayout(dev, m_impl->descriptorLayout, nullptr);
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

void Renderer::drawFrame(const Camera& camera, const glm::mat4& modelMatrix) {
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
    FrameUbo ubo{};
    ubo.viewProj = proj * view;
    ubo.cameraPos = glm::vec4(camera.position, 1.0f);
    ubo.lightDirection = glm::vec4(glm::normalize(glm::vec3(0.4f, 1.0f, 0.3f)), 0.0f);
    ubo.lightColor = glm::vec4(3.0f, 2.9f, 2.7f, 1.0f); // warm key light, HDR intensity
    impl.frameUbos[frame].writeBytes(&ubo, sizeof(ubo));

    // Reset the fence only once we know we WILL submit (else: deadlock).
    vkResetFences(dev, 1, &impl.inFlight[frame]);

    const VkCommandBuffer cmd = impl.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);
    impl.record(cmd, imageIndex, frame, modelMatrix);

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
