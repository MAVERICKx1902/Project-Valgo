// =============================================================================
// Project Valgo — engine core: in-memory images and screenshot encoding.
//
// Used by the screenshot system, the software rasteriser's readback path and
// (later) by the texture pipeline. PNG output is written with stored (uncom-
// pressed) deflate blocks: valid PNG, zero external dependencies. Screenshot
// size matters far less than having no third-party code in the core.
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include "valgo/core/types.h"

namespace valgo {

/// 8-bit RGBA image, tightly packed, top-left origin.
struct Image {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels;  ///< width * height * 4 bytes

    Image() = default;
    Image(u32 w, u32 h, u8 r = 0, u8 g = 0, u8 b = 0, u8 a = 255);

    bool empty() const { return width == 0 || height == 0; }
    usize byte_size() const { return pixels.size(); }

    u8* At(u32 x, u32 y);
    const u8* At(u32 x, u32 y) const;

    /// Simple order-independent checksum, useful for regression tests that need
    /// to detect unintended rendering changes without storing golden images.
    u64 Checksum() const;

    /// Writes the image as a PNG file. Returns false on I/O failure.
    bool SavePng(const std::string& path) const;

    /// Writes the image as a binary PPM (debugging on tools without PNG).
    bool SavePpm(const std::string& path) const;

    /// Writes the image as an uncompressed TGA (fast dump for frame capture).
    bool SaveTga(const std::string& path) const;
};

/// Encodes RGBA8 pixels into PNG bytes (stored deflate; no compression deps).
std::vector<u8> EncodePng(u32 width, u32 height, const u8* rgba);

}  // namespace valgo
