#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "image_loader.h"

// ── Test fixture ─────────────────────────────────────────────────────── ───────────────────────────────────────────────────────

class ToneMapTest : public ::testing::Test {
protected:
    static constexpr int kW = 4, kH = 4, kN = kW * kH;  // 16 pixels

    // Build the 4×4 RGBA float image from the task spec: {0.25,0.5,0.75,1.0, ...}
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

// ── 1. Output bounds [0, 255] for all three modes ──────────────────────

TEST_F(ToneMapTest, OutputInRangeReinhard) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 0);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

TEST_F(ToneMapTest, OutputInRangeACES) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 1);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

TEST_F(ToneMapTest, OutputInRangeUncharted2) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 2);
    for (int i = 0; i < kN * 4; i++) {
        EXPECT_GE(sdr_[i], 0);
        EXPECT_LE(sdr_[i], 255);
    }
}

// ── 2. Reinhard identity-like test (exposure=1.0, gamma=1.0) ──────────
// Expected: reinhard(0.25)=0.2→51, reinhard(0.5)=1/3→85,
//           reinhard(0.75)=3/7→109, alpha always 255

TEST_F(ToneMapTest, ReinhardExpectedValues) {
    toneMapImage(hdr_, sdr_, kW, kH, 1.0f, 1.0f, 0);
    // All 16 pixels have identical RGB values; check the first pixel
    EXPECT_EQ(sdr_[0], 51)   << "R: reinhard(0.25) * 255";
    EXPECT_EQ(sdr_[1], 85)   << "G: reinhard(0.50) * 255";
    EXPECT_EQ(sdr_[2], 109)  << "B: reinhard(0.75) * 255";
    EXPECT_EQ(sdr_[3], 255)  << "A: always 255";
}

// ── 3. Exposure zero → all black ───────────────────────────────────────

TEST_F(ToneMapTest, ExposureZeroAllBlack) {
    toneMapImage(hdr_, sdr_, kW, kH, 0.0f, 1.0f, 0);
    for (int i = 0; i < kN; i++) {
        EXPECT_EQ(sdr_[i*4 + 0], 0)   << "R at pixel " << i << " should be 0";
        EXPECT_EQ(sdr_[i*4 + 1], 0)   << "G at pixel " << i << " should be 0";
        EXPECT_EQ(sdr_[i*4 + 2], 0)   << "B at pixel " << i << " should be 0";
        EXPECT_EQ(sdr_[i*4 + 3], 255) << "A at pixel " << i << " always 255";
    }
}

// ── 4. Different modes produce different outputs ───────────────────────

TEST_F(ToneMapTest, DifferentModesDiffer) {
    uint8_t out0[kN * 4], out1[kN * 4], out2[kN * 4];
    toneMapImage(hdr_, out0, kW, kH, 1.0f, 1.0f, 0);  // Reinhard
    toneMapImage(hdr_, out1, kW, kH, 1.0f, 1.0f, 1);  // ACES
    toneMapImage(hdr_, out2, kW, kH, 1.0f, 1.0f, 2);  // Uncharted 2

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

// ── 5. Gamma effect: gamma=2.2 changes output vs gamma=1.0 ────────────

TEST_F(ToneMapTest, GammaChangesOutput) {
    uint8_t out_g1[kN * 4], out_g22[kN * 4];
    toneMapImage(hdr_, out_g1,  kW, kH, 1.0f, 1.0f, 0);
    toneMapImage(hdr_, out_g22, kW, kH, 1.0f, 2.2f, 0);

    bool differs = false;
    for (int i = 0; i < kN * 4; i++) {
        if (out_g1[i] != out_g22[i]) { differs = true; break; }
    }
    EXPECT_TRUE(differs) << "Gamma=2.2 must produce different output from gamma=1.0";
}
