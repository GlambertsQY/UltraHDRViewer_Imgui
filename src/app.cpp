#include "app.h"
#include "ui_panels.h"
#include "image_loader.h"
#include "file_dialog.h"
#include "renderer.h"
#include "log.h"

#include "imgui.h"
#include "ultrahdr_api.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <fstream>

Application* Application::s_instance = nullptr;

Application::~Application() { shutdown(); }

bool Application::init(const AppConfig& config) {
    s_instance = this;
    glfwSetErrorCallback([](int e, const char* d) { LOG_ERROR("GLFW %d: %s", e, d); });
    if (!glfwInit()) return false;

    if (config.backend == "vulkan") {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_renderer = Renderer::createVulkan();
    } else {
        m_renderer = Renderer::createOpenGL();
    }
    if (!m_renderer) { glfwTerminate(); return false; }

    // Set GLFW hints BEFORE window creation (must be here, not in renderer)
    if (config.backend == "opengl") {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#if defined(__APPLE__)
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    }

    m_window = glfwCreateWindow(config.windowWidth, config.windowHeight,
                                 "Ultra HDR Viewer", nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetDropCallback(m_window, dropCallback);
    glfwSetKeyCallback(m_window, keyCallback);

    if (!m_renderer->init(m_window)) {
        glfwDestroyWindow(m_window); glfwTerminate(); return false;
    }
    glfwGetFramebufferSize(m_window, &m_displayWidth, &m_displayHeight);
    return true;
}

void Application::run() {
    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) { glfwWaitEvents(); continue; }

        // Re-apply tone mapping if parameters changed
        if (m_toneMapDirty && m_hdrData) {
            reapplyToneMapping();
            m_toneMapDirty = false;
        }

        m_renderer->newFrame();
        renderUI();
        m_renderer->render();
    }
}

void Application::shutdown() {
    if (m_texture && m_renderer) { m_renderer->destroyTexture(m_texture); m_texture = nullptr; }
    if (m_renderer) { m_renderer->shutdown(); m_renderer.reset(); }
    if (m_window) { glfwDestroyWindow(m_window); m_window = nullptr; }
    glfwTerminate();
    s_instance = nullptr;
}

// ============================================================
// Tone mapping re-apply
// ============================================================
void Application::reapplyToneMapping() {
    if (!m_hdrData || !m_currentImage) return;

    applyToneMap(*m_hdrData, *m_currentImage, m_exposure, m_gamma, m_toneMappingMode);

    if (m_texture && m_renderer) {
        m_renderer->updateTexture(m_texture, *m_currentImage);
    }
}

// ============================================================
// UI
// ============================================================
void Application::renderUI() {
    bool hasHDR = (m_hdrData != nullptr);

    UIPanels::renderMenuBar(m_window, m_currentImage.get(), m_zoom, hasHDR,
                            m_fitToWindow, m_showInfo, m_showAbout,
                            [this]() { openImageFile(); },
                            [this]() { saveImageFile(); },
                            [this]() { fitToWindow(); },
                            [this]() { resetView(); });
    UIPanels::renderControlPanel(hasHDR,
                                 m_exposure, m_gamma,
                                 m_toneMappingMode, m_toneMapDirty,
                                 m_fitToWindow, m_zoom,
                                 m_offsetX, m_offsetY,
                                 [this]() { fitToWindow(); },
                                 [this]() { resetView(); });
    renderImagePanel();
    UIPanels::renderInfoPanel(m_showInfo, m_exifData,
                              m_currentImage.get(), hasHDR,
                              m_zoom, m_exposure, m_gamma,
                              m_displayWidth);
    UIPanels::renderAboutDialog(m_showAbout);
}

