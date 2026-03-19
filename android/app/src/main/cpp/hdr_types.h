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

struct HDRData {
    std::vector<float> pixels;
    int width = 0;
    int height = 0;
};

struct ExifData {
    bool valid = false;
    std::string make;
    std::string model;
    std::string software;
    std::string dateTime;
    double exposureTime = 0;
    double fNumber = 0;
    uint32_t isoSpeed = 0;
    double focalLength = 0;
    uint32_t orientation = 1;
    uint32_t pixelX = 0;
    uint32_t pixelY = 0;
    std::string lensMake;
    std::string lensModel;
    uint16_t flash = 0xFFFF;
    uint16_t whiteBalance = 0xFFFF;
    double gpsLatitude = 0;
    double gpsLongitude = 0;
    double gpsAltitude = 0;
    bool hasGPS = false;
    bool isUltraHDR = false;
    std::string colorTransfer;
    int hdrHeadroom = 0;
    std::string exposureStr() const;
    std::string apertureStr() const;
    std::string focalLengthStr() const;
    std::string orientationStr() const;
};

ExifData parseExif(const uint8_t* data, size_t size);
bool loadImageFile(const std::string& filePath, ImageData& outSDR,
                   std::unique_ptr<HDRData>& outHDR,
                   ExifData* outExif = nullptr);
bool loadRegularImage(const std::string& filePath, ImageData& outSDR,
                      ExifData* outExif = nullptr);
bool isUltraHDRFile(const std::string& filePath);
void toneMapImage(const float* hdrPixels, uint8_t* sdrPixels, int width, int height,
                  float exposure, float gamma, int toneMappingMode);
void applyToneMap(const HDRData& hdr, ImageData& sdr,
                  float exposure, float gamma, int toneMappingMode);
bool saveImageToFile(const ImageData& image, const std::string& outputPath);
