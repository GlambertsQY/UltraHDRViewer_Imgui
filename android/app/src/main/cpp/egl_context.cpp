#include "egl_context.h"

#include <android/log.h>
#include <cstring>
#include <vector>

#define TAG "UHDRViewer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif
#ifndef EGL_COLOR_COMPONENT_TYPE_EXT
#define EGL_COLOR_COMPONENT_TYPE_EXT 0x3339
#endif
#ifndef EGL_COLOR_COMPONENT_TYPE_FIXED_EXT
#define EGL_COLOR_COMPONENT_TYPE_FIXED_EXT 0x333A
#endif
#ifndef EGL_GL_COLORSPACE_KHR
#define EGL_GL_COLORSPACE_KHR 0x309D
#endif
#ifndef EGL_GL_COLORSPACE_BT2020_PQ_EXT
#define EGL_GL_COLORSPACE_BT2020_PQ_EXT 0x3340
#endif
#ifndef EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT
#define EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT 0x3341
#define EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT 0x3342
#define EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT 0x3343
#define EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT 0x3344
#define EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT 0x3345
#define EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT 0x3346
#define EGL_SMPTE2086_WHITE_POINT_X_EXT 0x3347
#define EGL_SMPTE2086_WHITE_POINT_Y_EXT 0x3348
#define EGL_SMPTE2086_MAX_LUMINANCE_EXT 0x3349
#define EGL_SMPTE2086_MIN_LUMINANCE_EXT 0x334A
#endif
#ifndef EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT
#define EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT 0x3360
#define EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT 0x3361
#endif
#ifndef EGL_METADATA_SCALING_EXT
#define EGL_METADATA_SCALING_EXT 50000
#endif

static bool extListed(const char* list, const char* name) {
    if (!list || !name) return false;
    size_t len = strlen(name);
    const char* p = list;
    while ((p = strstr(p, name)) != nullptr) {
        bool startOk = (p == list) || (p[-1] == ' ');
        bool endOk = (p[len] == ' ') || (p[len] == '\0');
        if (startOk && endOk) return true;
        p += len;
    }
    return false;
}

bool EglContext::init(ANativeWindow* window, bool allowHdr, float displayMaxNits) {
    if (m_display != EGL_NO_DISPLAY) return true;
    if (!window) return false;

    m_displayMaxNits = displayMaxNits > 0.0f ? displayMaxNits : 1000.0f;
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(m_display, &major, &minor)) {
        LOGE("eglInitialize failed: 0x%x", eglGetError());
        m_display = EGL_NO_DISPLAY;
        return false;
    }
    LOGI("EGL %d.%d initialized", major, minor);
    logExtensions();

    const char* exts = eglQueryString(m_display, EGL_EXTENSIONS);
    m_hasBt2020Pq = extListed(exts, "EGL_EXT_gl_colorspace_bt2020_pq");
    m_hasSmpte2086 = extListed(exts, "EGL_EXT_surface_SMPTE2086_metadata");
    m_hasCta861_3 = extListed(exts, "EGL_EXT_surface_CTA861_3_metadata");
    m_hdrSupported = allowHdr && m_hasBt2020Pq;

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        LOGE("eglBindAPI failed: 0x%x", eglGetError());
        destroy();
        return false;
    }

    m_window = window;

    bool hdr = false;
    if (m_hdrSupported && chooseConfig(true)) {
        hdr = true;
    } else {
        m_hdrSupported = false;
        if (!chooseConfig(false)) {
            LOGE("No usable EGL config");
            destroy();
            return false;
        }
    }

    EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctxAttrs);
    if (m_context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        destroy();
        return false;
    }

    if (!createSurface(window, hdr)) {
        if (hdr) {
            LOGW("HDR surface creation failed, falling back to SDR");
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
            m_hdrSupported = false;
            if (!chooseConfig(false)) {
                LOGE("No SDR EGL config");
                destroy();
                return false;
            }
            m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctxAttrs);
            if (m_context == EGL_NO_CONTEXT || !createSurface(window, false)) {
                LOGE("SDR surface creation failed");
                destroy();
                return false;
            }
        } else {
            destroy();
            return false;
        }
    }

    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        destroy();
        return false;
    }

    querySurfaceSize();
    LOGI("EGL ready: %dx%d, HDR=%s, displayMax=%.0f nits",
         m_width, m_height, m_hdrActive ? "HDR10 BT2020/PQ" : "SDR", m_displayMaxNits);
    return true;
}

