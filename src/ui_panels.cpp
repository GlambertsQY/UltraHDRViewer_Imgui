#include "ui_panels.h"
#include "renderer.h"
#include "exif_parser.h"
#include "ultrahdr_api.h"

#include <GLFW/glfw3.h>
#include <cstdio>

namespace UIPanels {

void renderMenuBar(GLFWwindow* window, const ImageData* img, float zoom, bool isHDR,
                   bool& fitToWindow, bool& showInfo, bool& showAbout,
                   std::function<void()> onOpen,
                   std::function<void()> onSave,
                   std::function<void()> onFitToWindow,
                   std::function<void()> onResetView)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", "Ctrl+O")) onOpen();
            if (ImGui::MenuItem("Save SDR As...", "Ctrl+S", false, img != nullptr))
                onSave();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit to Window", "F", &fitToWindow)) {
                if (fitToWindow) onFitToWindow();
            }
            if (ImGui::MenuItem("Reset View", "R")) onResetView();
            ImGui::Separator();
            ImGui::MenuItem("Show Info", "I", &showInfo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) showAbout = true;
            ImGui::EndMenu();
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 320);
        if (img) {
            const char* type = isHDR ? "HDR" : "SDR";
            ImGui::Text("%dx%d %s | %.0f%% | %s",
                        img->width, img->height,
                        type, zoom * 100.0f,
                        img->filePath.c_str());
        } else {
            ImGui::TextDisabled("No image loaded");
        }
        ImGui::EndMainMenuBar();
    }
}

void renderControlPanel(bool hasHDR,
                        float& exposure, float& gamma,
                        int& toneMappingMode, bool& toneMapDirty,
                        bool& fitToWindow, float& zoom,
                        float& offsetX, float& offsetY,
                        std::function<void()> onFitToWindow,
                        std::function<void()> onResetView)
{
    ImGui::SetNextWindowPos(ImVec2(10, 35), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }

    if (!hasHDR) ImGui::BeginDisabled();

    ImGui::Text("Tone Mapping (HDR)");
    ImGui::Separator();

    const char* toneNames[] = {"Reinhard", "ACES Filmic", "Uncharted 2"};
    if (ImGui::Combo("Mode", &toneMappingMode, toneNames, IM_ARRAYSIZE(toneNames)))
        toneMapDirty = true;

    if (ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
        toneMapDirty = true;

    if (ImGui::SliderFloat("Gamma", &gamma, 1.0f, 4.0f, "%.2f"))
        toneMapDirty = true;

    if (ImGui::Button("Reset##tonemap")) {
        exposure = 1.0f; gamma = 2.2f; toneMappingMode = 0;
        toneMapDirty = true;
    }

    if (!hasHDR) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Text("View");
    ImGui::Separator();

    if (ImGui::Checkbox("Fit to Window", &fitToWindow)) {
        if (fitToWindow) onFitToWindow();
    }
    ImGui::SliderFloat("Zoom", &zoom, 0.05f, 20.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
    ImGui::DragFloat("Pan X", &offsetX, 1.0f);
    ImGui::DragFloat("Pan Y", &offsetY, 1.0f);
    if (ImGui::Button("Reset View")) onResetView();

    ImGui::End();
}

void renderInfoPanel(bool& showInfo, const ExifData& exif,
                     const ImageData* img, bool isHDR,
                     float zoom, float exposure, float gamma,
                     int displayWidth)
{
    if (!showInfo || !img) return;
    ImGui::SetNextWindowPos(ImVec2(displayWidth - 320, 40), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Image Info", &showInfo)) {
        ImGui::Text("File: %s", img->filePath.c_str());
        ImGui::Text("Size: %dx%d", img->width, img->height);
        ImGui::Text("Type: %s", isHDR ? "Ultra HDR" : "SDR");
        ImGui::Text("Zoom: %.1f%%", zoom * 100.0f);
        ImGui::Text("Memory: %.2f MB", img->dataSize() / (1024.0f * 1024.0f));
        if (isHDR) {
            ImGui::Text("Exposure: %.2f", exposure);
            ImGui::Text("Gamma: %.2f", gamma);
        }

        if (exif.valid) {
            ImGui::Separator();
            ImGui::Text("Camera");
            if (!exif.make.empty())
                ImGui::Text("Make: %s", exif.make.c_str());
            if (!exif.model.empty())
                ImGui::Text("Model: %s", exif.model.c_str());
            if (!exif.software.empty())
                ImGui::Text("Software: %s", exif.software.c_str());
            if (!exif.dateTime.empty())
                ImGui::Text("DateTime: %s", exif.dateTime.c_str());

            ImGui::Separator();
            ImGui::Text("Exposure");
            ImGui::Text("Shutter: %s", exif.exposureStr().c_str());
            ImGui::Text("Aperture: %s", exif.apertureStr().c_str());
            if (exif.isoSpeed > 0)
                ImGui::Text("ISO: %u", exif.isoSpeed);
            ImGui::Text("Focal Length: %s", exif.focalLengthStr().c_str());
            if (exif.flash != 0xFFFF)
                ImGui::Text("Flash: %s", (exif.flash & 1) ? "On" : "Off");

            ImGui::Separator();
            ImGui::Text("Image");
            ImGui::Text("Orientation: %s", exif.orientationStr().c_str());
            if (exif.pixelX > 0 && exif.pixelY > 0)
                ImGui::Text("EXIF Size: %ux%u", exif.pixelX, exif.pixelY);

            if (!exif.lensMake.empty())
                ImGui::Text("Lens: %s", exif.lensMake.c_str());
            if (!exif.lensModel.empty())
                ImGui::Text("Lens Model: %s", exif.lensModel.c_str());

            if (exif.hasGPS) {
                ImGui::Separator();
                ImGui::Text("GPS");
                ImGui::Text("Lat: %.6f", exif.gpsLatitude);
                ImGui::Text("Lon: %.6f", exif.gpsLongitude);
                ImGui::Text("Alt: %.1f m", exif.gpsAltitude);
            }

            if (exif.isUltraHDR) {
                ImGui::Separator();
                ImGui::Text("Ultra HDR");
                ImGui::Text("Color Transfer: %s", exif.colorTransfer.c_str());
                ImGui::Text("HDR Headroom: %d EV", exif.hdrHeadroom);
            }
        }
    }
    ImGui::End();
}

void renderAboutDialog(bool& showAbout) {
    if (!showAbout) return;
    ImGui::OpenPopup("About##AboutDialog");
    if (ImGui::BeginPopupModal("About##AboutDialog", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ultra HDR Viewer");
        ImGui::Text("Dear ImGui + GLFW + libultrahdr");
        ImGui::Separator();
        ImGui::Text("libultrahdr %s", UHDR_LIB_VERSION_STR);
        if (ImGui::Button("Close")) showAbout = false;
        ImGui::EndPopup();
    }
}

} // namespace UIPanels
