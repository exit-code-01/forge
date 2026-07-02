// engine/src/renderer/Renderer.cpp
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

#include "forge_gen/TriangleShaders.hpp" // generated from checked-in .spv (ADR-011)

#include <array>
#include <stdexcept>
#include <vector>

namespace forge {

namespace {

// CPU records frame N+1 while the GPU draws frame N. Two is the sweet spot:
// three adds latency, one serializes CPU and GPU.
constexpr uint32_t kFramesInFlight = 2;

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

// Sync2 image-layout transition, the only barrier shape P2 needs.
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                     VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
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
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

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

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
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

    Impl(Window& win, VulkanContext& ctx) : window(win), context(ctx) {}

    void createPipeline();
    void createFrameObjects();
    void createImageSemaphores();
    void recreateSwapchain();
    void record(VkCommandBuffer cmd, uint32_t imageIndex) const;
};

void Renderer::Impl::createPipeline() {
    const VkDevice dev = device->device();
    const VkShaderModule vert =
        makeShaderModule(dev, shaders::kTriangleVert, sizeof(shaders::kTriangleVert));
    const VkShaderModule frag =
        makeShaderModule(dev, shaders::kTriangleFrag, sizeof(shaders::kTriangleFrag));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex buffers (geometry is in the shader) and no descriptors yet.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
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
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }

    // Dynamic rendering (ADR-010): the pipeline declares the attachment
    // format directly — no VkRenderPass object anywhere in the engine.
    const VkFormat colorFormat = swapchain->format();
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
    swapchain.reset(); // old swapchain must die before the surface is reused

    const VkExtent2D extent = framebufferExtent(window);
    swapchain = std::make_unique<VulkanSwapchain>(*device, surface, extent.width, extent.height);
    createImageSemaphores();
    needRecreate = false;
    // Pipeline survives: viewport/scissor are dynamic and the surface format
    // does not change in practice (if it ever does, pipeline creation moves here).
}

void Renderer::Impl::record(VkCommandBuffer cmd, uint32_t imageIndex) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Swapchain image arrives in UNDEFINED; make it a color attachment.
    transitionImage(cmd, swapchain->image(imageIndex), VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                    VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain->view(imageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // near-black blue

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = {{0, 0}, swapchain->extent()};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0); // THE triangle
    vkCmdEndRendering(cmd);

    // Hand the image to the presentation engine.
    transitionImage(cmd, swapchain->image(imageIndex), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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
    m_impl->createPipeline();
    m_impl->createFrameObjects();
    FORGE_INFO("renderer ready: dynamic rendering, {} frames in flight", kFramesInFlight);
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
    }
    m_impl->swapchain.reset(); // strict reverse order: swapchain, device, surface
    m_impl->device.reset();
    if (m_impl->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_impl->context.instance(), m_impl->surface, nullptr);
    }
}

void Renderer::drawFrame() {
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

    // Reset the fence only once we know we WILL submit (else: deadlock).
    vkResetFences(dev, 1, &impl.inFlight[frame]);

    const VkCommandBuffer cmd = impl.commandBuffers[frame];
    vkResetCommandBuffer(cmd, 0);
    impl.record(cmd, imageIndex);

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