void Application::renderImagePanel() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float panelW = Config::PANEL_IMAGE_MARGIN;

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + panelW, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - panelW, vp->WorkSize.y));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoInputs;  // Don't consume input, we handle it manually

    if (!ImGui::Begin("##ImageView", nullptr, flags)) { ImGui::End(); return; }

    if (m_texture && m_currentImage) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cpos = ImGui::GetCursorScreenPos();
        ImVec2 csz = ImGui::GetContentRegionAvail();

        float iw, ih;
        if (m_fitToWindow) {
            float sx = csz.x / m_currentImage->width;
            float sy = csz.y / m_currentImage->height;
            m_zoom = std::min(sx, sy);
            iw = m_currentImage->width * m_zoom;
            ih = m_currentImage->height * m_zoom;
            m_offsetX = (csz.x - iw) * 0.5f;
            m_offsetY = (csz.y - ih) * 0.5f;
        } else {
            iw = m_currentImage->width * m_zoom;
            ih = m_currentImage->height * m_zoom;
        }

        ImVec2 ip(cpos.x + m_offsetX, cpos.y + m_offsetY);
        ImVec2 ie(ip.x + iw, ip.y + ih);

        // ---- Input handling (zoom + pan) ----
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse = io.MousePos;
        bool inArea = (mouse.x >= cpos.x && mouse.x < cpos.x + csz.x &&
                       mouse.y >= cpos.y && mouse.y < cpos.y + csz.y);

        if (inArea && !ImGui::IsAnyItemActive()) {
            // Scroll zoom towards cursor
            if (io.MouseWheel != 0.0f) {
                m_fitToWindow = false;
                float oldZoom = m_zoom;
                float factor = 1.0f + io.MouseWheel * 0.1f;
                m_zoom = std::max(Config::ZOOM_MIN, std::min(m_zoom * factor, 100.0f));
                float scale = m_zoom / oldZoom;
                m_offsetX = mouse.x - (mouse.x - m_offsetX) * scale;
                m_offsetY = mouse.y - (mouse.y - m_offsetY) * scale;
            }

            // Left-click drag pan
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_fitToWindow = false;
                m_isDragging = true;
                m_dragStartX = mouse.x;
                m_dragStartY = mouse.y;
            }
        }
        if (m_isDragging) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_offsetX += mouse.x - (float)m_dragStartX;
                m_offsetY += mouse.y - (float)m_dragStartY;
                m_dragStartX = mouse.x;
                m_dragStartY = mouse.y;
            } else {
                m_isDragging = false;
            }
        }

        // Checkerboard
        float gs = std::max(4.0f, 16.0f * m_zoom);
        for (float y = ip.y; y < ie.y; y += gs)
            for (float x = ip.x; x < ie.x; x += gs) {
                int ix = (int)((x - ip.x) / gs), iy = (int)((y - ip.y) / gs);
                ImU32 c = ((ix + iy) % 2 == 0) ? IM_COL32(80,80,80,255) : IM_COL32(120,120,120,255);
                dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x+gs,ie.x), std::min(y+gs,ie.y)), c);
            }

        dl->AddImage((ImTextureID)(intptr_t)m_texture, ip, ie);

        // Status bar at bottom
        char buf[128];
        snprintf(buf, sizeof(buf), "  %dx%d  %.0f%%", m_currentImage->width, m_currentImage->height, m_zoom * 100.0f);
        dl->AddText(ImVec2(cpos.x + 4, cpos.y + csz.y - 20), IM_COL32(200,200,200,200), buf);
    } else {
        ImVec2 a = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(a.x * 0.5f - 80, a.y * 0.5f - 20));
        ImGui::TextDisabled("Drop an image here");
        ImGui::SetCursorPosX(a.x * 0.5f - 60);
        ImGui::TextDisabled("or File > Open (Ctrl+O)");
    }
    ImGui::End();
}

// ============================================================
// File operations
// ============================================================
void Application::openImageFile() {
    std::string path = openImageDialog(m_window);
    if (!path.empty()) loadImage(path);
}

