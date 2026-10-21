#include <android/input.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <jni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "android_config.h"
#include "egl_context.h"
#include "hdr_gles.h"
#include "hdr_types.h"

#define TAG "UHDRViewer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

struct LoadResult {
    std::unique_ptr<ImageData> sdr;
    std::unique_ptr<HDRData> hdr;
    ExifData exif;
    bool ok = false;
};

struct TouchEvent {
    int action = 0;
    int pointerCount = 0;
    float x[2] = {0, 0};
    float y[2] = {0, 0};
    int64_t timeMs = 0;
};

struct AndroidApp {
    ANativeWindow* window = nullptr;

    EglContext egl;
    HdrGles hdrRenderer;

    int width = 0;
    int height = 0;
    float density = 1.0f;
    bool deviceHdr10 = false;
    float displayMaxNits = 1000.0f;

    std::thread renderThread;
    std::atomic<bool> loopRunning{false};
    std::atomic<bool> paused{false};

    std::atomic<bool> openRequest{false};
    std::atomic<int> pendingHdrToggle{-1};
    std::atomic<bool> hdrOutputWanted{false};

    // Window handoff (UI thread -> render thread).
    std::mutex windowMutex;
    ANativeWindow* pendingWindow = nullptr;
    std::atomic<bool> windowDirty{false};

    // Image handoff (decode thread -> render thread).
    std::mutex loadMutex;
    LoadResult pending;
    bool hasPending = false;
    std::thread loadThread;

    // Render-thread-owned image state.
    std::unique_ptr<ImageData> currentImage;
    std::unique_ptr<HDRData> hdrData;
    ExifData exifData;
    GLuint sdrTexture = 0;
    bool sdrDirty = false;

    // View state.
    float exposure = AndroidConfig::DEFAULT_EXPOSURE;
    float gamma = AndroidConfig::DEFAULT_GAMMA;
    int toneMappingMode = 0;
    float zoom = AndroidConfig::DEFAULT_ZOOM;
    float offsetX = 0.0f, offsetY = 0.0f;
    bool fitToWindow = true;

    // Image viewport in screen pixel coordinates (updated every frame).
    float imageX = 0.0f, imageY = 0.0f, imageW = 1.0f, imageH = 1.0f;
    bool hdrDrawValid = false;
    float hdrRect[4] = {0, 0, 0, 0};

    // Touch state.
    std::mutex touchMutex;
    std::vector<TouchEvent> touchQueue;
    bool mouseDown = false;
    bool pinching = false;
    bool gestureMoved = false;
    float lastX = 0.0f, lastY = 0.0f;
    float downX = 0.0f, downY = 0.0f;
    int64_t downTimeMs = 0;
    int64_t lastTapTimeMs = 0;
    float pinchStartDist = 0.0f;
    float pinchStartZoom = 1.0f;
};

AndroidApp* g_app = nullptr;

int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void reapplyToneMap(AndroidApp* app) {
    if (app->hdrData && app->currentImage) {
        applyToneMap(*app->hdrData, *app->currentImage,
                     app->exposure, app->gamma, app->toneMappingMode);
    }
    app->sdrDirty = true;
}

void uploadSdrTexture(AndroidApp* app) {
    if (!app->currentImage || app->currentImage->pixels.empty()) return;
    const ImageData& img = *app->currentImage;
    if (app->sdrTexture == 0) {
        glGenTextures(1, &app->sdrTexture);
    }
    glBindTexture(GL_TEXTURE_2D, app->sdrTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.pixels.data());
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("SDR texture upload failed: 0x%x (%dx%d)", err, img.width, img.height);
    }
    app->sdrDirty = false;
}

