#include "hdr_types.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

static uint16_t readU16(const uint8_t* p, bool be) {
    return be ? (uint16_t(p[0]) << 8) | p[1] : (uint16_t(p[1]) << 8) | p[0];
}
static uint32_t readU32(const uint8_t* p, bool be) {
    return be ? (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]
               : (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
}
static int32_t readS32(const uint8_t* p, bool be) {
    return static_cast<int32_t>(readU32(p, be));
}
static double readURational(const uint8_t* p, bool be) {
    uint32_t num = readU32(p, be);
    uint32_t den = readU32(p + 4, be);
    return (den == 0) ? 0.0 : (double)num / (double)den;
}
static double readSRational(const uint8_t* p, bool be) {
    int32_t num = readS32(p, be);
    int32_t den = readS32(p + 4, be);
    return (den == 0) ? 0.0 : (double)num / (double)den;
}

enum ExifTag {
    TAG_MAKE=0x010F, TAG_MODEL=0x0110, TAG_ORIENTATION=0x0112,
    TAG_SOFTWARE=0x0131, TAG_DATETIME=0x0132, TAG_PIXEL_X=0xA002,
    TAG_PIXEL_Y=0xA003, TAG_EXPOSURE_TIME=0x829A, TAG_FNUMBER=0x829D,
    TAG_ISO_SPEED=0x8827, TAG_FOCAL_LENGTH=0x920A, TAG_LENS_MAKE=0xA433,
    TAG_LENS_MODEL=0xA434, TAG_FLASH=0x9209, TAG_WHITE_BALANCE=0xA403,
    TAG_GPS_LAT=0x0002, TAG_GPS_LAT_REF=0x0001, TAG_GPS_LONG=0x0004,
    TAG_GPS_LONG_REF=0x0003, TAG_GPS_ALT=0x0006, TAG_GPS_ALT_REF=0x0005,
    TAG_EXIF_IFD=0x8769, TAG_GPS_IFD=0x8825,
};
enum TiffType { TIFF_BYTE=1, TIFF_ASCII=2, TIFF_SHORT=3, TIFF_LONG=4,
    TIFF_RATIONAL=5, TIFF_SBYTE=6, TIFF_UNDEFINED=7, TIFF_SSHORT=8,
    TIFF_SLONG=9, TIFF_SRATIONAL=10, TIFF_FLOAT=11, TIFF_DOUBLE=12 };

struct ExifEntry {
    uint16_t tag = 0, type = 0;
    uint32_t count = 0, value_offset = 0;
    const uint8_t* base = nullptr;
    bool bigendian = false;
    uint32_t getU32(size_t idx = 0) const {
        if (type == TIFF_LONG) return (count == 1) ? value_offset : readU32(base + value_offset + idx * 4, bigendian);
        if (type == TIFF_SHORT) return (count == 1) ? (value_offset >> (bigendian ? 16 : 0)) & 0xFFFF : readU16(base + value_offset + idx * 2, bigendian);
        return 0;
    }
    double getDouble(size_t idx = 0) const {
        if (type == TIFF_RATIONAL) return (count == 1) ? readURational(base + value_offset, bigendian) : readURational(base + value_offset + idx * 8, bigendian);
        if (type == TIFF_SRATIONAL) return (count == 1) ? readSRational(base + value_offset, bigendian) : readSRational(base + value_offset + idx * 8, bigendian);
        return 0.0;
    }
    std::string getString() const {
        if (type != TIFF_ASCII) return "";
        uint32_t off = (count > 4) ? value_offset : 0;
        std::string s;
        const char* p = (const char*)(base + off);
        size_t maxLen = (count > 4) ? count : 4;
        for (size_t i = 0; i < maxLen && p[i]; i++) s += p[i];
        return s;
    }
};

static bool parseIFD(const uint8_t* base, uint32_t offset, uint32_t maxSize, bool be, ExifData& out, bool isGPS = false) {
    if (offset + 2 > maxSize) return false;
    uint16_t num = readU16(base + offset, be);
    uint32_t pos = offset + 2;
    if (pos + num * 12 > maxSize) return false;
    for (uint16_t i = 0; i < num; i++) {
        const uint8_t* entry = base + pos + i * 12;
        ExifEntry e;
        e.tag = readU16(entry + 0, be);
        e.type = readU16(entry + 2, be);
        e.count = readU32(entry + 4, be);
        e.value_offset = readU32(entry + 8, be);
        e.base = base; e.bigendian = be;
        switch (e.tag) {
            case TAG_MAKE: out.make = e.getString(); break;
            case TAG_MODEL: out.model = e.getString(); break;
            case TAG_SOFTWARE: out.software = e.getString(); break;
            case TAG_DATETIME: out.dateTime = e.getString(); break;
            case TAG_ORIENTATION: out.orientation = e.getU32(); break;
            case TAG_PIXEL_X: out.pixelX = e.getU32(); break;
            case TAG_PIXEL_Y: out.pixelY = e.getU32(); break;
            case TAG_EXPOSURE_TIME: out.exposureTime = e.getDouble(); break;
            case TAG_FNUMBER: out.fNumber = e.getDouble(); break;
            case TAG_ISO_SPEED: out.isoSpeed = e.getU32(); break;
            case TAG_FOCAL_LENGTH: out.focalLength = e.getDouble(); break;
            case TAG_LENS_MAKE: out.lensMake = e.getString(); break;
            case TAG_LENS_MODEL: out.lensModel = e.getString(); break;
            case TAG_FLASH: out.flash = e.getU32(); break;
            case TAG_WHITE_BALANCE: out.whiteBalance = e.getU32(); break;
            case TAG_EXIF_IFD: { uint32_t exifOff = e.getU32(); if (exifOff < maxSize) parseIFD(base, exifOff, maxSize, be, out, false); break; }
            case TAG_GPS_IFD: { uint32_t gpsOff = e.getU32(); if (gpsOff < maxSize) parseIFD(base, gpsOff, maxSize, be, out, true); break; }
            case TAG_GPS_LAT: if (e.count >= 3) { out.gpsLatitude = e.getDouble(0) + e.getDouble(1)/60.0 + e.getDouble(2)/3600.0; } break;
            case TAG_GPS_LAT_REF: { std::string ref = e.getString(); if (!ref.empty() && (ref[0]=='S' || ref[0]=='s')) out.gpsLatitude = -std::fabs(out.gpsLatitude); } break;
            case TAG_GPS_LONG: if (e.count >= 3) { out.gpsLongitude = e.getDouble(0) + e.getDouble(1)/60.0 + e.getDouble(2)/3600.0; } break;
            case TAG_GPS_LONG_REF: { std::string ref = e.getString(); if (!ref.empty() && (ref[0]=='W' || ref[0]=='w')) out.gpsLongitude = -std::fabs(out.gpsLongitude); } break;
            case TAG_GPS_ALT: out.gpsAltitude = e.getDouble(0); break;
            case TAG_GPS_ALT_REF: if (e.getU32() & 0xFF) out.gpsAltitude = -std::fabs(out.gpsAltitude); break;
        }
    }
    uint32_t nextIFD = readU32(base + pos + num * 12, be);
    if (nextIFD != 0 && nextIFD < maxSize) parseIFD(base, nextIFD, maxSize, be, out, isGPS);
    return true;
}

ExifData parseExif(const uint8_t* data, size_t size) {
    ExifData out;
    if (!data || size < 4) return out;
    for (size_t i = 0; i + 1 < size; i++) {
        if (data[i] != 0xFF || data[i+1] != 0xE1) continue;
        size_t segmentLen = (size_t)data[i+2] << 8 | data[i+3];
        if (i + 4 + segmentLen > size || segmentLen < 6) break;
        size_t app1Start = i + 4;
        const char* exifSig = "\x45\x78\x69\x66\x00\x00";
        if (std::memcmp(data + app1Start, exifSig, 6) != 0) break;
        const uint8_t* tiff = data + app1Start + 6;
        size_t tiffSize = segmentLen - 6;
        if (tiffSize < 8) break;
        bool be = (tiff[0] == 0x4D && tiff[1] == 0x4D);
        bool le = (tiff[0] == 0x49 && tiff[1] == 0x49);
        if (!be && !le) break;
        uint16_t magic = readU16(tiff + 2, be);
        if (magic != 42) break;
        uint32_t ifd0Off = readU32(tiff + 4, be);
        if (ifd0Off + 4 > tiffSize) break;
        out.valid = true;
        parseIFD(tiff, ifd0Off, (uint32_t)tiffSize, be, out, false);
        if (out.gpsLatitude != 0.0 || out.gpsLongitude != 0.0) out.hasGPS = true;
        break;
    }
    return out;
}

std::string ExifData::exposureStr() const {
    if (exposureTime <= 0) return "Unknown";
    char buf[32];
    if (exposureTime >= 1.0) { snprintf(buf, sizeof(buf), "%.1f s", exposureTime); }
    else { snprintf(buf, sizeof(buf), "1/%.0f s", 1.0 / exposureTime); }
    return std::string(buf);
}
std::string ExifData::apertureStr() const {
    if (fNumber <= 0) return "Unknown";
    char buf[32]; snprintf(buf, sizeof(buf), "f/%.1f", fNumber); return std::string(buf);
}
std::string ExifData::focalLengthStr() const {
    if (focalLength <= 0) return "Unknown";
    char buf[32]; snprintf(buf, sizeof(buf), "%.0f mm", focalLength); return std::string(buf);
}
std::string ExifData::orientationStr() const {
    switch (orientation) {
        case 1: return "Normal";
        case 2: return "Flipped H";
        case 3: return "180";
        case 4: return "Flipped V";
        case 5: return "Transposed";
        case 6: return "90 CW";
        case 7: return "Transverse";
        case 8: return "90 CCW";
        default: return "Unknown";
    }
}
