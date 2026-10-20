#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

struct ImageData {
    std::vector<uint8_t> pixels;   // Always RGBA8888 for display
    int width = 0;
    int height = 0;
    int channels = 4;
    std::string filePath;

    size_t dataSize() const {
        return pixels.size();
    }
};

// Decoded HDR pixel data (linear float RGBA, 1.0 = SDR white = 203 nits)
struct HDRData {
    std::vector<float> pixels;  // RGBA float, width*height*4
    int width = 0;
    int height = 0;
};

// HDR output mode for HDR-capable displays
enum class HdrMode {
    Off = 0,   // SDR swapchain (tone-mapped display)
    ScRGB,     // R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR (linear, 1.0 = 80 nits)
    HDR10      // A2B10G10R10 + ST2084 (PQ encoded, BT.2020)
};

class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init(GLFWwindow* window) = 0;
    virtual void shutdown() = 0;

    virtual void newFrame() = 0;
    virtual void render() = 0;

    virtual void* createTexture(const ImageData& image) = 0;
    virtual void destroyTexture(void* texture) = 0;
    virtual void updateTexture(void* texture, const ImageData& image) = 0;

    // ---- HDR output (HDR displays only; GL backend returns stubs) ----
    virtual bool hdrOutputSupported() const = 0;              // any HDR swapchain format available
    virtual bool setHdrOutput(bool enable) = 0;               // returns true if HDR mode is active after the call
    virtual HdrMode activeHdrMode() const = 0;                // HdrMode::Off when SDR
    virtual void* createTextureHDR(const HDRData& image) = 0; // RGBA16F texture from linear float data
    virtual void updateTextureHDR(void* texture, const HDRData& image) = 0;
    // Image quad destination in framebuffer pixels (called every frame before render())
    virtual void setHdrDrawRect(float x0, float y0, float x1, float y1) = 0;

    static std::unique_ptr<Renderer> createOpenGL();
    static std::unique_ptr<Renderer> createVulkan();
};
