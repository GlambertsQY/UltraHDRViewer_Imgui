// baseline_gen.cpp — Standalone pixel baseline generator for tone mapping verification
//
// Generates 3 baseline .bin files from synthetic 4x4 RGBA float test data.
// Verifies that image_loader_android.cpp and native.cpp tone curves
// produce byte-identical output.
//
// Compile (MSVC): cl /EHsc /std:c++17 baseline_gen.cpp /Febaseline_gen.exe && .\baseline_gen.exe
// Compile (MinGW): g++ -std=c++17 baseline_gen.cpp -o baseline_gen.exe && .\baseline_gen.exe

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cstdint>

// ============================================================
// 1. Tone curves from image_loader_android.cpp (lines 16–29)
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

// ============================================================
// 2. Tone curves from native.cpp (lines 56–67)
//    — identical math, different function names / whitespace
// ============================================================

static float ringbuf(float x) { return x / (1.0f + x); }

static float acesFilm(float x) {
    float a=2.51f, b=0.03f, c=2.43f, d=0.59f, e=0.14f;
    float v = (x*(a*x+b)) / (x*(c*x+d)+e);
    return v<0?0:(v>1?1:v);
}

static float unch2(float x) {
    float A=0.15f, B=0.50f, C=0.10f, D=0.20f, E=0.02f, F=0.30f, W=11.2f;
    float curr = ((x*2*(A*x*2+C*B)+D*E)/(x*2*(A*x*2+B)+D*F))-E/F;
    float white = ((W*2*(A*W*2+C*B)+D*E)/(W*2*(A*W*2+B)+D*F))-E/F;
    return curr/white;
}

// ============================================================
// 3. toneMapImage — EXACT copy of image_loader_android.cpp lines 31–48
// ============================================================

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

// ============================================================
// 4. applyToneMap — native.cpp version (lines 69–98) using ringbuf/acesFilm/unch2
// ============================================================

void applyToneMap_Native(const float* hdr, uint8_t* sdr, int w, int h,
                         float exposure, float gamma, int mode) {
    float invG = 1.0f / gamma;
    for (int i = 0; i < w * h; i++) {
        float r = hdr[i*4+0] * exposure;
        float g = hdr[i*4+1] * exposure;
        float b = hdr[i*4+2] * exposure;
        switch (mode) {
            case 0: r=ringbuf(r); g=ringbuf(g); b=ringbuf(b); break;
            case 1: r=acesFilm(r); g=acesFilm(g); b=acesFilm(b); break;
            case 2: r=unch2(r); g=unch2(g); b=unch2(b); break;
        }
        if (r<0) r=0; if (g<0) g=0; if (b<0) b=0;
        r = std::pow(r, invG); g = std::pow(g, invG); b = std::pow(b, invG);
        int ri = (int)(r*255+0.5f); if (ri>255) ri=255;
        int gi = (int)(g*255+0.5f); if (gi>255) gi=255;
        int bi = (int)(b*255+0.5f); if (bi>255) bi=255;
        sdr[i*4+0] = (uint8_t)ri; sdr[i*4+1] = (uint8_t)gi;
        sdr[i*4+2] = (uint8_t)bi; sdr[i*4+3] = 255;
    }
}

// ============================================================
// 5. Helper: write RGBA uint8_t buffer to file
// ============================================================

bool writeBinaryFile(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s for writing\n", path); return false; }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    if (written != size) { fprintf(stderr, "ERROR: short write to %s\n", path); return false; }
    return true;
}

// ============================================================
// 6. Helpers: create test input, compare buffers
// ============================================================

void createTestInput(float* hdr, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        hdr[i*4+0] = 0.25f;
        hdr[i*4+1] = 0.50f;
        hdr[i*4+2] = 0.75f;
        hdr[i*4+3] = 1.0f;
    }
}

bool buffersIdentical(const uint8_t* a, const uint8_t* b, size_t size) {
    return memcmp(a, b, size) == 0;
}