void processPendingLoad(AndroidApp* app) {
    LoadResult result;
    {
        std::lock_guard<std::mutex> lock(app->loadMutex);
        if (!app->hasPending) return;
        result = std::move(app->pending);
        app->hasPending = false;
    }
    if (!result.ok || !result.sdr) {
        LOGE("Image load failed");
        return;
    }

    app->currentImage = std::move(result.sdr);
    app->hdrData = std::move(result.hdr);
    app->exifData = result.exif;
    app->fitToWindow = true;
    app->zoom = AndroidConfig::DEFAULT_ZOOM;
    app->offsetX = app->offsetY = 0.0f;
    if (app->hdrData) reapplyToneMap(app);
    app->sdrDirty = true;
    uploadSdrTexture(app);

    app->hdrRenderer.destroyTexture();
    bool hdrTextureOk = true;
    if (app->hdrData) {
        app->hdrRenderer.updateTexture(app->hdrData->width, app->hdrData->height,
                                       app->hdrData->pixels.data());
        hdrTextureOk = app->hdrRenderer.hasTexture();
        app->egl.setContentMaxNits(app->hdrData->maxNits);
    } else {
        app->egl.setContentMaxNits(AndroidConfig::HDR_WHITE_NITS);
    }

    // HDR output only makes sense for images with real HDR data (and a texture
    // that actually fit in GPU memory).
    if (app->egl.hdrSupported()) {
        app->pendingHdrToggle.store((app->hdrData && hdrTextureOk) ? 1 : 0);
    }

    LOGI("Loaded image: %dx%d HDR=%s maxNits=%.0f",
         app->currentImage->width, app->currentImage->height,
         app->hdrData ? "yes" : "no",
         app->hdrData ? app->hdrData->maxNits : 0.0f);
}

void applyPendingHdrToggle(AndroidApp* app) {
    int toggle = app->pendingHdrToggle.exchange(-1);
    if (toggle < 0) return;
    bool enable = toggle != 0;
    if (enable && (!app->egl.hdrSupported() || !app->hdrData)) {
        app->hdrOutputWanted.store(false);
        return;
    }

    bool was = app->egl.isHdrActive();
    if (!app->egl.setHdrEnabled(enable)) {
        app->hdrOutputWanted.store(was);
        return;
    }
    app->hdrOutputWanted.store(enable);
    if (!enable) {
        reapplyToneMap(app);
    }
}

void processWindowChanges(AndroidApp* app) {
    if (!app->windowDirty.exchange(false)) return;
    ANativeWindow* newWindow = nullptr;
    {
        std::lock_guard<std::mutex> lock(app->windowMutex);
        newWindow = app->pendingWindow;
        app->pendingWindow = nullptr;
    }
    if (!newWindow) return;

    if (!app->egl.recreateSurface(newWindow)) {
        LOGE("Failed to recreate EGL surface");
        ANativeWindow_release(newWindow);
        return;
    }
    if (app->window) ANativeWindow_release(app->window);
    app->window = newWindow;
    if (app->egl.isHdrActive()) {
        app->hdrRenderer.ensureUiTarget(app->egl.width(), app->egl.height());
    }
}

void zoomAround(AndroidApp* app, float centerX, float centerY, float newZoom) {
    newZoom = std::clamp(newZoom, AndroidConfig::ZOOM_MIN, AndroidConfig::ZOOM_MAX);
    float relX = centerX - app->imageX - app->offsetX;
    float relY = centerY - app->imageY - app->offsetY;
    float scale = newZoom / app->zoom;
    app->offsetX -= relX * (scale - 1.0f);
    app->offsetY -= relY * (scale - 1.0f);
    app->zoom = newZoom;
    app->fitToWindow = false;
}

