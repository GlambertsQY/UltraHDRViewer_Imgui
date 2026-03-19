#include "app.h"
#include "image_loader.h"
#include "file_dialog.h"
#include "renderer.h"

#include "imgui.h"
#include "ultrahdr_api.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

Application* Application::s_instance = nullptr;

Application::~Application() { shutdown(); }

bool Application::init(const AppConfig& config) {
    s_instance = this;
    glfwSetErrorCallback([](int e, const char* d) { fprintf(stderr, "GLFW %d: %s\n", e, d); });
    if (!glfwInit()) return false;

    if (config.backend == "vulkan") {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_renderer = Renderer::createVulkan();
    } else {
        m_renderer = Renderer::createOpenGL();
    }
    if (!m_renderer) { glfwTerminate(); return false; }

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
    renderMenuBar();
    renderControlPanel();
    renderImagePanel();
    renderInfoPanel();
    renderAboutDialog();
}

void Application::renderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) openImageFile();
            if (ImGui::MenuItem("Save SDR As...", "Ctrl+S", false, m_currentImage != nullptr))
                saveImageFile();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit to Window", "F", &m_fitToWindow)) {
                if (m_fitToWindow) fitToWindow();
            }
            if (ImGui::MenuItem("Reset View", "R")) resetView();
            ImGui::Separator();
            ImGui::MenuItem("Show Info", "I", &m_showInfo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) m_showAbout = true;
            ImGui::EndMenu();
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 320);
        if (m_currentImage) {
            const char* type = m_hdrData ? "HDR" : "SDR";
            ImGui::Text("%dx%d %s | %.0f%% | %s",
                        m_currentImage->width, m_currentImage->height,
                        type, m_zoom * 100.0f,
                        m_currentImage->filePath.c_str());
        } else {
            ImGui::TextDisabled("No image loaded");
        }
        ImGui::EndMainMenuBar();
    }
}

