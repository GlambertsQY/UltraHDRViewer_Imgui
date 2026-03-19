#pragma once

#include "renderer.h"
#include <GLFW/glfw3.h>

#ifdef UHDR_VIEWER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <vector>

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
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

private:
    GLFWwindow* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
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

    void uploadTextureData(VulkanTexture& tex, const ImageData& image);
    void transitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmdBuffer);
    static void checkVkResult(VkResult err);
};

#endif