bool EglContext::chooseConfig(bool hdr) {
    EGLint hdrAttrsWithType[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 10, EGL_GREEN_SIZE, 10, EGL_BLUE_SIZE, 10, EGL_ALPHA_SIZE, 2,
        EGL_COLOR_COMPONENT_TYPE_EXT, EGL_COLOR_COMPONENT_TYPE_FIXED_EXT,
        EGL_NONE
    };
    EGLint hdrAttrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 10, EGL_GREEN_SIZE, 10, EGL_BLUE_SIZE, 10, EGL_ALPHA_SIZE, 2,
        EGL_NONE
    };
    EGLint sdrAttrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    const EGLint* candidates[3];
    int count = 0;
    if (hdr) {
        candidates[count++] = hdrAttrsWithType;
        candidates[count++] = hdrAttrs;
    } else {
        candidates[count++] = sdrAttrs;
    }

    for (int i = 0; i < count; ++i) {
        EGLint n = 0;
        if (!eglChooseConfig(m_display, candidates[i], nullptr, 0, &n) || n <= 0) continue;
        std::vector<EGLConfig> configs((size_t)n);
        if (!eglChooseConfig(m_display, candidates[i], configs.data(), n, &n) || n <= 0) continue;
        m_config = configs[0];
        EGLint r = 0, g = 0, b = 0, a = 0;
        eglGetConfigAttrib(m_display, m_config, EGL_RED_SIZE, &r);
        eglGetConfigAttrib(m_display, m_config, EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(m_display, m_config, EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(m_display, m_config, EGL_ALPHA_SIZE, &a);
        LOGI("EGL config: R%d G%d B%d A%d (%s)", r, g, b, a, hdr ? "HDR candidate" : "SDR");
        return true;
    }
    return false;
}

bool EglContext::createSurface(ANativeWindow* window, bool hdr) {
    EGLint surfaceAttrs[5];
    int n = 0;
    if (hdr) {
        surfaceAttrs[n++] = EGL_GL_COLORSPACE_KHR;
        surfaceAttrs[n++] = EGL_GL_COLORSPACE_BT2020_PQ_EXT;
    }
    surfaceAttrs[n++] = EGL_NONE;

    m_surface = eglCreateWindowSurface(m_display, m_config,
                                       (EGLNativeWindowType)window, surfaceAttrs);
    if (m_surface == EGL_NO_SURFACE) {
        LOGW("eglCreateWindowSurface(%s) failed: 0x%x", hdr ? "HDR" : "SDR", eglGetError());
        return false;
    }
    m_hdrActive = hdr;
    if (hdr) {
        applyHdrMetadata(m_displayMaxNits, m_contentMaxNits);
    }
    return true;
}

void EglContext::applyHdrMetadata(float displayMaxNits, float contentMaxNits) {
    if (!m_hdrActive) return;
    if (m_hasSmpte2086) {
        // BT.2020 primaries + D65 white point, scaled by EGL_METADATA_SCALING_EXT.
        struct { EGLint attr; float value; } primaries[] = {
            { EGL_SMPTE2086_DISPLAY_PRIMARY_RX_EXT, 0.708f },
            { EGL_SMPTE2086_DISPLAY_PRIMARY_RY_EXT, 0.292f },
            { EGL_SMPTE2086_DISPLAY_PRIMARY_GX_EXT, 0.170f },
            { EGL_SMPTE2086_DISPLAY_PRIMARY_GY_EXT, 0.797f },
            { EGL_SMPTE2086_DISPLAY_PRIMARY_BX_EXT, 0.131f },
            { EGL_SMPTE2086_DISPLAY_PRIMARY_BY_EXT, 0.046f },
            { EGL_SMPTE2086_WHITE_POINT_X_EXT, 0.3127f },
            { EGL_SMPTE2086_WHITE_POINT_Y_EXT, 0.3290f },
        };
        for (const auto& p : primaries) {
            if (!eglSurfaceAttrib(m_display, m_surface, p.attr,
                                  (EGLint)(p.value * EGL_METADATA_SCALING_EXT))) {
                LOGW("eglSurfaceAttrib SMPTE2086 0x%x failed: 0x%x", p.attr, eglGetError());
            }
        }
        float maxNits = displayMaxNits > 0.0f ? displayMaxNits : 1000.0f;
        eglSurfaceAttrib(m_display, m_surface, EGL_SMPTE2086_MAX_LUMINANCE_EXT,
                         (EGLint)(maxNits * EGL_METADATA_SCALING_EXT));
        eglSurfaceAttrib(m_display, m_surface, EGL_SMPTE2086_MIN_LUMINANCE_EXT,
                         (EGLint)(0.005f * EGL_METADATA_SCALING_EXT));
    }
    if (m_hasCta861_3) {
        float content = contentMaxNits > 0.0f ? contentMaxNits : 203.0f;
        if (content > displayMaxNits) content = displayMaxNits;
        eglSurfaceAttrib(m_display, m_surface, EGL_CTA861_3_MAX_CONTENT_LIGHT_LEVEL_EXT,
                         (EGLint)(content * EGL_METADATA_SCALING_EXT));
        // Conservative frame-average level for still images.
        eglSurfaceAttrib(m_display, m_surface, EGL_CTA861_3_MAX_FRAME_AVERAGE_LEVEL_EXT,
                         (EGLint)(content * 0.5f * EGL_METADATA_SCALING_EXT));
    }
    LOGI("HDR metadata: display %.0f nits, content %.0f nits, SMPTE2086=%d CTA861.3=%d",
         displayMaxNits, contentMaxNits, (int)m_hasSmpte2086, (int)m_hasCta861_3);
}

bool EglContext::setHdrEnabled(bool enable) {
    if (!m_hdrSupported) return false;
    if (m_hdrActive == enable) return true;
    if (!m_window || m_surface == EGL_NO_SURFACE) return false;

    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(m_display, m_surface);
    m_surface = EGL_NO_SURFACE;

    if (!createSurface(m_window, enable)) {
        // Try to restore the previous mode so the app keeps rendering.
        if (!createSurface(m_window, m_hdrActive)) {
            LOGE("Failed to restore EGL surface after HDR toggle");
            return false;
        }
        eglMakeCurrent(m_display, m_surface, m_surface, m_context);
        return false;
    }
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOGE("eglMakeCurrent after HDR toggle failed: 0x%x", eglGetError());
        return false;
    }
    querySurfaceSize();
    LOGI("HDR output %s", enable ? "enabled" : "disabled");
    return true;
}

