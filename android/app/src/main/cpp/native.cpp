#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengles2.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <string>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <jni.h>
#include <fstream>
#include <algorithm>

#include "ultrahdr_api.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "hdr_types.h"
#include "stb_image.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "UHDRViewer", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "UHDRViewer", __VA_ARGS__)

struct AndroidApp {
    SDL_Window* window = nullptr;
    SDL_GLContext gl_context = nullptr;
    ANativeWindow* native_window = nullptr;

    std::unique_ptr<ImageData> currentImage;
    std::unique_ptr<HDRData> hdrData;
    ExifData exifData;
    void* texture = nullptr;

    float exposure = 1.0f;
    float gamma = 2.2f;
    int toneMappingMode = 0;
    bool toneMapDirty = false;

    float offsetX = 0.0f, offsetY = 0.0f;
    float zoom = 1.0f;
    bool fitToWindow = true;

    bool running = false;
    bool textureDirty = false;
    int width = 0, height = 0;
};

static AndroidApp* s_app = nullptr;

static float ringbuf(float x) { return x / (1.0f + x); }
static float acesFilm(float x) {
    float a=2.51f, b=0.03f, c=2.43f, d=0.59f, e=0.14f;
    float v = (x*(a*x+b)) / (x*(c*x+d)+e);
    return v<0?0:(v>1?1:v);
}
static float unch2(float x) {
    float A=0.15f, B=0.50f, C=0.10f, D=0.20f, E=0.02f, F=0.30f, W=11.2f;
    float curr = ((x*2*(A*x*2+C*B)+D*E)/(x*2*(A*x*2+B)+D*F))-E/F;
    float white = ((W*2*(A*W*2+C*B)+D*E)/(W*2*(A*W*2+B)+D*F))-E/F;
    return curr/white;
}

static void applyToneMap(AndroidApp* app) {
    if (!app->hdrData || !app->currentImage) return;
    app->currentImage->width = app->hdrData->width;
    app->currentImage->height = app->hdrData->height;
    if (app->currentImage->pixels) delete[] app->currentImage->pixels;
    app->currentImage->pixels = new uint8_t[(size_t)app->hdrData->width * app->hdrData->height * 4];

    const float* hdr = app->hdrData->pixels.data();
    uint8_t* sdr = app->currentImage->pixels;
    int w = app->hdrData->width, h = app->hdrData->height;
    float invG = 1.0f / app->gamma;
    for (int i = 0; i < w * h; i++) {
        float r = hdr[i*4+0] * app->exposure;
        float g = hdr[i*4+1] * app->exposure;
        float b = hdr[i*4+2] * app->exposure;
        switch (app->toneMappingMode) {
            case 0: r=ringbuf(r); g=ringbuf(g); b=ringbuf(b); break;
            case 1: r=acesFilm(r); g=acesFilm(g); b=acesFilm(b); break;
            case 2: r=unch2(r); g=unch2(g); b=unch2(b); break;
        }
        if (r<0) r=0; if (g<0) g=0; if (b<0) b=0;
        r = std::pow(r, invG); g = std::pow(g, invG); b = std::pow(b, invG);
        int ri = (int)(r*255+0.5f); if (ri>255) ri=255;
        int gi = (int)(g*255+0.5f); if (gi>255) gi=255;
        int bi = (int)(b*255+0.5f); if (bi>255) bi=255;
        sdr[i*4+0] = (uint8_t)ri; sdr[i*4+1] = (uint8_t)gi;
        sdr[i*4+2] = (uint8_t)bi; sdr[i*4+3] = 255;
    }
    app->textureDirty = true;
}