void processTouchEvents(AndroidApp* app) {
    std::vector<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(app->touchMutex);
        if (app->touchQueue.empty()) return;
        events.swap(app->touchQueue);
    }

    ImGuiIO& io = ImGui::GetIO();
    for (const TouchEvent& e : events) {
        if (e.pointerCount >= 2) {
            float dx = e.x[1] - e.x[0];
            float dy = e.y[1] - e.y[0];
            float dist = std::sqrt(dx * dx + dy * dy);
            float cx = (e.x[0] + e.x[1]) * 0.5f;
            float cy = (e.y[0] + e.y[1]) * 0.5f;
            io.AddMousePosEvent(cx, cy);

            if (e.action == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                e.action == AMOTION_EVENT_ACTION_DOWN) {
                app->pinching = true;
                app->mouseDown = false;
                app->gestureMoved = true;
                app->pinchStartDist = dist;
                app->pinchStartZoom = app->zoom;
                io.AddMouseButtonEvent(0, false);
            } else if (e.action == AMOTION_EVENT_ACTION_MOVE) {
                if (!app->pinching) {
                    app->pinching = true;
                    app->pinchStartDist = dist;
                    app->pinchStartZoom = app->zoom;
                }
                if (app->pinchStartDist > 1.0f) {
                    zoomAround(app, cx, cy, app->pinchStartZoom * (dist / app->pinchStartDist));
                }
            } else if (e.action == AMOTION_EVENT_ACTION_POINTER_UP ||
                       e.action == AMOTION_EVENT_ACTION_UP ||
                       e.action == AMOTION_EVENT_ACTION_CANCEL) {
                app->pinching = false;
                app->mouseDown = false;
            }
            continue;
        }

        if (e.pointerCount <= 0) continue;
        float x = e.x[0];
        float y = e.y[0];

        if (e.action == AMOTION_EVENT_ACTION_DOWN) {
            app->mouseDown = true;
            app->pinching = false;
            app->gestureMoved = false;
            app->downX = app->lastX = x;
            app->downY = app->lastY = y;
            app->downTimeMs = e.timeMs;
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, true);
        } else if (e.action == AMOTION_EVENT_ACTION_MOVE) {
            io.AddMousePosEvent(x, y);
            if (app->mouseDown && !app->pinching) {
                float dx = x - app->lastX;
                float dy = y - app->lastY;
                if (std::fabs(x - app->downX) > AndroidConfig::TAP_SLOP_PX ||
                    std::fabs(y - app->downY) > AndroidConfig::TAP_SLOP_PX) {
                    app->gestureMoved = true;
                }
                bool insideImage = x >= app->imageX && x <= app->imageX + app->imageW &&
                                   y >= app->imageY && y <= app->imageY + app->imageH;
                if (app->gestureMoved && insideImage) {
                    app->offsetX += dx;
                    app->offsetY += dy;
                    app->fitToWindow = false;
                }
                app->lastX = x;
                app->lastY = y;
            }
        } else if (e.action == AMOTION_EVENT_ACTION_UP) {
            io.AddMousePosEvent(x, y);
            io.AddMouseButtonEvent(0, false);
            int64_t duration = e.timeMs - app->downTimeMs;
            if (!app->gestureMoved && duration < AndroidConfig::TAP_MAX_DURATION_MS) {
                if (app->lastTapTimeMs > 0 &&
                    e.timeMs - app->lastTapTimeMs < AndroidConfig::DOUBLE_TAP_MS) {
                    app->fitToWindow = true;
                    app->zoom = AndroidConfig::DEFAULT_ZOOM;
                    app->offsetX = app->offsetY = 0.0f;
                    app->lastTapTimeMs = 0;
                } else {
                    if (!app->currentImage) {
                        app->openRequest.store(true);
                    }
                    app->lastTapTimeMs = e.timeMs;
                }
            }
            app->mouseDown = false;
            app->pinching = false;
        } else if (e.action == AMOTION_EVENT_ACTION_CANCEL ||
                   e.action == AMOTION_EVENT_ACTION_POINTER_UP) {
            io.AddMouseButtonEvent(0, false);
            app->mouseDown = false;
            app->pinching = false;
        }
    }
}

