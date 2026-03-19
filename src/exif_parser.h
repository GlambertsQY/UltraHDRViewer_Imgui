#pragma once

#include <string>
#include <cstdint>

struct ExifData {
    bool valid = false;

    // Camera info
    std::string make;
    std::string model;
    std::string software;
    std::string dateTime;

    // Exposure
    double exposureTime = 0;      // seconds, e.g. 1/125 = 0.008
    double fNumber = 0;           // aperture, e.g. 2.8
    uint32_t isoSpeed = 0;        // e.g. 100
    double focalLength = 0;       // mm, e.g. 24.0

    // Image
    uint32_t orientation = 1;     // 1=normal, 3=180, 6=90CW, 8=90CCW
    uint32_t pixelX = 0;          // image width
    uint32_t pixelY = 0;          // image height

    // Lens
    std::string lensMake;
    std::string lensModel;

    // Flash
    uint16_t flash = 0xFFFF;      // 0xFFFF = unknown

    // White balance
    uint16_t whiteBalance = 0xFFFF;

    // GPS (degrees)
    double gpsLatitude = 0;
    double gpsLongitude = 0;
    double gpsAltitude = 0;
    bool hasGPS = false;

    // Ultra HDR specific
    bool isUltraHDR = false;
    std::string colorTransfer;    // SDR, PQ, HLG
    int hdrHeadroom = 0;

    // Format string helpers
    std::string exposureStr() const;
    std::string apertureStr() const;
    std::string focalLengthStr() const;
    std::string orientationStr() const;
};

// Parse EXIF from raw JPEG file data
ExifData parseExif(const uint8_t* data, size_t size);