static void loadImage(AndroidApp* app, const std::string& path) {
    if (app->texture) {
        glDeleteTextures(1, (GLuint*)&app->texture);
        app->texture = nullptr;
    }
    app->hdrData.reset();
    app->currentImage.reset();
    app->exifData = ExifData{};

    LOGI("Loading: %s", path.c_str());

    auto image = std::make_unique<ImageData>();
    std::unique_ptr<HDRData> hdr;
    ExifData exif;
    bool ok = false;

    std::ifstream f(path, std::ios::binary|std::ios::ate);
    if (f.is_open()) {
        auto sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data((size_t)sz);
        if (f.read((char*)data.data(), sz)) {
            exif = parseExif(data.data(), data.size());

            if (is_uhdr_image(data.data(), (int)data.size())) {
                LOGI("Ultra HDR detected");
                uhdr_codec_private_t* dec = uhdr_create_decoder();
                if (dec) {
                    uhdr_compressed_image_t img{};
                    img.data = data.data();
                    img.data_sz = data.size();
                    img.capacity = data.size();

                    if (uhdr_dec_set_image(dec, &img).error_code == UHDR_CODEC_OK &&
                        uhdr_dec_set_out_img_format(dec, UHDR_IMG_FMT_32bppRGBA8888).error_code == UHDR_CODEC_OK) {
                        uhdr_dec_set_out_color_transfer(dec, UHDR_CT_SRGB);
                        if (uhdr_decode(dec).error_code == UHDR_CODEC_OK) {
                            uhdr_raw_image_t* decImg = uhdr_get_decoded_image(dec);
                            if (decImg) {
                                int w = (int)decImg->w, h = (int)decImg->h;
                                uint8_t* src = (uint8_t*)decImg->planes[UHDR_PLANE_PACKED];
                                size_t stride = decImg->stride[UHDR_PLANE_PACKED];

                                hdr = std::make_unique<HDRData>();
                                hdr->width = w; hdr->height = h;
                                hdr->pixels.resize((size_t)w*h*4);

                                image->width = w; image->height = h;
                                image->channels = 4;
                                image->filePath = path;
                                image->pixels = new uint8_t[(size_t)w*h*4];

                                for (int y = 0; y < h; y++) {
                                    uint8_t* dst = image->pixels + (size_t)y*w*4;
                                    float* hd = hdr->pixels.data() + (size_t)y*w*4;
                                    uint8_t* sr = src + (size_t)y*stride*4;
                                    for (int x = 0; x < w; x++) {
                                        dst[x*4+0] = sr[x*4+0];
                                        dst[x*4+1] = sr[x*4+1];
                                        dst[x*4+2] = sr[x*4+2];
                                        dst[x*4+3] = 255;
                                        hd[x*4+0] = sr[x*4+0]/255.0f;
                                        hd[x*4+1] = sr[x*4+1]/255.0f;
                                        hd[x*4+2] = sr[x*4+2]/255.0f;
                                        hd[x*4+3] = 1.0f;
                                    }
                                }
                                ok = true;
                            }
                        }
                    }
                    uhdr_release_decoder(dec);
                }
            }

            if (!ok) {
                LOGI("Falling back to stb_image");
                int w=0, h=0, ch=0;
                unsigned char* imgd = stbi_load(path.c_str(), &w, &h, &ch, 4);
                if (imgd && w > 0 && h > 0) {
                    image->width = w; image->height = h;
                    image->channels = 4;
                    image->filePath = path;
                    image->pixels = new uint8_t[(size_t)w*h*4];
                    memcpy(image->pixels, imgd, (size_t)w*h*4);
                    stbi_image_free(imgd);
                    ok = true;
                }
            }
        }
    }

    if (ok && image->pixels && image->width > 0 && image->height > 0) {
        app->currentImage = std::move(image);
        app->hdrData = std::move(hdr);
        app->exifData = exif;

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, app->currentImage->width, app->currentImage->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, app->currentImage->pixels);
        app->texture = (void*)(intptr_t)tex;

        if (app->hdrData) applyToneMap(app);

        app->fitToWindow = true;
        LOGI("Loaded: %dx%d", app->currentImage->width, app->currentImage->height);
    } else {
        LOGE("Failed to load: %s", path.c_str());
    }
}

