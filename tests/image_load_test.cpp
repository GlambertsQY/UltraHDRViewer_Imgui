#include <gtest/gtest.h>
#include "image_loader.h"
#include "renderer.h"
#include <fstream>
#include <memory>

TEST(ImageLoadTest, LoadUltraHDR) {
    ImageData img;
    std::unique_ptr<HDRData> hdr;
    bool ok = loadImageFile("MVIMG_20251123_200209.jpg", img, hdr);
    ASSERT_TRUE(ok);

    // ImageData checks
    EXPECT_GT(img.width, 0);
    EXPECT_GT(img.height, 0);
    EXPECT_FALSE(img.pixels.empty());
    EXPECT_GT(img.dataSize(), 0u);

    // HDRData checks
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->width, img.width);
    EXPECT_EQ(hdr->height, img.height);
    EXPECT_FALSE(hdr->pixels.empty());
    EXPECT_EQ(hdr->pixels.size(), (size_t)img.width * img.height * 4);

    // Save raw RGBA baseline for future pixel-diff comparison
    std::ofstream out("tests/baseline/ref_pixels.raw", std::ios::binary);
    ASSERT_TRUE(out.is_open()) << "Failed to open baseline output file";
    out.write(reinterpret_cast<const char*>(img.pixels.data()), img.dataSize());
    out.close();
}
