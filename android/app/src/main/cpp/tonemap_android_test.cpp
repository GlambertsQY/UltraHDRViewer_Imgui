#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include "hdr_types.h"

class ToneMapAndroidTest : public ::testing::Test {
protected:
    static constexpr int kW = 4, kH = 4, kN = kW * kH;

    void SetUp() override {
        for (int i = 0; i < kN; i++) {
            hdr_[i*4 + 0] = 0.25f;
            hdr_[i*4 + 1] = 0.50f;
            hdr_[i*4 + 2] = 0.75f;
            hdr_[i*4 + 3] = 1.00f;
        }
    }

    float hdr_[kN * 4] = {};
    uint8_t sdr_[kN * 4] = {};
};

// 1. Output bounds [0, 255] for all three modes
TEST_F(ToneMapAndroidTest, OutputInRangeReinhard) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 0);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

TEST_F(ToneMapAndroidTest, OutputInRangeACES) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 1);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

TEST_F(ToneMapAndroidTest, OutputInRangeUncharted2) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 2);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

// 2. Reinhard expected values
TEST_F(ToneMapAndroidTest, ReinhardExpectedValues) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 0);
    EXPECT_EQ(sdr_[0], 51)  << "R: reinhard(0.25) * 255";
    EXPECT_EQ(sdr_[1], 85)  << "G: reinhard(0.50) * 255";
    EXPECT_EQ(sdr_[2], 109) << "B: reinhard(0.75) * 255";
    EXPECT_EQ(sdr_[3], 255) << "A: always 255";
}

// 3. Exposure zero -> all black
TEST_F(ToneMapAndroidTest, ExposureZeroAllBlack) {
    toneMapImage(hdr_, sdr_, kW, kH, 0.0f, 1.0f, 0);
    for (int i = 0; i < kN; i++) {
        EXPECT_EQ(sdr_[i*4 + 0], 0);
        EXPECT_EQ(sdr_[i*4 + 1], 0);
        EXPECT_EQ(sdr_[i*4 + 2], 0);
        EXPECT_EQ(sdr_[i*4 + 3], 255);
    }
}

// 4. Different modes produce different outputs
TEST_F(ToneMapAndroidTest, DifferentModesDiffer) {
    uint8_t out0[kN * 4], out1[kN * 4], out2[kN * 4];
    toneMapImage(hdr_, out0, kW, kH, 1.0f, 1.0f, 0);
    toneMapImage(hdr_, out1, kW, kH, 1.0f, 1.0f, 1);
    toneMapImage(hdr_, out2, kW, kH, 1.0f, 1.0f, 2);
    bool r_vs_a = false, r_vs_u = false, a_vs_u = false;
    for (int i = 0; i < kN * 4; i++) {
        if (out0[i] != out1[i]) r_vs_a = true;
        if (out0[i] != out2[i]) r_vs_u = true;
        if (out1[i] != out2[i]) a_vs_u = true;
    }
    EXPECT_TRUE(r_vs_a) << "Reinhard vs ACES must differ";
    EXPECT_TRUE(r_vs_u) << "Reinhard vs Uncharted2 must differ";
    EXPECT_TRUE(a_vs_u) << "ACES vs Uncharted2 must differ";
}

// 5. Gamma effect
TEST_F(ToneMapAndroidTest, GammaChangesOutput) {
    uint8_t out_g1[kN * 4], out_g22[kN * 4];
    toneMapImage(hdr_, out_g1,  kW, kH, 1.0f, 1.0f, 0);
    toneMapImage(hdr_, out_g22, kW, kH, 1.0f, 2.2f, 0);
    bool differs = false;
    for (int i = 0; i < kN * 4; i++) {
        if (out_g1[i] != out_g22[i]) { differs = true; break; }
    }
    EXPECT_TRUE(differs) << "Gamma=2.2 must produce different output from gamma=1.0";
}

// 6. Regression match baseline
TEST_F(ToneMapAndroidTest, RegressionMatchBaseline) {
    uint8_t out[kN * 4];
    toneMapImage(hdr_, out, kW, kH, 1.0f, 1.0f, 0);
    namespace fs = std::filesystem;
    fs::path dir = fs::current_path();
    fs::path baselinePath;
    for (int i = 0; i < 6; ++i) {
        fs::path candidate = dir / "tests" / "baseline" / "android_reinhard.bin";
        std::error_code ec;
        if (fs::exists(candidate, ec)) { baselinePath = candidate; break; }
        if (!dir.has_parent_path()) break;
        dir = dir.parent_path();
    }
    std::ifstream baseline(baselinePath, std::ios::binary);
    ASSERT_TRUE(baseline.is_open()) << "Baseline file not found";
    uint8_t expected[kN * 4];
    baseline.read(reinterpret_cast<char*>(expected), kN * 4);
    baseline.close();
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_EQ(out[i], expected[i]) << "Byte mismatch at index " << i;
    }
}
