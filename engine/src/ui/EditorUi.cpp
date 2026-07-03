// engine/src/ui/EditorUi.cpp — ImGui lifecycle + backends live here, once.
#include "forge/ui/EditorUi.hpp"
#include "forge/core/Log.hpp"
#include "forge/platform/Window.hpp"
#include "forge/renderer/Renderer.hpp"

#include "../renderer/RendererInternal.hpp"

// volk is loaded by the renderer; IMGUI_IMPL_VULKAN_USE_VOLK (defined on the
// imgui target) makes the backend share that dispatch table.
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdexcept>

namespace forge {

struct EditorUi::Impl final : internal::UiRenderHook {
    Renderer& renderer;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    bool frameOpen = false; // NewFrame'd but not yet Render'd

    explicit Impl(Renderer& r) : renderer(r) {}

    void record(VkCommandBuffer cmd) override {
        ImGui::Render();
        frameOpen = false;
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }
};

EditorUi::EditorUi(Window& window, Renderer& renderer) : m_impl(std::make_unique<Impl>(renderer)) {
    const internal::RendererVkInfo vk = internal::queryVkInfo(renderer);
    m_impl->device = vk.device;

    // ImGui's own descriptor pool: FREE_DESCRIPTOR_SET because the backend
    // allocates/frees per-texture sets (fonts) itself.
    // 1.92.x allocates split SAMPLER/SAMPLED_IMAGE sets as well as combined.
    const VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 16},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 48;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(vk.device, &poolInfo, nullptr, &m_impl->pool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool (imgui) failed");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr; // no imgui.ini litter in the repo

    // install_callbacks=true CHAINS our Window's GLFW callbacks (they were
    // installed first), so engine Input keeps working alongside the UI.
    ImGui_ImplGlfw_InitForVulkan(window.nativeHandle(), true);

    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = vk.instance;
    init.PhysicalDevice = vk.physicalDevice;
    init.Device = vk.device;
    init.QueueFamily = vk.graphicsFamily;
    init.Queue = vk.graphicsQueue;
    init.DescriptorPool = m_impl->pool;
    init.MinImageCount = 2;
    init.ImageCount = vk.imageCount;
    // We draw inside the renderer's dynamic-rendering main pass, so the UI
    // pipeline must declare the SAME attachment formats (1.92.8 keeps these
    // in PipelineInfoMain).
    init.UseDynamicRendering = true;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoMain.PipelineRenderingCreateInfo = {};
    init.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    static VkFormat s_colorFormat; // pointed-to storage must outlive pipeline creation
    s_colorFormat = vk.colorFormat;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &s_colorFormat;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = vk.depthFormat;
    if (!ImGui_ImplVulkan_Init(&init)) {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }

    renderer.setUiHook(m_impl.get());
    FORGE_INFO("editor UI ready (ImGui {})", IMGUI_VERSION);
}

EditorUi::~EditorUi() {
    m_impl->renderer.setUiHook(nullptr);
    vkDeviceWaitIdle(m_impl->device); // UI resources may be in flight
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(m_impl->device, m_impl->pool, nullptr);
}

void EditorUi::beginFrame() {
    if (m_impl->frameOpen) {
        ImGui::EndFrame(); // drawFrame skipped (minimized): discard cleanly
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_impl->frameOpen = true;
}

bool EditorUi::wantCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }
bool EditorUi::wantCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }

} // namespace forge