void renderControls(AndroidApp* app) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * app->density, 8.0f * app->density));
    if (ImGui::Begin("Controls", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings)) {

        if (ImGui::Button("Open Image", ImVec2(-1.0f, 0.0f))) {
            app->openRequest.store(true);
        }

        ImGui::Separator();
        if (app->currentImage) {
            const char* kind = app->hdrData ? "Ultra HDR" : "SDR";
            ImGui::Text("%dx%d  %s", app->currentImage->width, app->currentImage->height, kind);
            if (app->hdrData) {
                ImGui::Text("Peak: %.0f nits", app->hdrData->maxNits);
            }
        } else {
            ImGui::TextDisabled("Tap image or Open Image");
        }

        const char* hdrState = "Unsupported";
        if (app->egl.hdrSupported()) {
            hdrState = app->egl.isHdrActive() ? "HDR10 ON" : "HDR10 OFF";
        }
        ImGui::Text("Display: %s", hdrState);
        ImGui::TextDisabled("Panel max: %.0f nits", app->displayMaxNits);

        ImGui::Separator();
        bool canHdr = app->egl.hdrSupported() && app->hdrData != nullptr;
        if (!canHdr) ImGui::BeginDisabled();
        bool wantHdr = app->hdrOutputWanted.load();
        if (ImGui::Checkbox("HDR Output (PQ)", &wantHdr)) {
            app->hdrOutputWanted.store(wantHdr);
            app->pendingHdrToggle.store(wantHdr ? 1 : 0);
        }
        if (!canHdr) ImGui::EndDisabled();

        bool hasHdr = app->hdrData != nullptr;
        bool hdrOn = app->egl.isHdrActive();
        if (!hasHdr || hdrOn) ImGui::BeginDisabled();
        if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* modes[] = {"Reinhard", "ACES Filmic", "Uncharted 2"};
            if (ImGui::Combo("Mode", &app->toneMappingMode, modes, 3)) {
                reapplyToneMap(app);
            }
            if (ImGui::SliderFloat("Exposure", &app->exposure,
                                   AndroidConfig::EXPOSURE_MIN, AndroidConfig::EXPOSURE_MAX,
                                   "%.2f", ImGuiSliderFlags_Logarithmic)) {
                if (!hdrOn) reapplyToneMap(app);
            }
            if (ImGui::SliderFloat("Gamma", &app->gamma,
                                   AndroidConfig::GAMMA_MIN, AndroidConfig::GAMMA_MAX, "%.2f")) {
                if (!hdrOn) reapplyToneMap(app);
            }
        }
        if (!hasHdr || hdrOn) ImGui::EndDisabled();
        if (hdrOn) {
            ImGui::TextDisabled("HDR direct output: exposure only");
            if (ImGui::SliderFloat("HDR Exposure", &app->exposure,
                                   AndroidConfig::EXPOSURE_MIN, AndroidConfig::EXPOSURE_MAX,
                                   "%.2f", ImGuiSliderFlags_Logarithmic)) {
                // Applied by the HDR shader; no CPU tone map needed.
            }
        }

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Fit to Window", &app->fitToWindow);
            if (ImGui::SliderFloat("Zoom", &app->zoom,
                                   AndroidConfig::ZOOM_MIN, AndroidConfig::ZOOM_MAX,
                                   "%.2fx", ImGuiSliderFlags_Logarithmic)) {
                app->fitToWindow = false;
            }
            if (ImGui::Button("Reset")) {
                app->zoom = AndroidConfig::DEFAULT_ZOOM;
                app->offsetX = app->offsetY = 0.0f;
                app->fitToWindow = true;
            }
        }

        if (app->exifData.valid && ImGui::CollapsingHeader("EXIF")) {
            if (!app->exifData.make.empty())
                ImGui::Text("Make: %s", app->exifData.make.c_str());
            if (!app->exifData.model.empty())
                ImGui::Text("Model: %s", app->exifData.model.c_str());
            ImGui::Text("Shutter: %s", app->exifData.exposureStr().c_str());
            ImGui::Text("Aperture: %s", app->exifData.apertureStr().c_str());
            if (app->exifData.isoSpeed > 0)
                ImGui::Text("ISO: %u", app->exifData.isoSpeed);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void renderImageView(AndroidApp* app, const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;
    bool hdrOn = app->egl.isHdrActive();
    if (hdrOn) flags |= ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##ImageView", nullptr, flags)) {
        ImVec2 cpos = ImGui::GetCursorScreenPos();
        ImVec2 csz = ImGui::GetContentRegionAvail();
        app->imageX = cpos.x;
        app->imageY = cpos.y;
        app->imageW = std::max(csz.x, 1.0f);
        app->imageH = std::max(csz.y, 1.0f);
        app->hdrDrawValid = false;

        if (app->currentImage && app->currentImage->width > 0 && app->currentImage->height > 0) {
            float iw = 0.0f, ih = 0.0f;
            if (app->fitToWindow) {
                float sx = csz.x / (float)app->currentImage->width;
                float sy = csz.y / (float)app->currentImage->height;
                app->zoom = std::min(sx, sy);
                iw = app->currentImage->width * app->zoom;
                ih = app->currentImage->height * app->zoom;
                app->offsetX = (csz.x - iw) * 0.5f;
                app->offsetY = (csz.y - ih) * 0.5f;
            } else {
                iw = app->currentImage->width * app->zoom;
                ih = app->currentImage->height * app->zoom;
            }

            ImVec2 ip(cpos.x + app->offsetX, cpos.y + app->offsetY);
            ImVec2 ie(ip.x + iw, ip.y + ih);

            if (hdrOn) {
                if (app->hdrRenderer.hasTexture()) {
                    app->hdrRect[0] = ip.x;
                    app->hdrRect[1] = ip.y;
                    app->hdrRect[2] = ie.x;
                    app->hdrRect[3] = ie.y;
                    app->hdrDrawValid = true;
                }
            } else if (app->sdrTexture != 0) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                // ImGui uses 16-bit indices per draw list; keep the decorative
                // checkerboard bounded so large images cannot overflow it.
                float area = std::max(1.0f, iw * ih);
                float capGs = std::sqrt(area / 4096.0f);
                float gs = std::max(std::max(4.0f, 16.0f * app->zoom), capGs);
                float vx0 = std::max(ip.x, cpos.x);
                float vy0 = std::max(ip.y, cpos.y);
                float vx1 = std::min(ie.x, cpos.x + csz.x);
                float vy1 = std::min(ie.y, cpos.y + csz.y);
                if (vx1 > vx0 && vy1 > vy0) {
                    int ix0 = (int)std::floor((vx0 - ip.x) / gs);
                    int ix1 = (int)std::floor((vx1 - ip.x) / gs);
                    int iy0 = (int)std::floor((vy0 - ip.y) / gs);
                    int iy1 = (int)std::floor((vy1 - ip.y) / gs);
                    for (int iy = iy0; iy <= iy1; ++iy) {
                        for (int ix = ix0; ix <= ix1; ++ix) {
                            ImVec2 a(std::max(ip.x + ix * gs, vx0),
                                     std::max(ip.y + iy * gs, vy0));
                            ImVec2 b(std::min(ip.x + (ix + 1) * gs, vx1),
                                     std::min(ip.y + (iy + 1) * gs, vy1));
                            if (b.x <= a.x || b.y <= a.y) continue;
                            ImU32 c = ((ix + iy) & 1) ? IM_COL32(80, 80, 80, 255)
                                                      : IM_COL32(120, 120, 120, 255);
                            dl->AddRectFilled(a, b, c);
                        }
                    }
                }
                dl->AddImage((ImTextureID)(intptr_t)app->sdrTexture, ip, ie);
            }

            char buf[128];
            std::snprintf(buf, sizeof(buf), "  %dx%d  %.0f%%",
                          app->currentImage->width, app->currentImage->height, app->zoom * 100.0f);
            ImGui::GetWindowDrawList()->AddText(
                    ImVec2(cpos.x + 6, cpos.y + csz.y - 22.0f * app->density),
                    ImGui::GetColorU32(ImGuiCol_Text, 0.8f), buf);
        } else {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2(avail.x * 0.5f - 80.0f, avail.y * 0.5f - 20.0f));
            ImGui::TextDisabled("Tap to open image");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void renderUI(AndroidApp* app) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    if (w <= 0 || h <= 0) return;

    bool landscape = w >= h;
    float panelW, panelH;
    ImVec2 panelPos, panelSize, imagePos, imageSize;

    if (landscape) {
        panelW = std::min(AndroidConfig::PANEL_MAX_WIDTH * app->density,
                          w * AndroidConfig::PANEL_LANDSCAPE_RATIO);
        panelH = h;
        panelPos = ImVec2(0, 0);
        panelSize = ImVec2(panelW, panelH);
        imagePos = ImVec2(panelW, 0);
        imageSize = ImVec2(w - panelW, h);
    } else {
        panelW = w;
        panelH = std::min(AndroidConfig::PANEL_MAX_HEIGHT * app->density,
                          h * AndroidConfig::PANEL_PORTRAIT_RATIO);
        panelPos = ImVec2(0, h - panelH);
        panelSize = ImVec2(panelW, panelH);
        imagePos = ImVec2(0, 0);
        imageSize = ImVec2(w, h - panelH);
    }

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    renderControls(app);
    renderImageView(app, imagePos, imageSize);
}

