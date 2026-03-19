#pragma once

#include <string>
#include <memory>
#include "renderer.h"
#include "image_loader.h"
#include "exif_parser.h"

struct GLFWwindow;

struct AppConfig {
    std::string backend = "opengl";
    int windowWidth = 1440;
    int windowHeight = 900;
};

class Application {
public:
    Application() = default;
    ~Application();

    bool init(const AppConfig& config);
    void run();
    void shutdown();

private:
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<Renderer> m_renderer;

    // Image state
    std::unique_ptr<ImageData> m_currentImage;
    std::unique_ptr<HDRData> m_hdrData;
    ExifData m_exifData;
    void* m_texture = nullptr;

    // UI state
    bool m_showAbout = false;
    bool m_showInfo = false;
    bool m_isDragging = false;
    double m_dragStartX = 0.0;
    double m_dragStartY = 0.0;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
    float m_zoom = 1.0f;
    bool m_fitToWindow = true;

    // Tone mapping parameters
    float m_exposure = 1.0f;
    float m_gamma = 2.2f;
    int m_toneMappingMode = 0;  // 0=Reinhard, 1=ACES, 2=Uncharted2
    bool m_toneMapDirty = false;

    int m_displayWidth = 0;
    int m_displayHeight = 0;

    void renderUI();
    void renderMenuBar();
    void renderImagePanel();
    void renderControlPanel();
    void renderInfoPanel();
    void renderAboutDialog();

    void openImageFile();
    void saveImageFile();
    void loadImage(const std::string& path);
    void reapplyToneMapping();
    void resetView();
    void fitToWindow();

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void dropCallback(GLFWwindow* window, int count, const char** paths);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static Application* getInstance(GLFWwindow* window);
    static Application* s_instance;
};
