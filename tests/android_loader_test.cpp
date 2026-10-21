#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "hdr_types.h"

namespace {

std::string findTestImage() {
    namespace fs = std::filesystem;
    fs::path dir = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = dir / "synthetic_uhdr_test.jpg";
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate.string();
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    return {};
}

} // namespace

// Regression for the Android fake-HDR bug: decoding must request
// UHDR_IMG_FMT_64bppRGBAHalfFloat + UHDR_CT_LINEAR so highlights exceed 1.0.
TEST(AndroidLoaderTest, TrueHDRDecode) {
    std::string path = findTestImage();
    if (path.empty()) {
        GTEST_SKIP() << "synthetic_uhdr_test.jpg not found; run HdrRoundTripTest.* first";
    }

    ImageData sdr;
    std::unique_ptr<HDRData> hdr;
    ExifData exif;
    ASSERT_TRUE(loadImageFile(path, sdr, hdr, &exif));
    ASSERT_NE(hdr, nullptr);
    ASSERT_GT(hdr->width, 0);
    ASSERT_GT(hdr->height, 0);
    ASSERT_EQ(hdr->pixels.size(),
              static_cast<size_t>(hdr->width) * hdr->height * 4);

    float maxLinear = 0.0f;
    for (size_t i = 0; i < hdr->pixels.size(); ++i) {
        if ((i & 3) == 3) continue; // skip alpha
        maxLinear = std::max(maxLinear, hdr->pixels[i]);
    }
    EXPECT_GT(maxLinear, 1.0f) << "HDRData has no values above SDR white (fake HDR)";
    EXPECT_GT(hdr->maxNits, 203.0f);

    EXPECT_EQ(sdr.pixels.size(),
              static_cast<size_t>(hdr->width) * hdr->height * 4);
    EXPECT_EQ(sdr.width, hdr->width);
    EXPECT_EQ(sdr.height, hdr->height);
}