void Application::renderControlPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }

    bool hasHDR = (m_hdrData != nullptr);
    if (!hasHDR) ImGui::BeginDisabled();

    ImGui::Text("Tone Mapping (HDR)");
    ImGui::Separator();

    const char* toneNames[] = {"Reinhard", "ACES Filmic", "Uncharted 2"};
    if (ImGui::Combo("Mode", &m_toneMappingMode, toneNames, IM_ARRAYSIZE(toneNames)))
        m_toneMapDirty = true;

    if (ImGui::SliderFloat("Exposure", &m_exposure, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
        m_toneMapDirty = true;

    if (ImGui::SliderFloat("Gamma", &m_gamma, 1.0f, 4.0f, "%.2f"))
        m_toneMapDirty = true;

    if (ImGui::Button("Reset##tonemap")) {
        m_exposure = 1.0f; m_gamma = 2.2f; m_toneMappingMode = 0;
        m_toneMapDirty = true;
    }

    if (!hasHDR) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Text("View");
    ImGui::Separator();

    if (ImGui::Checkbox("Fit to Window", &m_fitToWindow)) {
        if (m_fitToWindow) fitToWindow();
    }
    ImGui::SliderFloat("Zoom", &m_zoom, 0.05f, 20.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Pan X", &m_offsetX, 1.0f);
    ImGui::DragFloat("Pan Y", &m_offsetY, 1.0f);
    if (ImGui::Button("Reset View")) resetView();

    ImGui::End();
}

void Application::renderImagePanel() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float panelW = 290.0f;

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
                m_zoom = std::max(0.01f, std::min(m_zoom * factor, 100.0f));
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

void Application::renderInfoPanel() {
    if (!m_showInfo || !m_currentImage) return;
    ImGui::SetNextWindowPos(ImVec2(m_displayWidth - 320, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Image Info", &m_showInfo)) {
        ImGui::Text("File: %s", m_currentImage->filePath.c_str());
        ImGui::Text("Size: %dx%d", m_currentImage->width, m_currentImage->height);
        ImGui::Text("Type: %s", m_hdrData ? "Ultra HDR" : "SDR");
        ImGui::Text("Zoom: %.1f%%", m_zoom * 100.0f);
        ImGui::Text("Memory: %.2f MB", m_currentImage->dataSize() / (1024.0f * 1024.0f));
        if (m_hdrData) {
            ImGui::Text("Exposure: %.2f", m_exposure);
            ImGui::Text("Gamma: %.2f", m_gamma);
        }

        if (m_exifData.valid) {
            ImGui::Separator();
            ImGui::Text("Camera");
            if (!m_exifData.make.empty())
                ImGui::Text("Make: %s", m_exifData.make.c_str());
            if (!m_exifData.model.empty())
                ImGui::Text("Model: %s", m_exifData.model.c_str());
            if (!m_exifData.software.empty())
                ImGui::Text("Software: %s", m_exifData.software.c_str());
            if (!m_exifData.dateTime.empty())
                ImGui::Text("DateTime: %s", m_exifData.dateTime.c_str());

            ImGui::Separator();
            ImGui::Text("Exposure");
            ImGui::Text("Shutter: %s", m_exifData.exposureStr().c_str());
            ImGui::Text("Aperture: %s", m_exifData.apertureStr().c_str());
            if (m_exifData.isoSpeed > 0)
                ImGui::Text("ISO: %u", m_exifData.isoSpeed);
            ImGui::Text("Focal Length: %s", m_exifData.focalLengthStr().c_str());
            if (m_exifData.flash != 0xFFFF)
                ImGui::Text("Flash: %s", (m_exifData.flash & 1) ? "On" : "Off");

            ImGui::Separator();
            ImGui::Text("Image");
            ImGui::Text("Orientation: %s", m_exifData.orientationStr().c_str());
            if (m_exifData.pixelX > 0 && m_exifData.pixelY > 0)
                ImGui::Text("EXIF Size: %ux%u", m_exifData.pixelX, m_exifData.pixelY);

            if (!m_exifData.lensMake.empty())
                ImGui::Text("Lens: %s", m_exifData.lensMake.c_str());
            if (!m_exifData.lensModel.empty())
                ImGui::Text("Lens Model: %s", m_exifData.lensModel.c_str());

            if (m_exifData.hasGPS) {
                ImGui::Separator();
                ImGui::Text("GPS");
                ImGui::Text("Lat: %.6f", m_exifData.gpsLatitude);
                ImGui::Text("Lon: %.6f", m_exifData.gpsLongitude);
                ImGui::Text("Alt: %.1f m", m_exifData.gpsAltitude);
            }

            if (m_exifData.isUltraHDR) {
                ImGui::Separator();
                ImGui::Text("Ultra HDR");
                ImGui::Text("Color Transfer: %s", m_exifData.colorTransfer.c_str());
                ImGui::Text("HDR Headroom: %d EV", m_exifData.hdrHeadroom);
            }
        }
    }
    ImGui::End();
}

void Application::renderAboutDialog() {
    if (!m_showAbout) return;
    ImGui::OpenPopup("About##AboutDialog");
    if (ImGui::BeginPopupModal("About##AboutDialog", &m_showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ultra HDR Viewer");
        ImGui::Text("Dear ImGui + GLFW + libultrahdr");
        ImGui::Separator();
        ImGui::Text("libultrahdr %s", UHDR_LIB_VERSION_STR);
        if (ImGui::Button("Close")) m_showAbout = false;
        ImGui::EndPopup();
    }
}

// ============================================================
// File operations
// ============================================================
void Application::openImageFile() {
#ifdef _WIN32
    static const char* filter =
        "Ultra HDR & Image Files\0*.jpg;*.jpeg;*.jpe;*.uhdr;*.png;*.bmp\0"
        "All Files\0*.*\0";
    std::string path = openFileDialog(m_window, nullptr, filter);
#else
    std::string path = openFileDialog(m_window, nullptr, nullptr);
#endif
    if (!path.empty()) loadImage(path);
}

void Application::saveImageFile() {
    if (!m_currentImage) return;
    // Simple save dialog - reuse openFileDialog logic or just save
    saveImageToFile(*m_currentImage, m_currentImage->filePath + ".sdr.raw");
}

void Application::loadImage(const std::string& path) {
    if (m_texture && m_renderer) {
        m_renderer->destroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_hdrData.reset();
    m_currentImage.reset();
    m_exifData = ExifData{};

    fprintf(stdout, "Loading: %s\n", path.c_str());

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
        fprintf(stderr, "Exception loading image: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Unknown exception loading image\n");
    }

    if (ok && image->pixels && image->width > 0 && image->height > 0) {
        m_currentImage = std::move(image);
        m_hdrData = std::move(hdr);
        m_exifData = exif;
        m_texture = m_renderer->createTexture(*m_currentImage);
        if (!m_texture) {
            fprintf(stderr, "Failed to create texture\n");
            m_currentImage.reset();
            m_hdrData.reset();
            m_exifData = ExifData{};
        } else {
            resetView();
            m_fitToWindow = true;
            fprintf(stdout, "Display: %dx%d\n", m_currentImage->width, m_currentImage->height);
        }
    } else {
        fprintf(stderr, "Failed to load: %s\n", path.c_str());
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
