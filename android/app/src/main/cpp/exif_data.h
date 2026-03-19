#pragma once
#include <string>
#include <cstdint>

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
