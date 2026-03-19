#include "renderer_vk.h"

#ifdef UHDR_VIEWER_VULKAN_ENABLED

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define VK_CHECK(result)                     \
    do {                                     \
        VkResult err = (result);             \
        if (err != VK_SUCCESS) {             \
            checkVkResult(err);              \
            return false;                    \
        }                                    \
    } while (0)

#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
#endif

VulkanRenderer::VulkanRenderer() {}
VulkanRenderer::~VulkanRenderer() { shutdown(); }

void VulkanRenderer::checkVkResult(VkResult err) {
    if (err == VK_SUCCESS) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallback(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char* pLayerPrefix, const char* pMessage, void* pUserData) {
    fprintf(stderr, "[vulkan] %s\n", pMessage);
    return VK_FALSE;
}
#endif

bool VulkanRenderer::init(GLFWwindow* window) {
    m_window = window;
    if (!glfwVulkanSupported()) {
        fprintf(stderr, "GLFW: Vulkan Not Supported\n");
        return false;
    }
    if (!createInstance()) return false;
    if (!createSurface(window)) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createDescriptorPool()) return false;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    if (!createSwapchain(w, h)) return false;
    if (!createRenderPass()) return false;
    if (!createFrameResources()) return false;
    if (!createCommandBuffers()) return false;
    if (!createSyncObjects()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = m_instance;
    initInfo.PhysicalDevice = m_physicalDevice;
    initInfo.Device = m_device;
    initInfo.QueueFamily = m_queueFamily;
    initInfo.Queue = m_queue;
    initInfo.PipelineCache = m_pipelineCache;
    initInfo.DescriptorPool = m_descriptorPool;
    initInfo.MinImageCount = m_minImageCount;
    initInfo.ImageCount = m_imageCount;
    initInfo.Allocator = nullptr;
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = checkVkResult;
    ImGui_ImplVulkan_Init(&initInfo);

    return true;
}

bool VulkanRenderer::createInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "UltraHDR Viewer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = layers;
#endif

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    auto vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugReportCallbackEXT");
    if (vkCreateDebugReportCallbackEXT) {
        VkDebugReportCallbackCreateInfoEXT debugInfo = {};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debugInfo.pfnCallback = debugReportCallback;
        VkDebugReportCallbackEXT callback;
        vkCreateDebugReportCallbackEXT(m_instance, &debugInfo, nullptr, &callback);
    }
#endif
    return true;
}

bool VulkanRenderer::createSurface(GLFWwindow* window) {
    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
    return true;
}

bool VulkanRenderer::selectPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) { fprintf(stderr, "No Vulkan devices found\n"); return false; }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
    m_physicalDevice = devices[0];

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qfProps.data());

    for (uint32_t i = 0; i < qfCount; i++) {
        if (qfProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
            if (presentSupport) { m_queueFamily = i; break; }
        }
    }
    if (m_queueFamily == (uint32_t)-1) { fprintf(stderr, "No queue family\n"); return false; }
    return true;
}

bool VulkanRenderer::createLogicalDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = exts;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

bool VulkanRenderer::createDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
    };
    VkDescriptorPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 100;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = poolSizes;
    VK_CHECK(vkCreateDescriptorPool(m_device, &pi, nullptr, &m_descriptorPool));
    return true;
}

bool VulkanRenderer::createSwapchain(int width, int height) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t fmtCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }
    m_swapchainFormat = chosen.format;
    m_colorSpace = chosen.colorSpace;

    uint32_t pmCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, pms.data());
    m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto pm : pms) { if (pm == VK_PRESENT_MODE_MAILBOX_KHR) { m_presentMode = pm; break; } }

    m_extent = (caps.currentExtent.width != UINT32_MAX) ? caps.currentExtent : VkExtent2D{(uint32_t)width, (uint32_t)height};

    m_minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && m_minImageCount > caps.maxImageCount)
        m_minImageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = m_surface;
    sci.minImageCount = m_minImageCount;
    sci.imageFormat = m_swapchainFormat;
    sci.imageColorSpace = m_colorSpace;
    sci.imageExtent = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = m_presentMode;
    sci.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain));
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, nullptr);
    return true;
}

