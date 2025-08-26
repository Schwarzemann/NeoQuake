#include "BSP.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

//
// -------------------------------------------------------------------------------------------------
// Converts palettized Quake texture data to RGBA and provides a couple of small,
// opt-in image utilities useful in a renderer (gamma correction, alpha handling,
// channel swizzles, tinting, fallback pattern, and CPU mipmap generation).
// -------------------------------------------------------------------------------------------------
//


namespace neoquake {

// -------------------------------------------------------------------------------------------------
// Internal helpers
// -------------------------------------------------------------------------------------------------

// Read a single RGB triplet from a 256×3 palette buffer. If palette is missing/short,
// we gracefully fall back to grayscale, which keeps rendering alive with “something” on screen.
static inline void paletteLookup(
    const std::vector<uint8_t>& palette, uint8_t idx, uint8_t& r, uint8_t& g, uint8_t& b)
{
    // Quake palettes are 256 entries × 3 bytes = 768 bytes.
    if (palette.size() >= 768) {
        const size_t base = static_cast<size_t>(idx) * 3u;
        r = palette[base + 0];
        g = palette[base + 1];
        b = palette[base + 2];
    } else {
        // Soft-degrade into grayscale so missing palette files don't crash the pipeline.
        r = g = b = idx;
    }
}

// Clamp float to 0..255 and cast.
static inline uint8_t clampToByte(float x) {
    if (x < 0.f)   x = 0.f;
    if (x > 255.f) x = 255.f;
    return static_cast<uint8_t>(x + 0.5f);
}

// Compute the next mip level dimension (never below 1).
static inline int nextMipDim(int v) { return std::max(1, v >> 1); }

// -------------------------------------------------------------------------------------------------
// Original API
// -------------------------------------------------------------------------------------------------

// Converts palettized indices to RGBA8.
// - Alpha is 0 for palette index 255 (a common "transparent" convention in Quake assets),
//   otherwise 255. This mirrors the original code exactly.
// - If the palette is missing/too short, we fall back to a grayscale approximation.
//
std::vector<uint8_t> IndexedToRGBA(const BSPTexture& tex, const std::vector<uint8_t>& palette) {
    std::vector<uint8_t> rgba;

    // Quick sanity: if the texture is empty, return an empty result.
    if (tex.width == 0 || tex.height == 0 || tex.indices.empty())
        return rgba;

    const size_t pixelCount = static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height);
    rgba.resize(pixelCount * 4u);

    for (size_t i = 0; i < tex.indices.size(); ++i) {
        const uint8_t idx = tex.indices[i];

        uint8_t r, g, b;
        paletteLookup(palette, idx, r, g, b);

        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = (idx == 255) ? 0 : 255; // index 255 = transparent convention
    }

    return rgba;
}

// -------------------------------------------------------------------------------------------------
// Extended utilities
// -------------------------------------------------------------------------------------------------

// Advanced conversion with options (transparent index, premultiply, gamma).
// Use when you need more control but don't want to change existing call sites.
// Example:
// auto rgba = IndexedToRGBAEx(tex, palette, /*transparent_index=*/255,
//                               /*premultiply=*/true, /*gamma=*/2.2f);
std::vector<uint8_t> IndexedToRGBAEx(const BSPTexture& tex,
                                     const std::vector<uint8_t>& palette,
                                     uint8_t transparent_index /*=255*/,
                                     bool premultiply /*=false*/,
                                     float gamma /*=1.0f*/)
{
    std::vector<uint8_t> rgba;

    if (tex.width == 0 || tex.height == 0 || tex.indices.empty())
        return rgba;

    const size_t pixelCount = static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height);
    rgba.resize(pixelCount * 4u);

    // Precompute gamma transform if needed.
    // We apply gamma in the “display” sense: out = pow(in/255, 1/gamma)*255.
    const bool useGamma = (std::abs(gamma - 1.0f) > 1e-5f);
    uint8_t gammaLUT[256];
    if (useGamma) {
        const float inv = 1.0f / std::max(gamma, 1e-6f);
        for (int v = 0; v < 256; ++v) {
            float lin = static_cast<float>(v) / 255.0f;
            gammaLUT[v] = clampToByte(std::pow(lin, inv) * 255.0f);
        }
    }

    for (size_t i = 0; i < tex.indices.size(); ++i) {
        const uint8_t idx = tex.indices[i];

        uint8_t r, g, b;
        paletteLookup(palette, idx, r, g, b);

        if (useGamma) {
            r = gammaLUT[r];
            g = gammaLUT[g];
            b = gammaLUT[b];
        }

        uint8_t a = (idx == transparent_index) ? 0u : 255u;

        if (premultiply) {
            // Premultiply color by alpha. This helps with correct edge filtering in GPUs.
            const float af = static_cast<float>(a) / 255.0f;
            r = clampToByte(static_cast<float>(r) * af);
            g = clampToByte(static_cast<float>(g) * af);
            b = clampToByte(static_cast<float>(b) * af);
        }

        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }

    return rgba;
}

// In-place gamma correction on an RGBA8 buffer.
void ApplyGammaRGBA(std::vector<uint8_t>& rgba, float gamma) {
    if (rgba.empty() || std::abs(gamma - 1.0f) <= 1e-5f) return;

    uint8_t lut[256];
    const float inv = 1.0f / std::max(gamma, 1e-6f);
    for (int v = 0; v < 256; ++v) {
        float lin = static_cast<float>(v) / 255.0f;
        lut[v] = clampToByte(std::pow(lin, inv) * 255.0f);
    }

    // Apply to RGB, leave alpha as-is.
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        rgba[i + 0] = lut[rgba[i + 0]];
        rgba[i + 1] = lut[rgba[i + 1]];
        rgba[i + 2] = lut[rgba[i + 2]];
    }
}

