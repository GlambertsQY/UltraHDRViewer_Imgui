#include "hdr_gles.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>
#include <cstdio>

#define TAG "UHDRViewer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

const char* kImageVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aUnit;
uniform vec2 uViewport;
uniform vec2 uRectMin;
uniform vec2 uRectMax;
out vec2 vUV;
void main() {
    vUV = aUnit;
    vec2 px = mix(uRectMin, uRectMax, aUnit);
    vec2 ndc = vec2(px.x / uViewport.x * 2.0 - 1.0,
                    1.0 - px.y / uViewport.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kImageFragmentShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform float uExposure;

const float M1 = 0.1593017578125;
const float M2 = 78.84375;
const float C1 = 0.8359375;
const float C2 = 18.8515625;
const float C3 = 18.6875;

vec3 pqEncode(vec3 nits) {
    vec3 y = clamp(nits / 10000.0, 0.0, 1.0);
    vec3 ym = pow(y, vec3(M1));
    return pow((C1 + C2 * ym) / (1.0 + C3 * ym), vec3(M2));
}

void main() {
    // HDRData is linear BT.709 with 1.0 = 203 nits.
    vec3 lin709 = texture(uTex, vUV).rgb * uExposure;
    vec3 nits = lin709 * 203.0;
    vec3 r2020 = vec3(dot(nits, vec3(0.6274039, 0.3292830, 0.0433000)),
                      dot(nits, vec3(0.0690970, 0.9195404, 0.0113600)),
                      dot(nits, vec3(0.0163914, 0.0880132, 0.8956000)));
    fragColor = vec4(pqEncode(max(r2020, vec3(0.0))), 1.0);
}
)";

const char* kUiVertexShader = R"(#version 300 es
layout(location = 0) in vec2 aUnit;
out vec2 vUV;
void main() {
    vUV = aUnit;
    gl_Position = vec4(aUnit * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kUiFragmentShader = R"(#version 300 es
precision highp float;
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uUiTex;

const float M1 = 0.1593017578125;
const float M2 = 78.84375;
const float C1 = 0.8359375;
const float C2 = 18.8515625;
const float C3 = 18.6875;
// UI white in nits; slightly above the 203-nit image reference so the panels
// keep a comfortable brightness when the display switches to HDR.
const float UI_WHITE_NITS = 400.0;

vec3 srgbToLinear(vec3 c) {
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), c));
}

vec3 pqEncode(vec3 nits) {
    vec3 y = clamp(nits / 10000.0, 0.0, 1.0);
    vec3 ym = pow(y, vec3(M1));
    return pow((C1 + C2 * ym) / (1.0 + C3 * ym), vec3(M2));
}

void main() {
    vec4 c = texture(uUiTex, vUV);
    if (c.a <= 0.0001) discard;

    // The UI framebuffer is authored with straight alpha blending over a
    // transparent background, which leaves premultiplied rgb; recover the
    // sRGB color before converting it to BT.2020 / PQ.
    vec3 srgb = c.rgb / c.a;
    vec3 nits = srgbToLinear(srgb) * UI_WHITE_NITS;
    vec3 r2020 = vec3(dot(nits, vec3(0.6274039, 0.3292830, 0.0433000)),
                      dot(nits, vec3(0.0690970, 0.9195404, 0.0113600)),
                      dot(nits, vec3(0.0163914, 0.0880132, 0.8956000)));
    fragColor = vec4(pqEncode(max(r2020, vec3(0.0))), c.a);
}
)";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        LOGE("Shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint linkProgram(const char* vsSource, const char* fsSource) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSource);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        LOGE("Program link failed: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

bool HdrGles::init() {
    if (m_program != 0 && m_uiProgram != 0) return true;

    m_program = linkProgram(kImageVertexShader, kImageFragmentShader);
    if (!m_program) return false;

    m_uViewport = glGetUniformLocation(m_program, "uViewport");
    m_uRectMin = glGetUniformLocation(m_program, "uRectMin");
    m_uRectMax = glGetUniformLocation(m_program, "uRectMax");
    m_uExposure = glGetUniformLocation(m_program, "uExposure");
    m_uTex = glGetUniformLocation(m_program, "uTex");

    m_uiProgram = linkProgram(kUiVertexShader, kUiFragmentShader);
    if (!m_uiProgram) return false;
    m_uiTexUniform = glGetUniformLocation(m_uiProgram, "uUiTex");

    static const float unitQuad[] = {
        0.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  1.0f, 1.0f,  0.0f, 1.0f,
    };
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unitQuad), unitQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    LOGI("HDR GLES renderer initialized");
    return true;
}

void HdrGles::shutdown() {
    destroyTexture();
    if (m_uiFbo) { glDeleteFramebuffers(1, &m_uiFbo); m_uiFbo = 0; }
    if (m_uiTexture) { glDeleteTextures(1, &m_uiTexture); m_uiTexture = 0; }
    m_uiW = m_uiH = 0;
    if (m_uiProgram) { glDeleteProgram(m_uiProgram); m_uiProgram = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
}

bool HdrGles::createTexture(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (m_texture == 0) {
        glGenTextures(1, &m_texture);
    }
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                 GL_RGBA, GL_FLOAT, nullptr);
    m_texW = width;
    m_texH = height;
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("HDR texture allocation failed: 0x%x (%dx%d)", err, width, height);
        destroyTexture();
        return false;
    }
    LOGI("HDR texture allocated: %dx%d RGBA16F", width, height);
    return true;
}

void HdrGles::updateTexture(int width, int height, const float* rgba) {
    if (!rgba) return;
    if (m_texture == 0 || width != m_texW || height != m_texH) {
        if (!createTexture(width, height)) return;
    }
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_FLOAT, rgba);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOGE("HDR texture upload failed: 0x%x", err);
    }
}

void HdrGles::destroyTexture() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_texW = 0;
    m_texH = 0;
}

void HdrGles::draw(float x0, float y0, float x1, float y1,
                   int viewportW, int viewportH, float exposure) {
    if (!m_program || !m_texture || viewportW <= 0 || viewportH <= 0) return;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glUniform1i(m_uTex, 0);
    glUniform2f(m_uViewport, (float)viewportW, (float)viewportH);
    glUniform2f(m_uRectMin, x0, y0);
    glUniform2f(m_uRectMax, x1, y1);
    glUniform1f(m_uExposure, exposure);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glEnable(GL_BLEND);
}

bool HdrGles::ensureUiTarget(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (m_uiFbo != 0 && m_uiW == width && m_uiH == height) return true;

    if (m_uiFbo) { glDeleteFramebuffers(1, &m_uiFbo); m_uiFbo = 0; }
    if (m_uiTexture) { glDeleteTextures(1, &m_uiTexture); m_uiTexture = 0; }

    glGenTextures(1, &m_uiTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &m_uiFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_uiTexture, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("UI framebuffer incomplete: 0x%x", status);
        glDeleteFramebuffers(1, &m_uiFbo);
        glDeleteTextures(1, &m_uiTexture);
        m_uiFbo = 0;
        m_uiTexture = 0;
        return false;
    }

    m_uiW = width;
    m_uiH = height;
    LOGI("UI target allocated: %dx%d RGBA8", width, height);
    return true;
}

void HdrGles::beginUiPass() {
    if (!m_uiFbo) return;
    glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
    glViewport(0, 0, m_uiW, m_uiH);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void HdrGles::compositeUi() {
    if (!m_uiProgram || !m_uiFbo || !m_uiTexture) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_uiW, m_uiH);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_uiProgram);
    glBindVertexArray(m_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_uiTexture);
    glUniform1i(m_uiTexUniform, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
