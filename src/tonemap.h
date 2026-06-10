#pragma once
#include <cstdint>

struct HDRData;
struct ImageData;

// Tone mapping: HDR float RGBA -> SDR uint8 RGBA
// mode: 0=Reinhard, 1=ACES Filmic, 2=Uncharted 2
void toneMapImage(const float* hdrPixels, uint8_t* sdrPixels, int width, int height,
                  float exposure, float gamma, int toneMappingMode);

// Apply tone mapping from HDRData -> ImageData (reallocates ImageData::pixels)
void applyToneMap(const HDRData& hdr, ImageData& sdr,
                  float exposure, float gamma, int toneMappingMode);
