// hdr_roundtrip_test.cpp — Regression test for real HDR decode.
//
// Encodes a synthetic linear half-float HDR image (values > 1.0, i.e. beyond
// SDR white) into an Ultra HDR JPEG via libultrahdr, then verifies that
// loadImageFile() recovers true HDR data with highlight values > 1.0.
//
// Bug background: the old loader decoded with UHDR_CT_SRGB + RGBA8888, which
// returns only the SDR rendition; HDRData was faked as SDR/255 (max == 1.0).
// This test fails on the old code (max <= 1.0) and passes on the fixed code.

#include <gtest/gtest.h>
#include "image_loader.h"
#include "renderer.h"
#include "half_float.h"
#include "ultrahdr_api.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

TEST(HdrRoundTripTest, DecodeProducesRealHdrData) {
    const int W = 256, H = 64;
    std::vector<uint16_t> hf((size_t)W * H * 4);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // Horizontal ramp from 0.25 to 8.0 (8x SDR white)
            float v = 0.25f + 7.75f * (float)x / (float)(W - 1);
            size_t i = ((size_t)y * W + x) * 4;
            hf[i + 0] = floatToHalf(v);
            hf[i + 1] = floatToHalf(v * 0.5f);
            hf[i + 2] = floatToHalf(v * 0.25f);
            hf[i + 3] = floatToHalf(1.0f);
        }
    }

    uhdr_raw_image_t raw{};
    raw.fmt = UHDR_IMG_FMT_64bppRGBAHalfFloat;
    raw.cg = UHDR_CG_BT_709;
    raw.ct = UHDR_CT_LINEAR;
    raw.range = UHDR_CR_FULL_RANGE;
    raw.w = W;
    raw.h = H;
    raw.planes[UHDR_PLANE_PACKED] = hf.data();
    raw.stride[UHDR_PLANE_PACKED] = W;

    uhdr_codec_private_t* enc = uhdr_create_encoder();
    ASSERT_NE(enc, nullptr);
    ASSERT_EQ(uhdr_enc_set_raw_image(enc, &raw, UHDR_HDR_IMG).error_code, UHDR_CODEC_OK);
    ASSERT_EQ(uhdr_enc_set_quality(enc, 95, UHDR_BASE_IMG).error_code, UHDR_CODEC_OK);
    ASSERT_EQ(uhdr_encode(enc).error_code, UHDR_CODEC_OK);

    uhdr_compressed_image_t* stream = uhdr_get_encoded_stream(enc);
    ASSERT_NE(stream, nullptr);
    ASSERT_GT(stream->data_sz, 0u);

    const char* path = "synthetic_uhdr_test.jpg";
    {
        std::ofstream f(path, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f.write(reinterpret_cast<const char*>(stream->data), (std::streamsize)stream->data_sz);
    }
    uhdr_release_encoder(enc);

    // Decode through the app loader
    ImageData img;
    std::unique_ptr<HDRData> hdr;
    ASSERT_TRUE(loadImageFile(path, img, hdr));
    ASSERT_NE(hdr, nullptr);
    EXPECT_EQ(hdr->width, W);
    EXPECT_EQ(hdr->height, H);

    // True HDR: highlights must exceed SDR white (1.0).
    // Old buggy code produced max == 255/255 == 1.0.
    float maxV = 0.0f;
    for (float v : hdr->pixels) maxV = std::max(maxV, v);
    EXPECT_GT(maxV, 2.0f);

    // Dark end stays near SDR range
    EXPECT_LT(hdr->pixels[0], 0.5f);

    // Display SDR was tone-mapped from HDR: highlight end must be compressed
    // below clipping (Reinhard(8.0) -> ~242, not clipped 255)
    ASSERT_FALSE(img.pixels.empty());
    size_t lastPx = ((size_t)(H / 2) * img.width + (img.width - 1)) * 4;
    EXPECT_GT(img.pixels[lastPx], 200);
    EXPECT_LT(img.pixels[lastPx], 255);

    // The generated JPEG is kept as a reusable Ultra HDR sample asset
    // (run the test binary from the repo root to place it there).
}