static void renderUI(AndroidApp* app) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float toolbarH = 40.0f;
    float panelW = 200.0f;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open")) {
                // On Android, this is handled by the Java side
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                SDL_Event e;
                e.type = SDL_QUIT;
                SDL_PushEvent(&e);
            }
            ImGui::EndMenu();
        }
        ImGui::SameLine(vp->WorkSize.x - 200);
        if (app->currentImage) {
            const char* type = app->hdrData ? "HDR" : "SDR";
            ImGui::Text("%dx%d %s | %.0f%%", app->currentImage->width,
                        app->currentImage->height, type, app->zoom * 100.0f);
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(0, toolbarH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelW, vp->WorkSize.y - toolbarH), ImGuiCond_Always);
    if (ImGui::Begin("Controls", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        bool hasHDR = (app->hdrData != nullptr);
        if (!hasHDR) ImGui::BeginDisabled();

        if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* modes[] = {"Reinhard", "ACES Filmic", "Uncharted 2"};
            if (ImGui::Combo("Mode", &app->toneMappingMode, modes, 3)) {
                if (app->hdrData) { applyToneMap(app); }
            }
            if (ImGui::SliderFloat("Exposure", &app->exposure, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
                if (app->hdrData) applyToneMap(app);
            }
            if (ImGui::SliderFloat("Gamma", &app->gamma, 1.0f, 4.0f, "%.2f")) {
                if (app->hdrData) applyToneMap(app);
            }
        }

        if (!hasHDR) ImGui::EndDisabled();

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Fit to Window", &app->fitToWindow)) {}
            ImGui::SliderFloat("Zoom", &app->zoom, 0.05f, 20.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
            if (ImGui::Button("Reset")) {
                app->zoom = 1.0f; app->offsetX = 0; app->offsetY = 0; app->fitToWindow = true;
            }
        }

        if (app->exifData.valid) {
            if (ImGui::CollapsingHeader("EXIF")) {
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
    }

    ImGui::SetNextWindowPos(ImVec2(panelW, toolbarH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - panelW, vp->WorkSize.y - toolbarH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##ImageView", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings)) {
        if (app->texture && app->currentImage) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 cpos = ImGui::GetCursorScreenPos();
            ImVec2 csz = ImGui::GetContentRegionAvail();

            float iw, ih;
            if (app->fitToWindow) {
                float sx = csz.x / app->currentImage->width;
                float sy = csz.y / app->currentImage->height;
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

            float gs = std::max(4.0f, 16.0f * app->zoom);
            for (float y = ip.y; y < ie.y; y += gs) {
                for (float x = ip.x; x < ie.x; x += gs) {
                    int ix = (int)((x - ip.x) / gs), iy = (int)((y - ip.y) / gs);
                    ImU32 c = ((ix + iy) % 2 == 0) ? IM_COL32(80,80,80,255) : IM_COL32(120,120,120,255);
                    dl->AddRectFilled(ImVec2(x,y), ImVec2(std::min(x+gs,ie.x), std::min(y+gs,ie.y)), c);
                }
            }
            dl->AddImage((ImTextureID)app->texture, ip, ie);

            char buf[128];
            snprintf(buf, sizeof(buf), "  %dx%d  %.0f%%", app->currentImage->width, app->currentImage->height, app->zoom * 100.0f);
            dl->AddText(ImVec2(cpos.x+4, cpos.y+csz.y-20), IM_COL32(200,200,200,200), buf);
        } else {
            ImVec2 a = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2(a.x*0.5f-80, a.y*0.5f-20));
            ImGui::TextDisabled("Tap to open image");
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();
}

static void updateTexture(AndroidApp* app) {
    if (app->textureDirty && app->currentImage) {
        GLuint tex = (GLuint)(intptr_t)app->texture;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, app->currentImage->width, app->currentImage->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, app->currentImage->pixels);
        app->textureDirty = false;
    }
}

static void mainLoop(AndroidApp* app) {
    while (app->running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) {
                app->running = false;
            }
        }

        if (!app->running) break;

        SDL_GL_MakeCurrent(app->window, app->gl_context);
        glViewport(0, 0, app->width, app->height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        renderUI(app);

        updateTexture(app);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(app->window);
    }
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnCreate(JNIEnv* env, jobject thiz, jobject surface) {
    if (s_app) {
        delete s_app;
    }
    s_app = new AndroidApp();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return 0;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
    if (!win) {
        LOGE("Failed to get native window");
        return 0;
    }
    s_app->native_window = win;
    s_app->width = ANativeWindow_getWidth(win);
    s_app->height = ANativeWindow_getHeight(win);

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Landscape");
    s_app->window = SDL_CreateWindowFrom(surface);
    if (!s_app->window) {
        LOGE("SDL_CreateWindowFrom failed: %s", SDL_GetError());
        return 0;
    }

    s_app->gl_context = SDL_GL_CreateContext(s_app->window);
    if (!s_app->gl_context) {
        LOGE("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(s_app->window, s_app->gl_context);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    LOGI("App created: %dx%d", s_app->width, s_app->height);
    s_app->running = true;

    return (jlong)(intptr_t)s_app;
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnSurfaceChanged(JNIEnv* env, jobject thiz,
        jlong handle, jobject surface, jint width, jint height) {
    auto* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    app->width = width;
    app->height = height;
    LOGI("Surface changed: %dx%d", width, height);
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnResume(JNIEnv* env, jobject thiz, jlong handle) {
    auto* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    app->running = true;
    mainLoop(app);
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnPause(JNIEnv* env, jobject thiz, jlong handle) {
    auto* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;
    app->running = false;
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnDestroy(JNIEnv* env, jobject thiz, jlong handle) {
    auto* app = (AndroidApp*)(intptr_t)handle;
    if (!app) return;

    app->running = false;

    if (app->texture) {
        glDeleteTextures(1, (GLuint*)&app->texture);
        app->texture = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (app->gl_context) SDL_GL_DeleteContext(app->gl_context);
    if (app->window) SDL_DestroyWindow(app->window);
    if (app->native_window) ANativeWindow_release(app->native_window);
    SDL_Quit();

    delete app;
    s_app = nullptr;
    LOGI("App destroyed");
}

JNIEXPORT void JNICALL
Java_com_uhdrviewer_MainActivity_nativeOnLoadImage(JNIEnv* env, jobject thiz,
        jlong handle, jstring jpath) {
    auto* app = (AndroidApp*)(intptr_t)handle;
    if (!app || !jpath) return;
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    if (path) {
        loadImage(app, std::string(path));
        env->ReleaseStringUTFChars(jpath, path);
    }
}

}
