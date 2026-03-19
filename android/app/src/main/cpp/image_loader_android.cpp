#include "hdr_types.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <jni.h>
#include "ultrahdr_api.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

ImageData::~ImageData() {
    if (pixels) { delete[] pixels; pixels = nullptr; }
}

static float reinhard(float x) { return x / (1.0f + x); }

static float acesFilmic(float x) {
    float a=2.51f, b=0.03f, c=2.43f, d=0.59f, e=0.14f;
    float v=(x*(a*x+b))/(x*(c*x+d)+e);
    return v<0?0:(v>1?1:v);
}

static float uncharted2Curve(float x) {
    float A=0.15f, B=0.50f, C=0.10f, D=0.20f, E=0.02f, F=0.30f, W=11.2f;
    float curr=((x*2*(A*x*2+C*B)+D*E)/(x*2*(A*x*2+B)+D*F))-E/F;
    float white=((W*2*(A*W*2+C*B)+D*E)/(W*2*(A*W*2+B)+D*F))-E/F;
    return curr/white;
}

void toneMapImage(const float* hdr, uint8_t* sdr, int w, int h,
                  float exposure, float gamma, int mode) {
    float invG = 1.0f/gamma;
    for (int i=0; i<w*h; i++) {
        float r=hdr[i*4+0]*exposure, g=hdr[i*4+1]*exposure, b=hdr[i*4+2]*exposure;
        switch(mode) {
            case 0: r=reinhard(r); g=reinhard(g); b=reinhard(b); break;
            case 1: r=acesFilmic(r); g=acesFilmic(g); b=acesFilmic(b); break;
            case 2: r=uncharted2Curve(r); g=uncharted2Curve(g); b=uncharted2Curve(b); break;
        }
        if(r<0)r=0; if(g<0)g=0; if(b<0)b=0;
        r=std::pow(r, invG); g=std::pow(g, invG); b=std::pow(b, invG);
        int ri=(int)(r*255+0.5f); if(ri>255)ri=255;
        int gi=(int)(g*255+0.5f); if(gi>255)gi=255;
        int bi=(int)(b*255+0.5f); if(bi>255)bi=255;
        sdr[i*4+0]=(uint8_t)ri; sdr[i*4+1]=(uint8_t)gi; sdr[i*4+2]=(uint8_t)bi; sdr[i*4+3]=255;
    }
}

void applyToneMap(const HDRData& hdr, ImageData& sdr,
                  float exposure, float gamma, int mode) {
    sdr.width=hdr.width; sdr.height=hdr.height; sdr.channels=4;
    if(sdr.pixels) delete[] sdr.pixels;
    sdr.pixels=new uint8_t[(size_t)hdr.width*hdr.height*4];
    toneMapImage(hdr.pixels.data(), sdr.pixels, hdr.width, hdr.height, exposure, gamma, mode);
}

bool isUltraHDRFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    unsigned char h[2]={}; f.read((char*)h,2); f.close();
    return h[0]==0xFF && h[1]==0xD8;
}

bool loadImageFile(const std::string& filePath, ImageData& outSDR,
                   std::unique_ptr<HDRData>& outHDR, ExifData* outExif) {
    outHDR.reset();
    if (outExif) *outExif = ExifData{};

    std::ifstream file(filePath, std::ios::binary|std::ios::ate);
    if(!file.is_open()) { fprintf(stderr,"Cannot open: %s\n",filePath.c_str()); return false; }
    auto sz=file.tellg();
    if(sz < 100) { fprintf(stderr,"File too small: %s\n",filePath.c_str()); return false; }
    file.seekg(0,std::ios::beg);
    std::vector<uint8_t> data((size_t)sz);
    if(!file.read((char*)data.data(), sz)) { fprintf(stderr,"Read failed: %s\n",filePath.c_str()); return false; }
    file.close();

    if (outExif) *outExif = parseExif(data.data(), data.size());

    uhdr_codec_private_t* dec=uhdr_create_decoder();
    if(!dec) return false;

    uhdr_compressed_image_t img={};
    img.data=data.data(); img.data_sz=data.size(); img.capacity=data.size();

    auto err=uhdr_dec_set_image(dec, &img);
    if(err.error_code!=UHDR_CODEC_OK) {
        if(err.has_detail) fprintf(stderr,"set_image: %s\n",err.detail);
        uhdr_release_decoder(dec); return false;
    }

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

    uint8_t* src=(uint8_t*)decoded->planes[UHDR_PLANE_PACKED];
    size_t srcStridePx=decoded->stride[UHDR_PLANE_PACKED];

    auto hdr=std::make_unique<HDRData>();
    hdr->width=w; hdr->height=h;
    hdr->pixels.resize((size_t)w*h*4);

    outSDR.width=w; outSDR.height=h; outSDR.channels=4; outSDR.filePath=filePath;
    outSDR.pixels=new uint8_t[(size_t)w*h*4];

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
    return true;
}

bool saveImageToFile(const ImageData& image, const std::string& path) {
    (void)image; (void)path;
    return false;
}

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
    unsigned char* imgdata = stbi_load_from_memory(filedata.data(), (int)filedata.size(), &w, &h, &channels, 4);
    if (!imgdata || w <= 0 || h <= 0) {
        fprintf(stderr, "stbi_load failed: %s\n", filePath.c_str());
        return false;
    }

    outSDR.width = w;
    outSDR.height = h;
    outSDR.channels = 4;
    outSDR.filePath = filePath;
    outSDR.pixels = new uint8_t[(size_t)w * h * 4];
    memcpy(outSDR.pixels, imgdata, (size_t)w * h * 4);
    stbi_image_free(imgdata);

    return true;
}
