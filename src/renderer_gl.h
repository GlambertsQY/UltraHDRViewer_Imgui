#pragma once

#include "renderer.h"
#include <GLFW/glfw3.h>

class OpenGLRenderer : public Renderer {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool init(GLFWwindow* window) override;
    void shutdown() override;
    void newFrame() override;
    void render() override;

    void* createTexture(const ImageData& image) override;
    void destroyTexture(void* texture) override;
    void updateTexture(void* texture, const ImageData& image) override;

    // HDR output: not possible on Windows WGL (no HDR swapchain) - stubs
    bool hdrOutputSupported() const override { return false; }
    bool setHdrOutput(bool) override { return false; }
    HdrMode activeHdrMode() const override { return HdrMode::Off; }
    void* createTextureHDR(const HDRData&) override { return nullptr; }
    void updateTextureHDR(void*, const HDRData&) override {}
    void setHdrDrawRect(float, float, float, float) override {}

private:
    GLFWwindow* m_window = nullptr;
    const char* m_glslVersion = "#version 130";
};