bool EglContext::recreateSurface(ANativeWindow* window) {
    if (m_display == EGL_NO_DISPLAY || m_context == EGL_NO_CONTEXT) return false;
    if (!window) return false;
    m_window = window;
    if (m_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }
    if (!createSurface(window, m_hdrActive)) {
        LOGW("Surface recreate failed");
        return false;
    }
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        LOGE("eglMakeCurrent after recreate failed: 0x%x", eglGetError());
        return false;
    }
    querySurfaceSize();
    LOGI("Surface recreated: %dx%d HDR=%d", m_width, m_height, (int)m_hdrActive);
    return true;
}

void EglContext::destroySurface() {
    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(m_display, m_surface);
        m_surface = EGL_NO_SURFACE;
    }
}

void EglContext::destroy() {
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE) eglDestroySurface(m_display, m_surface);
        if (m_context != EGL_NO_CONTEXT) eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }
    m_surface = EGL_NO_SURFACE;
    m_context = EGL_NO_CONTEXT;
    m_config = nullptr;
    m_window = nullptr;
    m_display = EGL_NO_DISPLAY;
    m_hdrActive = false;
}

bool EglContext::makeCurrent() {
    if (m_display == EGL_NO_DISPLAY || m_surface == EGL_NO_SURFACE) return false;
    return eglMakeCurrent(m_display, m_surface, m_surface, m_context) == EGL_TRUE;
}

bool EglContext::swapBuffers() {
    if (m_display == EGL_NO_DISPLAY || m_surface == EGL_NO_SURFACE) return false;
    return eglSwapBuffers(m_display, m_surface) == EGL_TRUE;
}

void EglContext::setContentMaxNits(float nits) {
    m_contentMaxNits = nits;
    if (m_hdrActive) applyHdrMetadata(m_displayMaxNits, m_contentMaxNits);
}

void EglContext::querySurfaceSize() {
    m_width = 0;
    m_height = 0;
    if (m_display != EGL_NO_DISPLAY && m_surface != EGL_NO_SURFACE) {
        eglQuerySurface(m_display, m_surface, EGL_WIDTH, &m_width);
        eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &m_height);
    }
}

void EglContext::logExtensions() const {
    const char* exts = eglQueryString(m_display, EGL_EXTENSIONS);
    LOGI("EGL extensions: %s", exts ? exts : "(none)");
}