bool initGraphics(AndroidApp* app) {
    if (!app->egl.init(app->window, app->deviceHdr10, app->displayMaxNits)) {
        LOGE("EGL initialization failed");
        return false;
    }
    if (!app->hdrRenderer.init()) {
        LOGE("HDR renderer initialization failed");
        app->egl.destroy();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImFontConfig fontCfg;
    fontCfg.SizePixels = AndroidConfig::FONT_SIZE_PX * app->density;
    io.Fonts->AddFontDefault(&fontCfg);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(app->density);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("ImGui GLES3 backend init failed");
        ImGui::DestroyContext();
        app->hdrRenderer.shutdown();
        app->egl.destroy();
        return false;
    }

    app->sdrTexture = 0;
    app->hdrOutputWanted.store(app->egl.isHdrActive());
    if (app->egl.isHdrActive()) {
        app->hdrRenderer.ensureUiTarget(app->egl.width(), app->egl.height());
    }

    // Context was recreated (e.g. after surface loss): re-upload image textures.
    if (app->currentImage) uploadSdrTexture(app);
    if (app->hdrData) {
        app->hdrRenderer.updateTexture(app->hdrData->width, app->hdrData->height,
                                       app->hdrData->pixels.data());
        app->egl.setContentMaxNits(app->hdrData->maxNits);
    }

    LOGI("Graphics initialized: density=%.2f HDR=%d", app->density,
         (int)app->egl.isHdrActive());
    return true;
}