void Application::saveImageFile() {
    if (!m_currentImage) return;

    std::string path = openSaveDialog(m_window);
    if (path.empty()) return;  // user cancelled

    int w = m_currentImage->width;
    int h = m_currentImage->height;
    int rowSize = ((w * 3 + 3) / 4) * 4;  // BMP rows are 4-byte aligned
    int imageSize = rowSize * h;
    int fileSize = 54 + imageSize;

    // BMP File Header (14 bytes)
    uint8_t bmpFileHeader[14] = {
        'B', 'M',           // signature
        (uint8_t)fileSize, (uint8_t)(fileSize>>8), (uint8_t)(fileSize>>16), (uint8_t)(fileSize>>24),
        0,0,0,0,            // reserved
        54,0,0,0            // data offset
    };

    // BMP DIB Header (40 bytes)
    uint8_t bmpDIB[40] = {};
    bmpDIB[0]=40; bmpDIB[4]=w&0xFF; bmpDIB[5]=(w>>8)&0xFF; bmpDIB[6]=(w>>16)&0xFF; bmpDIB[7]=(w>>24)&0xFF;
    bmpDIB[8]=h&0xFF; bmpDIB[9]=(h>>8)&0xFF; bmpDIB[10]=(h>>16)&0xFF; bmpDIB[11]=(h>>24)&0xFF;
    bmpDIB[12]=1; /*planes*/ bmpDIB[14]=24; /*bpp*/
    bmpDIB[20]=imageSize&0xFF; bmpDIB[21]=(imageSize>>8)&0xFF; bmpDIB[22]=(imageSize>>16)&0xFF; bmpDIB[23]=(imageSize>>24)&0xFF;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) { LOG_ERROR("Cannot save: %s", path.c_str()); return; }
    f.write((const char*)bmpFileHeader, 14);
    f.write((const char*)bmpDIB, 40);

    // Write pixel rows bottom-up (BMP convention), convert RGBA->BGR
    std::vector<uint8_t> row(rowSize, 0);
    uint8_t* src = m_currentImage->pixels.data();
    for (int y = h-1; y >= 0; y--) {
        uint8_t* srcRow = src + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            row[x*3+0] = srcRow[x*4+2];  // B ← R
            row[x*3+1] = srcRow[x*4+1];  // G ← G
            row[x*3+2] = srcRow[x*4+0];  // R ← B
        }
        f.write((const char*)row.data(), rowSize);
    }
    f.close();
    LOG_INFO("Saved BMP: %s (%dx%d)", path.c_str(), w, h);
}

void Application::loadImage(const std::string& path) {
    // Skip if same file already loaded
    if (!path.empty() && m_currentImage && path == m_currentImage->filePath) {
        LOG_INFO("Same file, skipping reload: %s", path.c_str());
        return;
    }

    if (m_texture && m_renderer) {
        m_renderer->destroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_hdrData.reset();
    m_currentImage.reset();
    m_exifData = ExifData{};

    LOG_INFO("Loading: %s", path.c_str());

    auto image = std::make_unique<ImageData>();
    std::unique_ptr<HDRData> hdr;
    ExifData exif;

    bool ok = false;
    try {
        ok = loadImageFile(path, *image, hdr, &exif);
        if (!ok) {
            ok = loadRegularImage(path, *image, &exif);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception loading image: %s", e.what());
    } catch (...) {
        LOG_ERROR("Unknown exception loading image");
    }

    if (ok && !image->pixels.empty() && image->width > 0 && image->height > 0) {
        m_currentImage = std::move(image);
        m_hdrData = std::move(hdr);
        m_exifData = exif;
        m_texture = m_renderer->createTexture(*m_currentImage);
        if (!m_texture) {
            LOG_ERROR("Failed to create texture");
            m_currentImage.reset();
            m_hdrData.reset();
            m_exifData = ExifData{};
        } else {
            resetView();
            m_fitToWindow = true;
            LOG_INFO("Display: %dx%d", m_currentImage->width, m_currentImage->height);
        }
    } else {
        LOG_ERROR("Failed to load: %s", path.c_str());
    }
}

void Application::resetView() { m_zoom = 1.0f; m_offsetX = 0; m_offsetY = 0; m_fitToWindow = true; }
void Application::fitToWindow() { m_fitToWindow = true; }

// ============================================================
// Callbacks
// ============================================================
Application* Application::getInstance(GLFWwindow* w) {
    return static_cast<Application*>(glfwGetWindowUserPointer(w));
}

void Application::framebufferSizeCallback(GLFWwindow* w, int width, int height) {
    auto* a = getInstance(w); if (a) { a->m_displayWidth = width; a->m_displayHeight = height; }
}

void Application::dropCallback(GLFWwindow* w, int count, const char** paths) {
    auto* a = getInstance(w); if (a && count > 0) a->loadImage(paths[0]);
}

void Application::keyCallback(GLFWwindow* w, int key, int, int action, int mods) {
    auto* a = getInstance(w); if (!a || action != GLFW_PRESS) return;
    if (mods & GLFW_MOD_CONTROL) {
        if (key == GLFW_KEY_O) a->openImageFile();
        else if (key == GLFW_KEY_S) a->saveImageFile();
    } else {
        switch (key) {
            case GLFW_KEY_F: a->m_fitToWindow = !a->m_fitToWindow; break;
            case GLFW_KEY_R: a->resetView(); break;
            case GLFW_KEY_I: a->m_showInfo = !a->m_showInfo; break;
            case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(w, GLFW_TRUE); break;
        }
    }
}