bool VulkanRenderer::createRenderPass() {
    VkAttachmentDescription ca = {};
    ca.format = m_swapchainFormat;
    ca.samples = VK_SAMPLE_COUNT_1_BIT;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ca.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi = {};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &ca;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(m_device, &rpi, nullptr, &m_renderPass));
    return true;
}

bool VulkanRenderer::createFrameResources() {
    std::vector<VkImage> images(m_imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, images.data());
    m_frames.resize(m_imageCount);

    for (uint32_t i = 0; i < m_imageCount; i++) {
        m_frames[i].image = images[i];

        VkImageViewCreateInfo vi = {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_swapchainFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(m_device, &vi, nullptr, &m_frames[i].view));

        VkFramebufferCreateInfo fi = {};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = m_renderPass;
        fi.attachmentCount = 1;
        fi.pAttachments = &m_frames[i].view;
        fi.width = m_extent.width;
        fi.height = m_extent.height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_device, &fi, nullptr, &m_frames[i].framebuffer));
    }
    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    for (uint32_t i = 0; i < m_imageCount; i++) {
        VkCommandPoolCreateInfo pi = {};
        pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = m_queueFamily;
        VK_CHECK(vkCreateCommandPool(m_device, &pi, nullptr, &m_frames[i].commandPool));

        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_frames[i].commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &m_frames[i].commandBuffer));
    }
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi = {};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < m_imageCount; i++) {
        VK_CHECK(vkCreateSemaphore(m_device, &si, nullptr, &m_frames[i].imageAvailableSemaphore));
        VK_CHECK(vkCreateSemaphore(m_device, &si, nullptr, &m_frames[i].renderFinishedSemaphore));
        VK_CHECK(vkCreateFence(m_device, &fi, nullptr, &m_frames[i].inFlightFence));
    }
    return true;
}

void VulkanRenderer::cleanupSwapchain() {
    vkDeviceWaitIdle(m_device);
    for (auto& f : m_frames) {
        if (f.framebuffer) vkDestroyFramebuffer(m_device, f.framebuffer, nullptr);
        if (f.view) vkDestroyImageView(m_device, f.view, nullptr);
        if (f.inFlightFence) vkDestroyFence(m_device, f.inFlightFence, nullptr);
        if (f.imageAvailableSemaphore) vkDestroySemaphore(m_device, f.imageAvailableSemaphore, nullptr);
        if (f.renderFinishedSemaphore) vkDestroySemaphore(m_device, f.renderFinishedSemaphore, nullptr);
        if (f.commandBuffer && f.commandPool) vkFreeCommandBuffers(m_device, f.commandPool, 1, &f.commandBuffer);
        if (f.commandPool) vkDestroyCommandPool(m_device, f.commandPool, nullptr);
    }
    m_frames.clear();
    if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}

void VulkanRenderer::shutdown() {
    if (m_device) vkDeviceWaitIdle(m_device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    cleanupSwapchain();
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_device) vkDestroyDevice(m_device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);

    m_instance = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
    m_window = nullptr;
}

void VulkanRenderer::recreateSwapchain(int w, int h) {
    if (w <= 0 || h <= 0) return;
    vkDeviceWaitIdle(m_device);
    cleanupSwapchain();
    createSwapchain(w, h);
    createFrameResources();
    createCommandBuffers();
    createSyncObjects();
}

void VulkanRenderer::newFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderer::render() {
    ImGui::Render();

    int fbW, fbH;
    glfwGetFramebufferSize(m_window, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) return;

    if (m_swapChainRebuild) {
        recreateSwapchain(fbW, fbH);
        m_swapChainRebuild = false;
    }

    uint32_t frameIdx = m_currentFrame;
    auto& frame = m_frames[frameIdx];

    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult res = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                          frame.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        m_swapChainRebuild = true;
        return;
    }
    checkVkResult(res);

    vkResetFences(m_device, 1, &frame.inFlightFence);

    VkCommandBuffer cmd = frame.commandBuffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_renderPass;
    rp.framebuffer = m_frames[imageIndex].framebuffer;
    rp.renderArea.extent = m_extent;
    rp.clearValueCount = 1;
    rp.pClearValues = &m_clearValue;
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &frame.imageAvailableSemaphore;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &frame.renderFinishedSemaphore;
    vkQueueSubmit(m_queue, 1, &si, frame.inFlightFence);

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &frame.renderFinishedSemaphore;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &imageIndex;

    res = vkQueuePresentKHR(m_queue, &pi);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        m_swapChainRebuild = true;
    }

    m_currentFrame = (frameIdx + 1) % m_imageCount;
}