void shutdownGraphics(AndroidApp* app) {
    if (app->egl.makeCurrent()) {
        app->hdrRenderer.shutdown();
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    app->sdrTexture = 0;
    app->hdrDrawValid = false;
    app->egl.destroy();
}

void renderLoop(AndroidApp* app) {
    if (!initGraphics(app)) {
        app->loopRunning.store(false);
        return;
    }

    while (app->loopRunning.load()) {
        if (app->paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        if (!app->egl.makeCurrent()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        static auto lastFrame = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;

        processWindowChanges(app);
        processPendingLoad(app);
        processTouchEvents(app);
        applyPendingHdrToggle(app);
        if (app->sdrDirty) uploadSdrTexture(app);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)app->egl.width(), (float)app->egl.height());
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = dt;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        renderUI(app);
        ImGui::Render();

        int w = app->egl.width();
        int h = app->egl.height();
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (app->egl.isHdrActive() && app->hdrDrawValid && app->hdrRenderer.hasTexture()) {
            app->hdrRenderer.draw(app->hdrRect[0], app->hdrRect[1],
                                  app->hdrRect[2], app->hdrRect[3],
                                  w, h, app->exposure);
        }

        if (app->egl.isHdrActive()) {
            app->hdrRenderer.ensureUiTarget(w, h);
            app->hdrRenderer.beginUiPass();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            app->hdrRenderer.compositeUi();
        } else {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        if (!app->egl.swapBuffers()) {
            LOGE("eglSwapBuffers failed: 0x%x", eglGetError());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    shutdownGraphics(app);
    LOGI("Render thread stopped");
}

void startRender(AndroidApp* app) {
    if (app->renderThread.joinable()) return;
    if (!app->window) return;
    app->loopRunning.store(true);
    app->renderThread = std::thread(renderLoop, app);
}

void stopRender(AndroidApp* app) {
    app->loopRunning.store(false);
    if (app->renderThread.joinable()) app->renderThread.join();
}

void loadImageAsync(AndroidApp* app, const std::string& path) {
    if (app->loadThread.joinable()) app->loadThread.join();
    app->loadThread = std::thread([app, path]() {
        auto result = std::make_unique<LoadResult>();
        auto image = std::make_unique<ImageData>();
        std::unique_ptr<HDRData> hdr;
        ExifData exif;

        bool ok = loadImageFile(path, *image, hdr, &exif);
        if (!ok) {
            LOGI("Ultra HDR decode failed, trying regular image: %s", path.c_str());
            image->pixels.clear();
            image->width = image->height = 0;
            hdr.reset();
            ok = loadRegularImage(path, *image, &exif);
        }

        if (ok && !image->pixels.empty() && image->width > 0 && image->height > 0) {
            result->sdr = std::move(image);
            result->hdr = std::move(hdr);
            result->exif = exif;
            result->ok = true;
        } else {
            LOGE("Failed to load image: %s", path.c_str());
        }

        {
            std::lock_guard<std::mutex> lock(app->loadMutex);
            app->pending = std::move(*result);
            app->hasPending = true;
        }
    });
}

void queueTouch(AndroidApp* app, const TouchEvent& event) {
    std::lock_guard<std::mutex> lock(app->touchMutex);
    if (app->touchQueue.size() > 128) {
        app->touchQueue.erase(app->touchQueue.begin());
    }
    app->touchQueue.push_back(event);
}

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnSurfaceCreated(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jobject surface,
        jint width, jint height, jfloat density, jboolean hdr10, jfloat maxNits) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) {
        app = new AndroidApp();
        g_app = app;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("Failed to acquire ANativeWindow");
        return (jlong)(intptr_t)app;
    }

    stopRender(app);
    if (app->window) {
        ANativeWindow_release(app->window);
        app->window = nullptr;
    }

    app->window = window;
    app->width = width;
    app->height = height;
    app->density = density > 0.0f ? density : 1.0f;
    app->deviceHdr10 = hdr10 == JNI_TRUE;
    app->displayMaxNits = maxNits > 0.0f ? maxNits : 1000.0f;

    LOGI("surfaceCreated: %dx%d density=%.2f hdr10=%d maxNits=%.0f",
         width, height, app->density, (int)app->deviceHdr10, app->displayMaxNits);

    startRender(app);
    return (jlong)(intptr_t)app;
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnSurfaceChanged(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jobject surface,
        jint width, jint height) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return;

    app->width = width;
    app->height = height;

    if (!app->renderThread.joinable()) {
        // No render thread yet: replace the window directly.
        if (app->window) ANativeWindow_release(app->window);
        app->window = window;
        startRender(app);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(app->windowMutex);
        if (app->pendingWindow) ANativeWindow_release(app->pendingWindow);
        app->pendingWindow = window;
    }
    app->windowDirty.store(true);
    LOGI("surfaceChanged: %dx%d", width, height);
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnSurfaceDestroyed(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    stopRender(app);
    if (app->window) {
        ANativeWindow_release(app->window);
        app->window = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(app->windowMutex);
        if (app->pendingWindow) {
            ANativeWindow_release(app->pendingWindow);
            app->pendingWindow = nullptr;
        }
    }
    LOGI("surfaceDestroyed");
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnResume(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    app->paused.store(false);
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnPause(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    app->paused.store(true);
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnDestroy(JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    stopRender(app);
    if (app->loadThread.joinable()) app->loadThread.join();
    if (app->window) {
        ANativeWindow_release(app->window);
        app->window = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(app->windowMutex);
        if (app->pendingWindow) {
            ANativeWindow_release(app->pendingWindow);
            app->pendingWindow = nullptr;
        }
    }
    if (g_app == app) g_app = nullptr;
    delete app;
    LOGI("Native app destroyed");
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnLoadImage(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jstring jpath) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app || !jpath) return;
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    if (path) {
        std::string copy(path);
        env->ReleaseStringUTFChars(jpath, path);
        loadImageAsync(app, copy);
    }
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnTouch(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jint action,
        jint pointerCount, jfloatArray xs, jfloatArray ys) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app || !xs || !ys) return;

    TouchEvent event;
    event.action = action;
    event.pointerCount = std::min((int)pointerCount, 2);
    event.timeMs = nowMs();

    jsize xlen = env->GetArrayLength(xs);
    jsize ylen = env->GetArrayLength(ys);
    int count = std::min(event.pointerCount, (int)std::min(xlen, ylen));
    if (count > 0) {
        jfloat buf[2] = {0, 0};
        env->GetFloatArrayRegion(xs, 0, count, buf);
        for (int i = 0; i < count; ++i) event.x[i] = buf[i];
        env->GetFloatArrayRegion(ys, 0, count, buf);
        for (int i = 0; i < count; ++i) event.y[i] = buf[i];
    }
    event.pointerCount = count;
    queueTouch(app, event);
}

JNIEXPORT jboolean JNICALL
Java_com_uhdrviewer_MainActivity_nativeConsumeOpenRequest(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    AndroidApp* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return JNI_FALSE;
    return app->openRequest.exchange(false) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
