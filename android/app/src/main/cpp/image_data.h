#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ImageData {
    uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 4;
    std::string filePath;
    ~ImageData();
    size_t dataSize() const { return (size_t)width * height * channels; }
};
