// colorspace_test.cpp — Unit tests for HDR output color math.
// Pure CPU tests: no Vulkan/GL required (see plan: hdr-output-support.md).

#include <gtest/gtest.h>
#include "colorspace.h"
#include "half_float.h"

#include <cmath>

using namespace ColorSpace;

// ---------- sRGB -> linear ----------

TEST(ColorSpaceTest, SrgbToLinearAnchors) {
    EXPECT_FLOAT_EQ(srgbToLinear(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(srgbToLinear(1.0f), 1.0f);
    EXPECT_NEAR(srgbToLinear(0.04045f), 0.0031308f, 1e-6f);
    EXPECT_NEAR(srgbToLinear(0.5f), 0.2140411f, 1e-6f);
}

// ---------- ST2084 PQ ----------

TEST(ColorSpaceTest, PqEncodeAnchors) {
    EXPECT_FLOAT_EQ(pqEncode(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(pqEncode(PQ_MAX_NITS), 1.0f);
    EXPECT_NEAR(pqEncode(1000.0f), 0.751827f, 1e-3f);   // ITU anchor
    EXPECT_NEAR(pqEncode(203.0f), 0.58f, 1e-2f);        // SDR white ~ 0.58
    // Above-range clamps
    EXPECT_FLOAT_EQ(pqEncode(20000.0f), 1.0f);
    EXPECT_FLOAT_EQ(pqEncode(-5.0f), 0.0f);
}

TEST(ColorSpaceTest, PqRoundTrip) {
    const float nits[] = {0.0f, 0.1f, 1.0f, 50.0f, 203.0f, 1000.0f, 4000.0f, 10000.0f};
    for (float n : nits) {
        EXPECT_NEAR(pqDecode(pqEncode(n)), n, std::max(0.05f, n * 1e-4f)) << "nits=" << n;
    }
}

// ---------- BT.709 -> BT.2020 ----------

TEST(ColorSpaceTest, GamutMatrixProperties) {
    // Rows sum to 1 (preserves white point)
    for (int i = 0; i < 3; i++) {
        float sum = BT709_TO_BT2020[i][0] + BT709_TO_BT2020[i][1] + BT709_TO_BT2020[i][2];
        EXPECT_NEAR(sum, 1.0f, 1e-4f) << "row " << i;
    }
    // White maps to white
    float r, g, b;
    bt709ToBt2020(1.0f, 1.0f, 1.0f, r, g, b);
    EXPECT_NEAR(r, 1.0f, 1e-4f);
    EXPECT_NEAR(g, 1.0f, 1e-4f);
    EXPECT_NEAR(b, 1.0f, 1e-4f);
    // All 6 BT.709 primaries/secondaries map to non-negative BT.2020 components
    const float colors[6][3] = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 0}
    };
    for (auto& c : colors) {
        bt709ToBt2020(c[0], c[1], c[2], r, g, b);
        EXPECT_GE(r, 0.0f);
        EXPECT_GE(g, 0.0f);
        EXPECT_GE(b, 0.0f);
    }
}

// ---------- half-float conversion ----------

TEST(HalfFloatTest, FloatToHalfAnchors) {
    EXPECT_EQ(floatToHalf(0.0f), 0x0000);
    EXPECT_EQ(floatToHalf(1.0f), 0x3C00);
    EXPECT_EQ(floatToHalf(0.5f), 0x3800);
    EXPECT_EQ(floatToHalf(2.0f), 0x4000);
    EXPECT_EQ(floatToHalf(-1.0f), 0xBC00);
    EXPECT_EQ(floatToHalf(65504.0f), 0x7BFF);           // max half
    EXPECT_EQ(floatToHalf(1e10f), 0x7C00);              // overflow -> +inf
}

TEST(HalfFloatTest, FullRoundTrip) {
    // Every half value (except NaN patterns) must survive half->float->half
    for (uint32_t h = 0; h <= 0xFFFF; h++) {
        uint16_t half = (uint16_t)h;
        float f = halfToFloat(half);
        if (std::isnan(f)) continue;                     // NaN payloads not preserved
        EXPECT_EQ(floatToHalf(f), half) << "h=0x" << std::hex << h;
    }
}

TEST(HalfFloatTest, KnownValues) {
    EXPECT_FLOAT_EQ(halfToFloat(0x3C00), 1.0f);
    EXPECT_FLOAT_EQ(halfToFloat(0x7BFF), 65504.0f);
    EXPECT_TRUE(std::isinf(halfToFloat(0x7C00)));
    EXPECT_FLOAT_EQ(halfToFloat(0x0000), 0.0f);
}
