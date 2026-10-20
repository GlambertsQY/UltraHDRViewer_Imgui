#pragma once

#include <cmath>

// Color space conversion helpers for HDR output.
// Data convention: HDRData is linear BT.709 float RGBA where 1.0 = 203 nits (SDR white).
namespace ColorSpace {

inline constexpr float HDR_WHITE_NITS = 203.0f;    // HDRData value 1.0 == 203 nits
inline constexpr float SCRGB_WHITE_NITS = 80.0f;   // scRGB value 1.0 == 80 nits
inline constexpr float SCRGB_SCALE = HDR_WHITE_NITS / SCRGB_WHITE_NITS; // 2.5375
inline constexpr float PQ_MAX_NITS = 10000.0f;

inline float srgbToLinear(float c) {
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// BT.709 linear -> BT.2020 linear (ITU-R BT.2087), row-major, each row sums to 1
inline constexpr float BT709_TO_BT2020[3][3] = {
    {0.6274039f, 0.3292830f, 0.0433000f},
    {0.0690970f, 0.9195404f, 0.0113600f},
    {0.0163914f, 0.0880132f, 0.8956000f},
};

inline void bt709ToBt2020(float r, float g, float b, float& outR, float& outG, float& outB) {
    outR = BT709_TO_BT2020[0][0] * r + BT709_TO_BT2020[0][1] * g + BT709_TO_BT2020[0][2] * b;
    outG = BT709_TO_BT2020[1][0] * r + BT709_TO_BT2020[1][1] * g + BT709_TO_BT2020[1][2] * b;
    outB = BT709_TO_BT2020[2][0] * r + BT709_TO_BT2020[2][1] * g + BT709_TO_BT2020[2][2] * b;
}

// ST2084 (PQ) inverse EOTF: absolute nits -> [0,1] signal
inline float pqEncode(float nits) {
    float y = nits / PQ_MAX_NITS;
    if (y <= 0.0f) return 0.0f;
    if (y >= 1.0f) return 1.0f;
    float ym = std::pow(y, 0.1593017578125f);              // m1 = 2610/16384
    return std::pow((0.8359375f + 18.8515625f * ym) /
                    (1.0f + 18.6875f * ym), 78.84375f);    // c1..c3, m2
}

// ST2084 (PQ) EOTF: [0,1] signal -> absolute nits
inline float pqDecode(float e) {
    if (e <= 0.0f) return 0.0f;
    if (e >= 1.0f) return PQ_MAX_NITS;
    float em = std::pow(e, 1.0f / 78.84375f);
    float num = em - 0.8359375f;
    if (num <= 0.0f) return 0.0f;
    float y = std::pow(num / (18.8515625f - 18.6875f * em), 1.0f / 0.1593017578125f);
    return y * PQ_MAX_NITS;
}

} // namespace ColorSpace
