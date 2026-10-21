#include <gtest/gtest.h>
#include "hdr_types.h"
#include <fstream>
#include <vector>
#include <cstring>

TEST(ExifAndroidTest, ParseUltraHDRImage) {
    std::ifstream f("MVIMG_20251123_200209.jpg", std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        GTEST_SKIP() << "Test image MVIMG_20251123_200209.jpg not found";
    }
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(data.data()), sz);
    f.close();

    ExifData exif = parseExif(data.data(), data.size());

    ASSERT_TRUE(exif.valid);
    EXPECT_FALSE(exif.make.empty());
    EXPECT_FALSE(exif.model.empty());
    EXPECT_GE(exif.orientation, 0u);
    EXPECT_LE(exif.orientation, 8u);
    EXPECT_GT(exif.pixelX, 0u);
    EXPECT_GT(exif.pixelY, 0u);
}

TEST(ExifAndroidTest, NullInputReturnsInvalid) {
    ExifData exif = parseExif(nullptr, 0);
    EXPECT_FALSE(exif.valid);
}

TEST(ExifAndroidTest, SmallInputReturnsInvalid) {
    uint8_t data[3] = {0xFF, 0x00, 0x00};
    ExifData exif = parseExif(data, 3);
    EXPECT_FALSE(exif.valid);
}
