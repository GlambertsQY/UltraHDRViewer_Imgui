#include "renderer_vk.h"
#include "log.h"
#include "half_float.h"
#include "colorspace.h"
#include "shaders/hdr_quad_vert_spv.h"
#include "shaders/hdr_quad_frag_spv.h"

#ifdef UHDR_VIEWER_VULKAN_ENABLED

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <GLFW/glfw3.h>

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
    LOG_ERROR("[vulkan] Error: VkResult = %d", err);
    if (err < 0) abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportCallback(
    VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
    uint64_t object, size_t location, int32_t messageCode,
    const char* pLayerPrefix, const char* pMessage, void* pUserData) {
    LOG_ERROR("[vulkan] %s", pMessage);
    return VK_FALSE;
}
#endif

bool VulkanRenderer::init(GLFWwindow* window) {
    m_window = window;
    if (!glfwVulkanSupported()) {
        LOG_ERROR("GLFW: Vulkan Not Supported");
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

    LOG_INFO("[vulkan] HDR output availability: scRGB=%s HDR10=%s",
             m_hdrScRGBSupported ? "yes" : "no", m_hdr10Supported ? "yes" : "no");

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
    initInfo.RenderPass = m_renderPass;
    initInfo.Subpass = 0;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
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

    // HDR output needs scRGB/HDR10 swapchain color spaces. Probe before enabling:
    // adding an unavailable extension fails instance creation on old drivers.
    {
        uint32_t extPropCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extPropCount, nullptr);
        std::vector<VkExtensionProperties> extProps(extPropCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extPropCount, extProps.data());
        for (auto& ep : extProps) {
            if (strcmp(ep.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0) {
                extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                LOG_INFO("[vulkan] %s enabled", VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
                break;
            }
        }
    }

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
    if (count == 0) { LOG_ERROR("No Vulkan devices found"); return false; }

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
    if (m_queueFamily == (uint32_t)-1) { LOG_ERROR("No queue family"); return false; }
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_physicalDeviceProperties);
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

// Test hook: parse UHDR_MOCK_HDR_OUTPUT once. Bit 1 = scrgb, bit 2 = hdr10.
static int mockHdrOutputEnv() {
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
        const char* e = getenv("UHDR_MOCK_HDR_OUTPUT");
        if (e) {
            if (strstr(e, "scrgb")) cached |= 1;
            if (strstr(e, "hdr10")) cached |= 2;
            if (strstr(e, "both")) cached |= 3;
        }
    }
    return cached;
}

void VulkanRenderer::querySurfaceFormats() {
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, formats.data());

    m_hdrScRGBSupported = false;
    m_hdr10Supported = false;
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_R16G16B16A16_SFLOAT &&
            f.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
            m_hdrScRGBSupported = true;
        if (f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
            f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
            m_hdr10Supported = true;
    }

    // Test hook for SDR development machines: UHDR_MOCK_HDR_OUTPUT=scrgb|hdr10|both
    // pretends the display advertises HDR swapchain formats. The real swapchain is
    // still created with the SDR format (drivers reject HDR formats on SDR screens),
    // but the full HDR activation path (mode selection, pipeline, HDR texture,
    // shaders, style baking, toggle/fallback logic) runs for verification.
    int mock = mockHdrOutputEnv();
    if (mock & 1) m_hdrScRGBSupported = true;
    if (mock & 2) m_hdr10Supported = true;
}

