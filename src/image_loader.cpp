#include "image_loader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include "ultrahdr_api.h"
#include "stb_image.h"
#include "exif_parser.h"

ImageData::~ImageData() {
    if (pixels) { delete[] pixels; pixels = nullptr; }
}

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
    if(!file.is_open()) { fprintf(stderr,"Cannot open: %s\n",filePath.c_str()); return false; }
    auto sz=file.tellg();
    if(sz < 100) { fprintf(stderr,"File too small: %s (%lld bytes)\n",filePath.c_str(),(long long)sz); return false; }
    file.seekg(0,std::ios::beg);
    std::vector<uint8_t> data((size_t)sz);
    if(!file.read((char*)data.data(), sz)) { fprintf(stderr,"Read failed: %s\n",filePath.c_str()); return false; }
    file.close();

    if (outExif) *outExif = parseExif(data.data(), data.size());

    if(!is_uhdr_image(data.data(), (int)data.size())) {
        fprintf(stderr,"Not Ultra HDR: %s\n", filePath.c_str());
        return false;
    }

    fprintf(stdout,"Decoding: %s\n", filePath.c_str());
    uhdr_codec_private_t* dec=uhdr_create_decoder();
    if(!dec) return false;

    uhdr_compressed_image_t img={};
    img.data=data.data(); img.data_sz=data.size(); img.capacity=data.size();

    auto err=uhdr_dec_set_image(dec, &img);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) fprintf(stderr,"set_image: %s\n",err.detail);
        uhdr_release_decoder(dec); return false;
    }

    // Always request RGBA8888 output - let libultrahdr handle the conversion
    err = uhdr_dec_set_out_img_format(dec, UHDR_IMG_FMT_32bppRGBA8888);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) fprintf(stderr,"set_out_format: %s\n",err.detail);
        uhdr_release_decoder(dec); return false;
    }
    uhdr_dec_set_out_color_transfer(dec, UHDR_CT_SRGB);

    err=uhdr_decode(dec);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) fprintf(stderr,"decode: %s\n",err.detail);
        uhdr_release_decoder(dec); return false;
    }

    uhdr_raw_image_t* decoded=uhdr_get_decoded_image(dec);
    if(!decoded) { uhdr_release_decoder(dec); return false; }

    int w=(int)decoded->w, h=(int)decoded->h;
    fprintf(stdout,"Decoded: %dx%d stride=%zu fmt=%d\n", w, h,
            decoded->stride[UHDR_PLANE_PACKED], (int)decoded->fmt);

    uint8_t* src=(uint8_t*)decoded->planes[UHDR_PLANE_PACKED];
    size_t srcStridePx=decoded->stride[UHDR_PLANE_PACKED];

    // Allocate output buffers BEFORE releasing decoder
    auto hdr=std::make_unique<HDRData>();
    hdr->width=w; hdr->height=h;
    hdr->pixels.resize((size_t)w*h*4);

    outSDR.width=w; outSDR.height=h; outSDR.channels=4; outSDR.filePath=filePath;
    outSDR.pixels=new uint8_t[(size_t)w*h*4];

    // Copy pixel data row by row (handles stride padding)
    for(int y=0; y<h; y++) {
        uint8_t* dstRow=outSDR.pixels+(size_t)y*w*4;
        float* hdrRow=hdr->pixels.data()+(size_t)y*w*4;
        uint8_t* srcRow=src+(size_t)y*srcStridePx*4;
        for(int x=0; x<w; x++) {
            dstRow[x*4+0]=srcRow[x*4+0];
            dstRow[x*4+1]=srcRow[x*4+1];
            dstRow[x*4+2]=srcRow[x*4+2];
            dstRow[x*4+3]=srcRow[x*4+3];
            hdrRow[x*4+0]=srcRow[x*4+0]/255.0f;
            hdrRow[x*4+1]=srcRow[x*4+1]/255.0f;
            hdrRow[x*4+2]=srcRow[x*4+2]/255.0f;
            hdrRow[x*4+3]=1.0f;
        }
    }

    uhdr_release_decoder(dec);

    outHDR=std::move(hdr);
    fprintf(stdout,"OK: %dx%d\n", w, h);
    return true;
}

bool saveImageToFile(const ImageData& image, const std::string& path) {
    std::ofstream f(path,std::ios::binary); if(!f.is_open()) return false;
    f.write((const char*)image.pixels, image.dataSize()); f.close();
    return true;
}

// ============================================================
// Load regular image via stb_image
// ============================================================
bool loadRegularImage(const std::string& filePath, ImageData& outSDR, ExifData* outExif) {
    if (outExif) *outExif = ExifData{};

    std::ifstream file(filePath, std::ios::binary|std::ios::ate);
    if(!file.is_open()) { fprintf(stderr,"Cannot open: %s\n",filePath.c_str()); return false; }
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
        fprintf(stderr, "stbi_load failed: %s (%s)\n", filePath.c_str(), stbi_failure_reason());
        return false;
    }

    outSDR.width = w;
    outSDR.height = h;
    outSDR.channels = 4;
    outSDR.filePath = filePath;
    outSDR.pixels = new uint8_t[(size_t)w * h * 4];
    memcpy(outSDR.pixels, imgdata, (size_t)w * h * 4);
    stbi_image_free(imgdata);

    fprintf(stdout, "Loaded regular image: %dx%d (%s)\n", w, h, filePath.c_str());
    return true;
}