void printBufferHex(const uint8_t* buf, size_t count, const char* label) {
    printf("  %s [%zu bytes]: ", label, count);
    for (size_t i = 0; i < count && i < 64; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

// ============================================================
// 7. Main
// ============================================================

int main() {
    const int W = 4, H = 4;
    const int NUM_PIXELS = W * H;         // 16
    const size_t FLOAT_SIZE = (size_t)NUM_PIXELS * 4 * sizeof(float);  // 256 bytes
    const size_t UINT8_SIZE = (size_t)NUM_PIXELS * 4;                  // 64 bytes (4x4x4)
    const size_t OUT_SIZE = (size_t)NUM_PIXELS * 4;                    // 64 bytes for uint8_t output

    // Allocate test input
    float* hdrInput = new float[(size_t)NUM_PIXELS * 4];
    createTestInput(hdrInput, W, H);

    // Allocate output buffers
    uint8_t* out_imgLoader = new uint8_t[OUT_SIZE];
    uint8_t* out_native = new uint8_t[OUT_SIZE];
    uint8_t* out_exp0 = new uint8_t[OUT_SIZE];

    const char* modeNames[] = {"Reinhard", "ACES Filmic", "Uncharted 2"};
    const char* modeFiles[] = {
        "tests/baseline/android_reinhard.bin",
        "tests/baseline/android_aces.bin",
        "tests/baseline/android_uncharted2.bin",
    };

    printf("=== UltraHDR Tone Mapping Baseline Generator ===\n");
    printf("Test image: %dx%d, R=0.25 G=0.50 B=0.75 A=1.0\n", W, H);
    printf("Output size per file: %zu bytes\n\n", OUT_SIZE);

    bool allOk = true;

    // ── Generate baseline for all 3 modes with exposure=1.0, gamma=1.0 ──
    for (int mode = 0; mode < 3; mode++) {
        printf("--- Mode %d: %s (exposure=1.0, gamma=1.0) ---\n", mode, modeNames[mode]);

        // Run image_loader_android.cpp version
        memset(out_imgLoader, 0, OUT_SIZE);
        toneMapImage(hdrInput, out_imgLoader, W, H, 1.0f, 1.0f, mode);

        // Run native.cpp version
        memset(out_native, 0, OUT_SIZE);
        applyToneMap_Native(hdrInput, out_native, W, H, 1.0f, 1.0f, mode);

        // Compare — they MUST be identical
        bool match = buffersIdentical(out_imgLoader, out_native, OUT_SIZE);
        if (match) {
            printf("  [PASS] image_loader_android.cpp vs native.cpp: BYTE-IDENTICAL\n");
        } else {
            printf("  [FAIL] image_loader_android.cpp vs native.cpp: MISMATCH!\n");
            printBufferHex(out_imgLoader, 32, "imgLoader");
            printBufferHex(out_native, 32, "native  ");
            allOk = false;
        }

        // Write baseline file (using image_loader_android.cpp output as canonical)
        if (!writeBinaryFile(modeFiles[mode], out_imgLoader, OUT_SIZE)) {
            allOk = false;
        } else {
            printf("  [SAVED] %s\n", modeFiles[mode]);
        }

        // Print first pixel for human inspection
        printf("  Pixel[0]: R=%3u G=%3u B=%3u A=%3u\n\n",
               out_imgLoader[0], out_imgLoader[1], out_imgLoader[2], out_imgLoader[3]);
    }

    // ── Test with exposure=0.0 → all RGB should be 0, A=255 ──
    printf("--- Exposure=0.0, gamma=1.0 (should produce all RGB=0) ---\n");
    memset(out_exp0, 0, OUT_SIZE);
    toneMapImage(hdrInput, out_exp0, W, H, 0.0f, 1.0f, 0); // Reinhard with exp=0

    bool exp0Ok = true;
    for (int i = 0; i < NUM_PIXELS; i++) {
        if (out_exp0[i*4+0] != 0 || out_exp0[i*4+1] != 0 || out_exp0[i*4+2] != 0) {
            printf("  [FAIL] Pixel[%d] R=%u G=%u B=%u (expected all 0)\n",
                   i, out_exp0[i*4+0], out_exp0[i*4+1], out_exp0[i*4+2]);
            exp0Ok = false;
        }
        if (out_exp0[i*4+3] != 255) {
            printf("  [FAIL] Pixel[%d] A=%u (expected 255)\n", i, out_exp0[i*4+3]);
            exp0Ok = false;
        }
    }
    if (exp0Ok) {
        printf("  [PASS] Exposure=0.0: all pixels R=G=B=0, A=255\n\n");
    } else {
        allOk = false;
    }

    // ── Cleanup ──
    delete[] hdrInput;
    delete[] out_imgLoader;
    delete[] out_native;
    delete[] out_exp0;

    // ── Final summary ──
    printf("=== %s ===\n", allOk ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return allOk ? 0 : 1;
}
