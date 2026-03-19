#include "renderer.h"
#include "renderer_gl.h"

#ifdef UHDR_VIEWER_OPENGL_ENABLED

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif

#include <GL/gl.h>
#include <cstdio>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// ============================================================
// OpenGL Texture creation (always RGBA8 SDR)
// ============================================================
static unsigned int uploadTexture(const ImageData& image) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);

    glBindTexture(GL_TEXTURE_2D, 0);
    return texID;
}

OpenGLRenderer::OpenGLRenderer() {}
OpenGLRenderer::~OpenGLRenderer() { shutdown(); }

bool OpenGLRenderer::init(GLFWwindow* window) {
    m_window = window;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

#if defined(__APPLE__)
    m_glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    m_glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(m_glslVersion);
    return true;
}

void OpenGLRenderer::shutdown() {
    if (!m_window) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_window = nullptr;
}

void OpenGLRenderer::newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void OpenGLRenderer::render() {
    ImGui::Render();
    int displayW, displayH;
    glfwGetFramebufferSize(m_window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

void* OpenGLRenderer::createTexture(const ImageData& image) {
    if (!image.pixels || image.width <= 0 || image.height <= 0) return nullptr;
    return (void*)(intptr_t)uploadTexture(image);
}

void OpenGLRenderer::destroyTexture(void* texture) {
    if (!texture) return;
    GLuint texID = (GLuint)(intptr_t)texture;
    glDeleteTextures(1, &texID);
}

void OpenGLRenderer::updateTexture(void* texture, const ImageData& image) {
    if (!texture || !image.pixels) return;
    GLuint texID = (GLuint)(intptr_t)texture;
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height,
                    GL_RGBA, GL_UNSIGNED_BYTE, image.pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::unique_ptr<Renderer> Renderer::createOpenGL() {
    return std::make_unique<OpenGLRenderer>();
}

#else

std::unique_ptr<Renderer> Renderer::createOpenGL() { return nullptr; }

#endif