bool VulkanRenderer::createSwapchain(int width, int height) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    querySurfaceFormats();

    uint32_t fmtCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    bool found = false;
    if (m_hdrMode == HdrMode::ScRGB) {
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_R16G16B16A16_SFLOAT &&
                f.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
                chosen = f; found = true; break;
            }
        }
    } else if (m_hdrMode == HdrMode::HDR10) {
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                chosen = f; found = true; break;
            }
        }
    }
    if (m_hdrMode != HdrMode::Off && !found) {
        if (mockHdrOutputEnv() != 0) {
            // MOCK: keep the logical HDR mode active; the real swapchain stays SDR
            // (HDR quad values > 1.0 will simply be clamped by the UNORM attachment).
            LOG_ERROR("[mock] HDR format not present on this display; running HDR path on SDR swapchain");
        } else {
            // Target HDR format vanished (e.g. window moved to an SDR display): fall back
            LOG_ERROR("HDR swapchain format no longer available, falling back to SDR");
            m_hdrMode = HdrMode::Off;
        }
    }
    if (m_hdrMode == HdrMode::Off) {
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f; break;
            }
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
    // Idempotent: ~VulkanRenderer() also calls shutdown() after Application::shutdown()
    // already did - a second ImGui_ImplVulkan_Shutdown() asserts (bd == nullptr).
    if (m_instance == VK_NULL_HANDLE && m_device == VK_NULL_HANDLE) return;
    if (m_device) vkDeviceWaitIdle(m_device);

    if (m_hdrTextureSet) { destroyTexture((void*)m_hdrTextureSet); m_hdrTextureSet = VK_NULL_HANDLE; }
    destroyHdrPipeline();

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

    // HDR image quad (drawn first; ImGui UI renders on top)
    if (m_hdrMode != HdrMode::Off && m_hdrDrawRectValid && m_hdrTextureSet && m_hdrPipeline) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hdrPipeline);

        VkViewport vp = {};
        vp.width = (float)m_extent.width;
        vp.height = (float)m_extent.height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc = {};
        sc.extent = m_extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);

        HdrPushConstants pc;
        pc.rectMin[0] = m_hdrDrawRect[0];
        pc.rectMin[1] = m_hdrDrawRect[1];
        pc.rectMax[0] = m_hdrDrawRect[2];
        pc.rectMax[1] = m_hdrDrawRect[3];
        pc.fbSize[0] = (float)m_extent.width;
        pc.fbSize[1] = (float)m_extent.height;
        pc.mode = (m_hdrMode == HdrMode::ScRGB) ? 0 : 1;
        vkCmdPushConstants(cmd, m_hdrPipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hdrPipelineLayout,
                                0, 1, &m_hdrTextureSet, 0, nullptr);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

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