// In-place channel swizzle. Useful when a backend expects BGRA, etc.
// e.g. SwizzleRGBA(rgba, width, height, "BGRA") or "RGBA" (no-op).
void SwizzleRGBA(std::vector<uint8_t>& rgba, int /*width*/, int /*height*/, const char* order) {
    if (!order || std::strlen(order) != 4 || rgba.empty()) return;

    // Build a remap table from letters to indices in [0..3].
    auto chIndex = [](char c)->int {
        switch (c) {
            case 'R': case 'r': return 0;
            case 'G': case 'g': return 1;
            case 'B': case 'b': return 2;
            case 'A': case 'a': return 3;
            default: return -1;
        }
    };

    int map[4] = { chIndex(order[0]), chIndex(order[1]), chIndex(order[2]), chIndex(order[3]) };
    if (map[0] < 0 || map[1] < 0 || map[2] < 0 || map[3] < 0) return;

    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        uint8_t r = rgba[i + 0], g = rgba[i + 1], b = rgba[i + 2], a = rgba[i + 3];
        uint8_t src[4] = { r, g, b, a };
        rgba[i + 0] = src[map[0]];
        rgba[i + 1] = src[map[1]];
        rgba[i + 2] = src[map[2]];
        rgba[i + 3] = src[map[3]];
    }
}

// In-place multiplicative tinting (RGB only; alpha unchanged).
// Values are 0..1; e.g., TintRGBA(rgba, w, h, 1.0f, 0.8f, 0.8f) for a slight warm tint.
void TintRGBA(std::vector<uint8_t>& rgba, int /*width*/, int /*height*/,
              float tr, float tg, float tb)
{
    tr = std::max(0.f, tr);
    tg = std::max(0.f, tg);
    tb = std::max(0.f, tb);

    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        rgba[i + 0] = clampToByte(static_cast<float>(rgba[i + 0]) * tr);
        rgba[i + 1] = clampToByte(static_cast<float>(rgba[i + 1]) * tg);
        rgba[i + 2] = clampToByte(static_cast<float>(rgba[i + 2]) * tb);
    }
}

// Tiny fallback texture (checkerboard) for cases where a texture is missing.
// Returns RGBA8 data with the requested size (min 2×2). Alpha is fully opaque.
std::vector<uint8_t> MakeCheckerRGBA(int width, int height, int cell = 8) {
    width  = std::max(2, width);
    height = std::max(2, height);
    cell   = std::max(1, cell);

    std::vector<uint8_t> out(static_cast<size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool on = (((x / cell) + (y / cell)) & 1) != 0;
            uint8_t v = on ? 200 : 60; // light vs dark
            size_t i = (static_cast<size_t>(y) * width + x) * 4u;
            out[i + 0] = v;
            out[i + 1] = v;
            out[i + 2] = v;
            out[i + 3] = 255;
        }
    }
    return out;
}

// CPU mipmap pyramid generator for RGBA8.
// Returns an array of levels; levels[0] is the original image, levels[1] is 1/2, etc.
// Simple box filter; sufficient for light previewing/GL upload.
std::vector<std::vector<uint8_t>> BuildMipmapsRGBA(const std::vector<uint8_t>& rgba,
                                                   int width, int height, int maxLevels = 0)
{
    std::vector<std::vector<uint8_t>> levels;
    if (rgba.empty() || width <= 0 || height <= 0) return levels;

    // Level 0
    levels.push_back(rgba);

    int w = width, h = height;

    // If maxLevels == 0, build until 1×1.
    while ((maxLevels == 0 || static_cast<int>(levels.size()) < maxLevels) && (w > 1 || h > 1)) {
        int nw = nextMipDim(w);
        int nh = nextMipDim(h);
        std::vector<uint8_t> next(static_cast<size_t>(nw) * nh * 4u, 0);

        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                // Average a 2×2 block from the previous level (clamping at edges).
                int sx = x * 2;
                int sy = y * 2;

                // Collect up to four samples (RGBA)
                uint32_t sum[4] = {0,0,0,0};
                int count = 0;
                for (int oy = 0; oy < 2; ++oy) {
                    for (int ox = 0; ox < 2; ++ox) {
                        int px = std::min(sx + ox, w - 1);
                        int py = std::min(sy + oy, h - 1);
                        size_t idx = (static_cast<size_t>(py) * w + px) * 4u;
                        sum[0] += levels.back()[idx + 0];
                        sum[1] += levels.back()[idx + 1];
                        sum[2] += levels.back()[idx + 2];
                        sum[3] += levels.back()[idx + 3];
                        ++count;
                    }
                }

                size_t di = (static_cast<size_t>(y) * nw + x) * 4u;
                next[di + 0] = static_cast<uint8_t>(sum[0] / count);
                next[di + 1] = static_cast<uint8_t>(sum[1] / count);
                next[di + 2] = static_cast<uint8_t>(sum[2] / count);
                next[di + 3] = static_cast<uint8_t>(sum[3] / count);
            }
        }

        levels.push_back(std::move(next));
        w = nw; h = nh;
    }

    return levels;
}

} // namespace neoquake
