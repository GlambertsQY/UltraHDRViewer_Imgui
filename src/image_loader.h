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

// Open a native file dialog
std::string openFileDialog(GLFWwindow* owner, const char* filterName, const char* filterSpec);

// Load Ultra HDR image, returns HDRData for re-tone-mapping
bool loadImageFile(const std::string& filePath, ImageData& outSDR,
                   std::unique_ptr<HDRData>& outHDR,
                   ExifData* outExif = nullptr);

// Load regular image (PNG/JPEG/BMP) via stb_image
bool loadRegularImage(const std::string& filePath, ImageData& outSDR,
                      ExifData* outExif = nullptr);

// Check if file is JPEG (possibly Ultra HDR)
bool isUltraHDRFile(const std::string& filePath);

// Tone map: HDR float RGBA -> SDR uint8 RGBA
void toneMapImage(const float* hdrPixels, uint8_t* sdrPixels, int width, int height,
                  float exposure, float gamma, int toneMappingMode);

// Apply tone mapping from HDRData -> ImageData
void applyToneMap(const HDRData& hdr, ImageData& sdr,
                  float exposure, float gamma, int toneMappingMode);

// Save image to file
bool saveImageToFile(const ImageData& image, const std::string& outputPath);
