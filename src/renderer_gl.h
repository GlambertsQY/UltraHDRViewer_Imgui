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

private:
    GLFWwindow* m_window = nullptr;
    const char* m_glslVersion = "#version 130";
};
