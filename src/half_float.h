#pragma once

#include <cstdint>
#include <cstring>

// IEEE 754 half-precision -> single-precision float conversion
inline float halfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;

    if (exp == 0) {
        // Subnormal half (or zero)
        float f = (float)mant / 1024.0f * 6.103515625e-05f; // 2^-14
        return sign ? -f : f;
    }
    uint32_t bits;
    if (exp == 31) {
        // Inf / NaN
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// IEEE 754 single-precision -> half-precision float (round to nearest even)
inline uint16_t floatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000;
    int exp = (int)((x >> 23) & 0xFF);
    uint32_t mant = x & 0x7FFFFF;

    if (exp == 255) {
        // Inf / NaN
        return (uint16_t)(sign | 0x7C00 | (mant ? 0x200u : 0u));
    }

    int hexp = exp - 127 + 15;
    if (hexp >= 31) return (uint16_t)(sign | 0x7C00);   // overflow -> inf
    if (hexp <= 0) {
        if (hexp < -10) return (uint16_t)sign;          // underflow -> 0
        // Subnormal half
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - hexp);         // [14, 24]
        uint32_t halfMant = mant >> shift;
        uint32_t remainder = mant & ((1u << shift) - 1u);
        uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (halfMant & 1u))) halfMant++;
        return (uint16_t)(sign | halfMant);
    }

    // Normalized half: round mantissa from 23 to 10 bits
    uint32_t halfMant = mant >> 13;
    uint32_t remainder = mant & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (halfMant & 1u))) {
        halfMant++;
        if (halfMant == 0x400u) {
            halfMant = 0;
            hexp++;
            if (hexp >= 31) return (uint16_t)(sign | 0x7C00);
        }
    }
    return (uint16_t)(sign | ((uint32_t)hexp << 10) | halfMant);
}