void VulkanRenderer::uploadTextureData(VulkanTexture& tex, const void* pixels, int width, int height,
                                       VkFormat format, VkDeviceSize byteSize) {
    VkDeviceSize size = byteSize;

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
    memcpy(data, pixels, (size_t)size);
    vkUnmapMemory(m_device, stagingMem);

    // Image
    VkImageCreateInfo ii = {};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = {(uint32_t)width, (uint32_t)height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.format = format;
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
    copyBufferToImage(staging, tex.image, (uint32_t)width, (uint32_t)height);
    transitionImageLayout(tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    // View
    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = tex.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
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

    tex.width = width;
    tex.height = height;
}

void* VulkanRenderer::createTexture(const ImageData& image) {
    if (image.pixels.empty() || image.width <= 0 || image.height <= 0) return nullptr;
    if ((uint32_t)image.width > m_physicalDeviceProperties.limits.maxImageDimension2D ||
        (uint32_t)image.height > m_physicalDeviceProperties.limits.maxImageDimension2D) {
        LOG_ERROR("Image %dx%d exceeds GPU texture limit %u", image.width, image.height,
                  m_physicalDeviceProperties.limits.maxImageDimension2D);
        return nullptr;
    }
    VulkanTexture* tex = new VulkanTexture();
    uploadTextureData(*tex, image.pixels.data(), image.width, image.height,
                      VK_FORMAT_R8G8B8A8_UNORM, image.dataSize());
    tex->descriptorSet = ImGui_ImplVulkan_AddTexture(tex->sampler, tex->view,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    tex->descriptorFromImGui = true;
    m_textures[tex->descriptorSet] = tex;
    return (void*)tex->descriptorSet;  // usable directly as ImTextureID
}

void VulkanRenderer::destroyTexture(void* texture) {
    if (!texture) return;
    VkDescriptorSet set = (VkDescriptorSet)texture;
    auto it = m_textures.find(set);
    if (it == m_textures.end()) { LOG_ERROR("destroyTexture: unknown texture %p", texture); return; }
    VulkanTexture* tex = it->second;
    vkDeviceWaitIdle(m_device);
    if (set == m_hdrTextureSet) m_hdrTextureSet = VK_NULL_HANDLE;
    if (tex->descriptorFromImGui) ImGui_ImplVulkan_RemoveTexture(set);
    else vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &set);
    if (tex->sampler) vkDestroySampler(m_device, tex->sampler, nullptr);
    if (tex->view) vkDestroyImageView(m_device, tex->view, nullptr);
    if (tex->image) vkDestroyImage(m_device, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(m_device, tex->memory, nullptr);
    m_textures.erase(it);
    delete tex;
}

void VulkanRenderer::updateTexture(void* texture, const ImageData& image) {
    if (!texture) return;
    VkDescriptorSet set = (VkDescriptorSet)texture;
    auto it = m_textures.find(set);
    if (it == m_textures.end()) { LOG_ERROR("updateTexture: unknown texture %p", texture); return; }
    VulkanTexture* tex = it->second;
    vkDeviceWaitIdle(m_device);
    if (tex->sampler) vkDestroySampler(m_device, tex->sampler, nullptr);
    if (tex->view) vkDestroyImageView(m_device, tex->view, nullptr);
    if (tex->image) vkDestroyImage(m_device, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(m_device, tex->memory, nullptr);
    uploadTextureData(*tex, image.pixels.data(), image.width, image.height,
                      VK_FORMAT_R8G8B8A8_UNORM, image.dataSize());
    // Reuse the same descriptor set (stable ImTextureID): rewrite it with the new view
    VkDescriptorImageInfo ii = {};
    ii.sampler = tex->sampler;
    ii.imageView = tex->view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

// ---- HDR textures ----

void VulkanRenderer::ensureHdrDescSetLayout() {
    if (m_hdrDescSetLayout) return;
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &binding;
    vkCreateDescriptorSetLayout(m_device, &ci, nullptr, &m_hdrDescSetLayout);
}

void VulkanRenderer::allocateHdrDescriptor(VulkanTexture& tex) {
    ensureHdrDescSetLayout();
    if (tex.descriptorSet == VK_NULL_HANDLE) {
        VkDescriptorSetAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_hdrDescSetLayout;
        vkAllocateDescriptorSets(m_device, &ai, &tex.descriptorSet);
        tex.descriptorFromImGui = false;
    }

    VkDescriptorImageInfo ii = {};
    ii.sampler = tex.sampler;
    ii.imageView = tex.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = tex.descriptorSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
}

void* VulkanRenderer::createTextureHDR(const HDRData& image) {
    if (image.pixels.empty() || image.width <= 0 || image.height <= 0) return nullptr;
    if ((uint32_t)image.width > m_physicalDeviceProperties.limits.maxImageDimension2D ||
        (uint32_t)image.height > m_physicalDeviceProperties.limits.maxImageDimension2D) {
        LOG_ERROR("HDR image %dx%d exceeds GPU texture limit %u", image.width, image.height,
                  m_physicalDeviceProperties.limits.maxImageDimension2D);
        return nullptr;
    }
    // Convert linear float -> half float on CPU (w*h*4 channels)
    std::vector<uint16_t> halfPixels(image.pixels.size());
    for (size_t i = 0; i < image.pixels.size(); i++)
        halfPixels[i] = floatToHalf(image.pixels[i]);

    VulkanTexture* tex = new VulkanTexture();
    uploadTextureData(*tex, halfPixels.data(), image.width, image.height,
                      VK_FORMAT_R16G16B16A16_SFLOAT,
                      (VkDeviceSize)halfPixels.size() * sizeof(uint16_t));
    allocateHdrDescriptor(*tex);
    m_textures[tex->descriptorSet] = tex;
    m_hdrTextureSet = tex->descriptorSet;
    return (void*)tex->descriptorSet;
}

void VulkanRenderer::updateTextureHDR(void* texture, const HDRData& image) {
    if (!texture || image.pixels.empty()) return;
    VkDescriptorSet set = (VkDescriptorSet)texture;
    auto it = m_textures.find(set);
    if (it == m_textures.end()) { LOG_ERROR("updateTextureHDR: unknown texture %p", texture); return; }
    VulkanTexture* tex = it->second;
    vkDeviceWaitIdle(m_device);
    if (tex->sampler) vkDestroySampler(m_device, tex->sampler, nullptr);
    if (tex->view) vkDestroyImageView(m_device, tex->view, nullptr);
    if (tex->image) vkDestroyImage(m_device, tex->image, nullptr);
    if (tex->memory) vkFreeMemory(m_device, tex->memory, nullptr);

    std::vector<uint16_t> halfPixels(image.pixels.size());
    for (size_t i = 0; i < image.pixels.size(); i++)
        halfPixels[i] = floatToHalf(image.pixels[i]);
    uploadTextureData(*tex, halfPixels.data(), image.width, image.height,
                      VK_FORMAT_R16G16B16A16_SFLOAT,
                      (VkDeviceSize)halfPixels.size() * sizeof(uint16_t));
    allocateHdrDescriptor(*tex);  // re-writes descriptor with the new view
}

// ---- HDR output ----

void VulkanRenderer::setHdrDrawRect(float x0, float y0, float x1, float y1) {
    m_hdrDrawRect[0] = x0;
    m_hdrDrawRect[1] = y0;
    m_hdrDrawRect[2] = x1;
    m_hdrDrawRect[3] = y1;
    m_hdrDrawRectValid = true;
}

bool VulkanRenderer::createHdrPipeline() {
    ensureHdrDescSetLayout();

    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(HdrPushConstants);

    VkPipelineLayoutCreateInfo li = {};
    li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    li.setLayoutCount = 1;
    li.pSetLayouts = &m_hdrDescSetLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &pcRange;
    VK_CHECK(vkCreatePipelineLayout(m_device, &li, nullptr, &m_hdrPipelineLayout));

    VkShaderModuleCreateInfo smi = {};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = sizeof(g_hdrQuadVertSpv);
    smi.pCode = g_hdrQuadVertSpv;
    VkShaderModule vertModule;
    VK_CHECK(vkCreateShaderModule(m_device, &smi, nullptr, &vertModule));
    smi.codeSize = sizeof(g_hdrQuadFragSpv);
    smi.pCode = g_hdrQuadFragSpv;
    VkShaderModule fragModule;
    VK_CHECK(vkCreateShaderModule(m_device, &smi, nullptr, &fragModule));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt = {};
    blendAtt.blendEnable = VK_FALSE;  // opaque: preserve HDR values > 1.0
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blendAtt;

    VkGraphicsPipelineCreateInfo pi = {};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pDynamicState = &dyn;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &cb;
    pi.layout = m_hdrPipelineLayout;
    pi.renderPass = m_renderPass;
    pi.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pi, nullptr, &m_hdrPipeline);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
    VK_CHECK(res);
    return true;
}

void VulkanRenderer::destroyHdrPipeline() {
    if (m_hdrPipeline) { vkDestroyPipeline(m_device, m_hdrPipeline, nullptr); m_hdrPipeline = VK_NULL_HANDLE; }
    if (m_hdrPipelineLayout) { vkDestroyPipelineLayout(m_device, m_hdrPipelineLayout, nullptr); m_hdrPipelineLayout = VK_NULL_HANDLE; }
    if (m_hdrDescSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_hdrDescSetLayout, nullptr); m_hdrDescSetLayout = VK_NULL_HANDLE; }
}

bool VulkanRenderer::setHdrOutput(bool enable) {
    if (enable) {
        querySurfaceFormats();
        HdrMode want = m_hdrScRGBSupported ? HdrMode::ScRGB
                       : m_hdr10Supported ? HdrMode::HDR10
                       : HdrMode::Off;
        if (want == HdrMode::Off) {
            LOG_ERROR("HDR output not available on this display");
            return false;
        }
        m_hdrMode = want;
    } else {
        m_hdrMode = HdrMode::Off;
    }

    // Contract: the app has already destroyed its textures (SDR + HDR)
    vkDeviceWaitIdle(m_device);
    if (m_hdrTextureSet) { destroyTexture((void*)m_hdrTextureSet); m_hdrTextureSet = VK_NULL_HANDLE; }

    // Rebuild swapchain + render pass + ImGui backend for the new format
    ImGui_ImplVulkan_Shutdown();
    cleanupSwapchain();
    if (m_renderPass) { vkDestroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }

    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    if (!createSwapchain(w, h)) return false;
    if (!createRenderPass()) return false;
    if (!createFrameResources()) return false;
    if (!createCommandBuffers()) return false;
    if (!createSyncObjects()) return false;

    if (m_hdrMode != HdrMode::Off) {
        if (!createHdrPipeline()) return false;
    } else {
        destroyHdrPipeline();
    }

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
    initInfo.RenderPass = m_renderPass;
    initInfo.Subpass = 0;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = checkVkResult;
    ImGui_ImplVulkan_Init(&initInfo);

    m_currentFrame = 0;
    m_swapChainRebuild = false;

    const char* modeName = m_hdrMode == HdrMode::ScRGB ? "scRGB"
                         : m_hdrMode == HdrMode::HDR10 ? "HDR10" : "off";
    LOG_INFO("HDR output: %s (format=%d)", modeName, (int)m_swapchainFormat);
    return m_hdrMode != HdrMode::Off || !enable;
}

std::unique_ptr<Renderer> Renderer::createVulkan() {
    return std::make_unique<VulkanRenderer>();
}

#else

std::unique_ptr<Renderer> Renderer::createVulkan() { return nullptr; }

#endif
