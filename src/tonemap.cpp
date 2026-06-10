#include "tonemap.h"

#include <cmath>
#include <algorithm>
#include "renderer.h"
#include "image_loader.h"

// ============================================================
// Tone mapping
// ============================================================
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
    sdr.pixels.resize((size_t)hdr.width*hdr.height*4);
    toneMapImage(hdr.pixels.data(), sdr.pixels.data(), hdr.width, hdr.height, exposure, gamma, mode);
}
