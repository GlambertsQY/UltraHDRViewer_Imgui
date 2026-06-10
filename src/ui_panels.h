#pragma once
#include "imgui.h"
#include <functional>

struct GLFWwindow;
struct ImageData;
struct ExifData;

namespace UIPanels {

void renderMenuBar(GLFWwindow* window, const ImageData* img, float zoom, bool isHDR,
                   bool& fitToWindow, bool& showInfo, bool& showAbout,
                   std::function<void()> onOpen,
                   std::function<void()> onSave,
                   std::function<void()> onFitToWindow,
                   std::function<void()> onResetView);

void renderControlPanel(bool hasHDR,
                        float& exposure, float& gamma,
                        int& toneMappingMode, bool& toneMapDirty,
                        bool& fitToWindow, float& zoom,
                        float& offsetX, float& offsetY,
                        std::function<void()> onFitToWindow,
                        std::function<void()> onResetView);

void renderInfoPanel(bool& showInfo, const ExifData& exif,
                     const ImageData* img, bool isHDR,
                     float zoom, float exposure, float gamma,
                     int displayWidth);

void renderAboutDialog(bool& showAbout);

} // namespace UIPanels
