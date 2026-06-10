#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include "renderer.h"
#include "exif_parser.h"

struct GLFWwindow;

// Decoded HDR pixel data (linear float RGBA)
struct HDRData {
    std::vector<float> pixels;  // RGBA float, width*height*4
    int width = 0;
    int height = 0;
};

// Load Ultra HDR image, returns HDRData for re-tone-mapping
bool loadImageFile(const std::string& filePath, ImageData& outSDR,
                   std::unique_ptr<HDRData>& outHDR,
                   ExifData* outExif = nullptr);

// Load regular image (PNG/JPEG/BMP) via stb_image
bool loadRegularImage(const std::string& filePath, ImageData& outSDR,
                      ExifData* outExif = nullptr);

// Check if file is JPEG (possibly Ultra HDR)
bool isUltraHDRFile(const std::string& filePath);

// Tone mapping functions (delegated to tonemap.h for backward compatibility)
#include "tonemap.h"

// Save image to file
bool saveImageToFile(const ImageData& image, const std::string& outputPath);
