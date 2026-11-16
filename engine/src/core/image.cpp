#include "valgo/core/image.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "valgo/core/file.h"

namespace valgo {
namespace {

// ---------------------------------------------------------------------------
// CRC32 (PNG polynomial) and Adler32 checksums.
// ---------------------------------------------------------------------------
struct Crc32Table {
    u32 entries[256];
    constexpr Crc32Table() : entries{} {
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (u32 k = 0; k < 8; ++k) {
                c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1u) : (c >> 1u);
            }
            entries[n] = c;
        }
    }
};

constexpr Crc32Table kCrcTable{};

u32 Crc32(const u8* data, usize size, u32 seed = 0xFFFFFFFFu) {
    u32 crc = seed;
    for (usize i = 0; i < size; ++i) {
        crc = kCrcTable.entries[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc;
}

u32 Adler32(const u8* data, usize size) {
    u32 a = 1;
    u32 b = 0;
    for (usize i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16u) | a;
}

void PushU32BE(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>((value >> 24u) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<u8>(value & 0xFFu));
}

void PushChunk(std::vector<u8>& out, const char type[5], const std::vector<u8>& payload) {
    PushU32BE(out, static_cast<u32>(payload.size()));
    const usize type_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    const u32 crc = Crc32(out.data() + type_start, out.size() - type_start) ^ 0xFFFFFFFFu;
    PushU32BE(out, crc);
}

/// Wraps raw bytes in a zlib stream made of *stored* deflate blocks.
std::vector<u8> ZlibStore(const std::vector<u8>& raw) {
    std::vector<u8> out;
    out.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    out.push_back(0x78);  // CM = 8, CINFO = 7
    out.push_back(0x01);  // FCHECK valid for (CMF<<8|FLG) % 31 == 0, no dict

    usize offset = 0;
    if (raw.empty()) {
        out.push_back(0x01);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0xFF);
        out.push_back(0xFF);
    }
    while (offset < raw.size()) {
        const usize block = std::min<usize>(raw.size() - offset, 65535u);
        const bool final_block = (offset + block) >= raw.size();
        out.push_back(final_block ? 0x01 : 0x00);
        const u16 len = static_cast<u16>(block);
        const u16 nlen = static_cast<u16>(~len);
        out.push_back(static_cast<u8>(len & 0xFFu));
        out.push_back(static_cast<u8>((len >> 8u) & 0xFFu));
        out.push_back(static_cast<u8>(nlen & 0xFFu));
        out.push_back(static_cast<u8>((nlen >> 8u) & 0xFFu));
        out.insert(out.end(), raw.begin() + static_cast<isize>(offset),
                   raw.begin() + static_cast<isize>(offset + block));
        offset += block;
    }
    PushU32BE(out, Adler32(raw.data(), raw.size()));
    return out;
}

}  // namespace

Image::Image(u32 w, u32 h, u8 r, u8 g, u8 b, u8 a) : width(w), height(h) {
    pixels.resize(static_cast<usize>(w) * static_cast<usize>(h) * 4u);
    for (usize i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
}

u8* Image::At(u32 x, u32 y) {
    return pixels.data() + (static_cast<usize>(y) * width + x) * 4u;
}

const u8* Image::At(u32 x, u32 y) const {
    return pixels.data() + (static_cast<usize>(y) * width + x) * 4u;
}

u64 Image::Checksum() const {
    // FNV-1a over the pixel data: stable across platforms, order dependent.
    u64 hash = 0xcbf29ce484222325ULL;
    for (u8 value : pixels) {
        hash ^= value;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::vector<u8> EncodePng(u32 width, u32 height, const u8* rgba) {
    std::vector<u8> png;
    if (width == 0 || height == 0 || rgba == nullptr) {
        return png;
    }

    static constexpr u8 kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), kSignature, kSignature + 8);

    // IHDR
    std::vector<u8> ihdr;
    PushU32BE(ihdr, width);
    PushU32BE(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter method
    ihdr.push_back(0);  // no interlace
    PushChunk(png, "IHDR", ihdr);

    // Raw scanlines with filter type 0.
    const usize row_bytes = static_cast<usize>(width) * 4u;
    std::vector<u8> raw;
    raw.reserve((row_bytes + 1u) * height);
    for (u32 y = 0; y < height; ++y) {
        raw.push_back(0);
        const u8* row = rgba + static_cast<usize>(y) * row_bytes;
        raw.insert(raw.end(), row, row + row_bytes);
    }
    PushChunk(png, "IDAT", ZlibStore(raw));
    PushChunk(png, "IEND", std::vector<u8>());
    return png;
}

bool Image::SavePng(const std::string& path) const {
    const std::vector<u8> encoded = EncodePng(width, height, pixels.data());
    if (encoded.empty()) {
        return false;
    }
    return WriteBinaryFile(path, encoded.data(), encoded.size());
}

bool Image::SavePpm(const std::string& path) const {
    std::vector<u8> ppm;
    const std::string header = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
    ppm.insert(ppm.end(), header.begin(), header.end());
    ppm.reserve(header.size() + static_cast<usize>(width) * height * 3u);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const u8* pixel = At(x, y);
            ppm.push_back(pixel[0]);
            ppm.push_back(pixel[1]);
            ppm.push_back(pixel[2]);
        }
    }
    return WriteBinaryFile(path, ppm.data(), ppm.size());
}

bool Image::SaveTga(const std::string& path) const {
    std::vector<u8> tga;
    tga.resize(18, 0);
    tga[2] = 2;  // uncompressed true colour
    tga[12] = static_cast<u8>(width & 0xFFu);
    tga[13] = static_cast<u8>((width >> 8u) & 0xFFu);
    tga[14] = static_cast<u8>(height & 0xFFu);
    tga[15] = static_cast<u8>((height >> 8u) & 0xFFu);
    tga[16] = 32;  // bits per pixel
    tga[17] = 8;   // alpha bits (TGA is bottom-up; we write top-down and flip)
    tga.reserve(18 + static_cast<usize>(width) * height * 4u);
    for (i32 y = static_cast<i32>(height) - 1; y >= 0; --y) {
        for (u32 x = 0; x < width; ++x) {
            const u8* pixel = At(x, static_cast<u32>(y));
            tga.push_back(pixel[2]);  // B
            tga.push_back(pixel[1]);  // G
            tga.push_back(pixel[0]);  // R
            tga.push_back(pixel[3]);  // A
        }
    }
    return WriteBinaryFile(path, tga.data(), tga.size());
}

}  // namespace valgo
