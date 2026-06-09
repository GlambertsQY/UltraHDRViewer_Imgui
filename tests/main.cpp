#include <gtest/gtest.h>

// main() is provided by GTest::gtest_main.
// Test files go here.

// Stub: satisfy linker for glfwGetWin32Window (referenced by openFileDialog in
// image_loader.cpp, which is compiled for tests but never called by tests).
extern "C" void* glfwGetWin32Window(void* window) { return nullptr; }
