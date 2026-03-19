#pragma once

#include <string>

struct GLFWwindow;

// Open a native file dialog to select an image file
std::string openImageDialog(GLFWwindow* owner);

// Open a native save dialog for exporting
std::string openSaveDialog(GLFWwindow* owner);
