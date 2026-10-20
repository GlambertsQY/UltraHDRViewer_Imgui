#pragma once

#include "renderer.h"

struct GLFWwindow;

#ifdef UHDR_VIEWER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    bool descriptorFromImGui = false;  // free via ImGui_ImplVulkan_RemoveTexture vs vkFreeDescriptorSets
};

class VulkanRenderer : public Renderer {
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    bool init(GLFWwindow* window) override;
    void shutdown() override;
    void newFrame() override;
    void render() override;

    void* createTexture(const ImageData& image) override;
    void destroyTexture(void* texture) override;
    void updateTexture(void* texture, const ImageData& image) override;

    // ---- HDR output ----
    bool hdrOutputSupported() const override { return m_hdrScRGBSupported || m_hdr10Supported; }
    bool setHdrOutput(bool enable) override;
    HdrMode activeHdrMode() const override { return m_hdrMode; }
    void* createTextureHDR(const HDRData& image) override;
    void updateTextureHDR(void* texture, const HDRData& image) override;
    void setHdrDrawRect(float x0, float y0, float x1, float y1) override;

private:
    GLFWwindow* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_physicalDeviceProperties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_queueFamily = (uint32_t)-1;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR m_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;

    struct SwapchainFrame {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<SwapchainFrame> m_frames;
    uint32_t m_imageCount = 0;
    uint32_t m_currentFrame = 0;
    uint32_t m_minImageCount = 2;
    VkExtent2D m_extent = {0, 0};
    VkClearValue m_clearValue = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
    bool m_swapChainRebuild = false;
    VkCommandPool m_tempCommandPool = VK_NULL_HANDLE;

    // ---- HDR output state ----
    bool m_hdrScRGBSupported = false;
    bool m_hdr10Supported = false;
    HdrMode m_hdrMode = HdrMode::Off;
    VkDescriptorSet m_hdrTextureSet = VK_NULL_HANDLE;  // descriptor set bound by the HDR quad
    float m_hdrDrawRect[4] = {0, 0, 0, 0};      // x0, y0, x1, y1 in framebuffer pixels
    bool m_hdrDrawRectValid = false;

    // Textures are returned to the app as their VkDescriptorSet (usable directly as
    // ImTextureID). This map resolves them back to the owning VulkanTexture.
    std::unordered_map<VkDescriptorSet, VulkanTexture*> m_textures;

    VkDescriptorSetLayout m_hdrDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_hdrPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_hdrPipeline = VK_NULL_HANDLE;

    struct HdrPushConstants {
        float rectMin[2];
        float rectMax[2];
        float fbSize[2];
        int mode = 0;               // 0 = scRGB, 1 = HDR10
        float whiteNits = 203.0f;   // HDRData 1.0 == 203 nits
        float refWhite = 80.0f;     // scRGB 1.0 == 80 nits
        float pad = 0.0f;
    };

    bool createInstance();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createDescriptorPool();
    bool createSurface(GLFWwindow* window);
    bool createSwapchain(int width, int height);
    bool createRenderPass();
    bool createFrameResources();
    bool createCommandBuffers();
    bool createSyncObjects();
    void cleanupSwapchain();
    void recreateSwapchain(int width, int height);

    // HDR helpers
    void querySurfaceFormats();
    bool createHdrPipeline();
    void destroyHdrPipeline();
    void ensureHdrDescSetLayout();
    void allocateHdrDescriptor(VulkanTexture& tex);

    void uploadTextureData(VulkanTexture& tex, const void* pixels, int width, int height,
                           VkFormat format, VkDeviceSize byteSize);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmdBuffer);
    static void checkVkResult(VkResult err);
};

#endif