// ---- Texture helpers ----

uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return 0;
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
    VkCommandPoolCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.queueFamilyIndex = m_queueFamily;
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(m_device, &pi, nullptr, &m_tempCommandPool);

    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_tempCommandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_device, &ai, &cmd);

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_tempCommandPool, 1, &cmd);
    vkDestroyCommandPool(m_device, m_tempCommandPool, nullptr);
    m_tempCommandPool = VK_NULL_HANDLE;
}

void VulkanRenderer::transitionImageLayout(VkImage image, VkImageLayout oldL, VkImageLayout newL) {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldL;
    b.newLayout = newL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags src, dst;
    if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    endSingleTimeCommands(cmd);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h) {
    VkCommandBuffer cmd = beginSingleTimeCommands();
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, buf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(cmd);
}

void VulkanRenderer::uploadTextureData(VulkanTexture& tex, const ImageData& image) {
    VkDeviceSize size = (VkDeviceSize)image.width * image.height * 4 * sizeof(uint8_t);

    // Staging buffer
    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging;
    vkCreateBuffer(m_device, &bi, nullptr, &staging);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(m_device, staging, &mr);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    vkAllocateMemory(m_device, &ai, nullptr, &stagingMem);
    vkBindBufferMemory(m_device, staging, stagingMem, 0);

    void* data;
    vkMapMemory(m_device, stagingMem, 0, size, 0, &data);
    memcpy(data, image.pixels, (size_t)size);
    vkUnmapMemory(m_device, stagingMem);

    // Image
    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = {(uint32_t)image.width, (uint32_t)image.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    vkCreateImage(m_device, &ii, nullptr, &tex.image);

    vkGetImageMemoryRequirements(m_device, tex.image, &mr);
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(m_device, &ai, nullptr, &tex.memory);
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    transitionImageLayout(tex.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(staging, tex.image, (uint32_t)image.width, (uint32_t)image.height);
    transitionImageLayout(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    // View
    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = tex.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_device, &vi, nullptr, &tex.view);

    // Sampler
    VkSamplerCreateInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    vkCreateSampler(m_device, &si, nullptr, &tex.sampler);

    tex.descriptorSet = ImGui_ImplVulkan_AddTexture(tex.sampler, tex.view,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex.width = image.width;
    tex.height = image.height;
}

void* VulkanRenderer::createTexture(const ImageData& image) {
    if (!image.pixels || image.width <= 0 || image.height <= 0) return nullptr;
    VulkanTexture* tex = new VulkanTexture();
    uploadTextureData(*tex, image);
    return tex;
}

void VulkanRenderer::destroyTexture(void* texture) {
    if (!texture) return;
    VulkanTexture* tex = (VulkanTexture*)texture;
    vkDeviceWaitIdle(m_device);
    if (tex->sampler) vkDestroySampler(m_device, tex->sampler, nullptr);
    if (tex->view) vkDestroyImageView(m_device, tex->view, nullptr);
    if (tex->image) vkDestroyImage(m_device, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(m_device, tex->memory, nullptr);
    delete tex;
}

void VulkanRenderer::updateTexture(void* texture, const ImageData& image) {
    if (!texture) return;
    VulkanTexture* tex = (VulkanTexture*)texture;
    vkDeviceWaitIdle(m_device);
    if (tex->sampler) vkDestroySampler(m_device, tex->sampler, nullptr);
    if (tex->view) vkDestroyImageView(m_device, tex->view, nullptr);
    if (tex->image) vkDestroyImage(m_device, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(m_device, tex->memory, nullptr);
    uploadTextureData(*tex, image);
}

std::unique_ptr<Renderer> Renderer::createVulkan() {
    return std::make_unique<VulkanRenderer>();
}

#else

std::unique_ptr<Renderer> Renderer::createVulkan() { return nullptr; }

#endif
