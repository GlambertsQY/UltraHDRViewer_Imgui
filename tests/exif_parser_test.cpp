#include <gtest/gtest.h>
#include "exif_parser.h"
#include <fstream>
#include <vector>

TEST(ExifParserTest, ParseUltraHDRImage) {
    // Read test image from project root
    std::ifstream f("MVIMG_20251123_200209.jpg", std::ios::binary | std::ios::ate);
    ASSERT_TRUE(f.is_open()) << "Test image not found in working directory";
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);
    f.close();

    ExifData exif = parseExif(data.data(), data.size());

    // Core: parse must succeed
    ASSERT_TRUE(exif.valid);

    // Camera info should be populated
    EXPECT_FALSE(exif.make.empty());
    EXPECT_FALSE(exif.model.empty());

    // Orientation range (0 when tag absent, 1..8 when present per TIFF spec)
    EXPECT_GE(exif.orientation, 0u);
    EXPECT_LE(exif.orientation, 8u);

    // Image dimensions should be non-zero
    EXPECT_GT(exif.pixelX, 0u);
    EXPECT_GT(exif.pixelY, 0u);

    // GPS: if present, values must be in reasonable ranges
    if (exif.hasGPS) {
        EXPECT_GE(exif.gpsLatitude, -90.0);
        EXPECT_LE(exif.gpsLatitude, 90.0);
        EXPECT_GE(exif.gpsLongitude, -180.0);
        EXPECT_LE(exif.gpsLongitude, 180.0);
    }
}
