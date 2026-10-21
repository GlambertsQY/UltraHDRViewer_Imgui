#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>

// Owns the EGL display/config/context/window surface used by the Android
// render thread. HDR is HDR10 (BT.2020 + ST2084/PQ) on a 10-bit surface.
class EglContext {
public:
    bool init(ANativeWindow* window, bool allowHdr, float displayMaxNits);
    bool recreateSurface(ANativeWindow* window);
    void destroySurface();
    void destroy();

    // Recreates the window surface as HDR10/PQ or SDR without touching the GL
    // context (so ImGui/GL objects stay valid). Requires hdrSupported().
    bool setHdrEnabled(bool enable);

    bool makeCurrent();
    bool swapBuffers();

    // Updates the CTA-861.3 content light level for the current image.
    void setContentMaxNits(float nits);

    bool isHdrActive() const { return m_hdrActive; }
    bool hdrSupported() const { return m_hdrSupported; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    bool chooseConfig(bool hdr);
    bool createSurface(ANativeWindow* window, bool hdr);
    void querySurfaceSize();
    void applyHdrMetadata(float displayMaxNits, float contentMaxNits);
    void logExtensions() const;

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;
    EGLSurface m_surface = EGL_NO_SURFACE;
    ANativeWindow* m_window = nullptr;

    bool m_hdrActive = false;
    bool m_hdrSupported = false;
    bool m_hasBt2020Pq = false;
    bool m_hasSmpte2086 = false;
    bool m_hasCta861_3 = false;

    int m_width = 0;
    int m_height = 0;
    float m_displayMaxNits = 1000.0f;
    float m_contentMaxNits = 203.0f;
};
