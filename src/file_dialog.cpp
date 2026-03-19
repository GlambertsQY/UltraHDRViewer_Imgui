#include "file_dialog.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <string>
#include <cstdio>

std::string openImageDialog(GLFWwindow* owner) {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(owner);
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "Ultra HDR Images (*.jpg;*.jpeg;*.jpe;*.uhdr)\0*.jpg;*.jpeg;*.jpe;*.uhdr\0"
        "JPEG Files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
        "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
#else
    // Linux/macOS fallback using zenity
    FILE* pipe = popen(
        "zenity --file-selection "
        "--file-filter='Ultra HDR images | *.jpg *.jpeg *.jpe *.uhdr' "
        "--file-filter='All files | *'", "r");
    if (!pipe) return "";
    char buf[4096] = {0};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        if (!result.empty() && result.back() == '\n')
            result.pop_back();
    }
    pclose(pipe);
    return result;
#endif
}

std::string openSaveDialog(GLFWwindow* owner) {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = glfwGetWin32Window(owner);
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter =
        "PNG Files (*.png)\0*.png\0"
        "BMP Files (*.bmp)\0*.bmp\0"
        "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
    return "";
#else
    FILE* pipe = popen("zenity --file-selection --save --confirm-overwrite", "r");
    if (!pipe) return "";
    char buf[4096] = {0};
    std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        if (!result.empty() && result.back() == '\n')
            result.pop_back();
    }
    pclose(pipe);
    return result;
#endif
}
