#pragma once

// Renders the HDR image quad directly to an HDR10 (BT.2020 + PQ) EGL surface
// and provides an SDR offscreen target for the ImGui UI. The UI is composited
// from sRGB into PQ per pixel, which keeps its colors and alpha blending
// behaving like SDR instead of blending PQ-encoded 8-bit colors.
class HdrGles {
public:
    bool init();
    void shutdown();

    bool createTexture(int width, int height);
    void updateTexture(int width, int height, const float* rgba);
    void destroyTexture();
    bool hasTexture() const { return m_texture != 0; }

    void draw(float x0, float y0, float x1, float y1,
              int viewportW, int viewportH, float exposure);

    // Offscreen SDR UI target (RGBA8, transparent background).
    bool ensureUiTarget(int width, int height);
    void beginUiPass();
    void compositeUi();

private:
    unsigned int m_program = 0;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_texture = 0;
    int m_texW = 0;
    int m_texH = 0;

    int m_uViewport = -1;
    int m_uRectMin = -1;
    int m_uRectMax = -1;
    int m_uExposure = -1;
    int m_uTex = -1;

    unsigned int m_uiProgram = 0;
    unsigned int m_uiFbo = 0;
    unsigned int m_uiTexture = 0;
    int m_uiW = 0;
    int m_uiH = 0;
    int m_uiTexUniform = -1;
};
