#include "image_loader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include "log.h"
#include "config.h"
#include "half_float.h"
#include "ultrahdr_api.h"
#include "stb_image.h"
#include "exif_parser.h"

// ============================================================
bool isUltraHDRFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    unsigned char h[2]={}; f.read((char*)h,2); f.close();
    return h[0]==0xFF && h[1]==0xD8;
}

// ============================================================
bool loadImageFile(const std::string& filePath, ImageData& outSDR,
                   std::unique_ptr<HDRData>& outHDR, ExifData* outExif) {
    outHDR.reset();
    if (outExif) *outExif = ExifData{};

    std::ifstream file(filePath, std::ios::binary|std::ios::ate);
    if(!file.is_open()) { LOG_ERROR("Cannot open: %s", filePath.c_str()); return false; }
    auto sz=file.tellg();
    if(sz < 100) { LOG_ERROR("File too small: %s (%lld bytes)", filePath.c_str(), (long long)sz); return false; }
    file.seekg(0,std::ios::beg);
    std::vector<uint8_t> data((size_t)sz);
    if(!file.read((char*)data.data(), sz)) { LOG_ERROR("Read failed: %s", filePath.c_str()); return false; }
    file.close();

    if (outExif) *outExif = parseExif(data.data(), data.size());

    if(!is_uhdr_image(data.data(), (int)data.size())) {
        LOG_ERROR("Not Ultra HDR: %s", filePath.c_str());
        return false;
    }

    LOG_INFO("Decoding: %s", filePath.c_str());
    uhdr_codec_private_t* dec=uhdr_create_decoder();
    if(!dec) return false;

    uhdr_compressed_image_t img={};
    img.data=data.data(); img.data_sz=data.size(); img.capacity=data.size();

    auto err=uhdr_dec_set_image(dec, &img);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) LOG_ERROR("set_image: %s", err.detail);
        uhdr_release_decoder(dec); return false;
    }

    // Request true HDR output: linear half-float, gain map applied at full boost.
    // (UHDR_CT_SRGB + RGBA8888 would return only the SDR rendition - no HDR data.)
    err = uhdr_dec_set_out_img_format(dec, UHDR_IMG_FMT_64bppRGBAHalfFloat);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) LOG_ERROR("set_out_format: %s", err.detail);
        uhdr_release_decoder(dec); return false;
    }
    err = uhdr_dec_set_out_color_transfer(dec, UHDR_CT_LINEAR);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) LOG_ERROR("set_out_ct: %s", err.detail);
        uhdr_release_decoder(dec); return false;
    }

    err=uhdr_decode(dec);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) LOG_ERROR("decode: %s", err.detail);
        uhdr_release_decoder(dec); return false;
    }

    uhdr_raw_image_t* decoded=uhdr_get_decoded_image(dec);
    if(!decoded) { uhdr_release_decoder(dec); return false; }

    int w=(int)decoded->w, h=(int)decoded->h;
    LOG_INFO("Decoded: %dx%d stride=%u fmt=%d", w, h,
             (unsigned)decoded->stride[UHDR_PLANE_PACKED], (int)decoded->fmt);

    if (decoded->fmt != UHDR_IMG_FMT_64bppRGBAHalfFloat) {
        LOG_ERROR("Unexpected output format: %d", (int)decoded->fmt);
        uhdr_release_decoder(dec); return false;
    }

    // Linear half-float RGBA: 1.0 = SDR white (203 nits), HDR highlights > 1.0
    const uint16_t* src=(const uint16_t*)decoded->planes[UHDR_PLANE_PACKED];
    size_t srcStridePx=decoded->stride[UHDR_PLANE_PACKED];

    // Allocate HDR buffer BEFORE releasing decoder
    auto hdr=std::make_unique<HDRData>();
    hdr->width=w; hdr->height=h;
    hdr->pixels.resize((size_t)w*h*4);

    // Convert half-float -> float row by row (handles stride padding)
    for(int y=0; y<h; y++) {
        float* hdrRow=hdr->pixels.data()+(size_t)y*w*4;
        const uint16_t* srcRow=src+(size_t)y*srcStridePx*4;
        for(int x=0; x<w*4; x++) {
            hdrRow[x]=halfToFloat(srcRow[x]);
        }
    }

    uhdr_release_decoder(dec);

    outHDR=std::move(hdr);

    // Generate display SDR by tone mapping the HDR data with default parameters
    applyToneMap(*outHDR, outSDR, Config::DEFAULT_EXPOSURE, Config::DEFAULT_GAMMA, 0);
    outSDR.filePath=filePath;

    LOG_INFO("OK: %dx%d", w, h);
    return true;
}

bool saveImageToFile(const ImageData& image, const std::string& path) {
    std::ofstream f(path,std::ios::binary); if(!f.is_open()) return false;
    f.write((const char*)image.pixels.data(), image.dataSize()); f.close();
    return true;
}

// ============================================================
// Load regular image via stb_image
// ============================================================
bool loadRegularImage(const std::string& filePath, ImageData& outSDR, ExifData* outExif) {
    if (outExif) *outExif = ExifData{};

    std::ifstream file(filePath, std::ios::binary|std::ios::ate);
    if(!file.is_open()) { LOG_ERROR("Cannot open: %s", filePath.c_str()); return false; }
    auto sz = file.tellg();
    if(sz < 4) return false;
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> filedata((size_t)sz);
    if(!file.read((char*)filedata.data(), sz)) return false;
    file.close();

    if (outExif) *outExif = parseExif(filedata.data(), filedata.size());

    int w=0, h=0, channels=0;
    unsigned char* imgdata = stbi_load(filePath.c_str(), &w, &h, &channels, 4);
    if (!imgdata || w <= 0 || h <= 0) {
        LOG_ERROR("stbi_load failed: %s (%s)", filePath.c_str(), stbi_failure_reason());
        return false;
    }

    outSDR.width = w;
    outSDR.height = h;
    outSDR.channels = 4;
    outSDR.filePath = filePath;
    outSDR.pixels.resize((size_t)w * h * 4);
    memcpy(outSDR.pixels.data(), imgdata, (size_t)w * h * 4);
    stbi_image_free(imgdata);

    LOG_INFO("Loaded regular image: %dx%d (%s)", w, h, filePath.c_str());
    return true;
}
