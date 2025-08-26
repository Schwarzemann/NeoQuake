#include "BSP.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>

//
// --------------------------------------------------------------------------------------
// Palettes in Quake are simple: 256 colors × 3 bytes (RGB) = 768 bytes.
// - Validate and normalize palettes
// - Apply gamma / brightness / contrast
// - Save palette back to .lmp or export to JASC-PAL
// - Load JASC-PAL (handy for editing with paint tools)
// - Find nearest color, build remap tables, peek a color by index
// --------------------------------------------------------------------------------------
//

namespace neoquake {

// --------------------------------------------------------------------------------------
// Handy “read the whole file” helper. We use this a lot across the project.
// --------------------------------------------------------------------------------------
static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0, std::ios::beg);
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<bool>(f);
}

// --------------------------------------------------------------------------------------
// Validate that a buffer *looks* like a Quake-style palette: exactly 768 bytes.
// We keep this tiny and cheap because it sits on hot paths.
// --------------------------------------------------------------------------------------
static inline bool IsValidPalette768(const std::vector<uint8_t>& rgb) {
    return rgb.size() == 768;
}

// --------------------------------------------------------------------------------------
// If someone passes a palette with the wrong size (common during experiments),
// this tries to “make it make sense”: truncate or pad with zeros to 768.
// It's better than crashing or silently using garbage.
// --------------------------------------------------------------------------------------
static void NormalizePaletteSize768(std::vector<uint8_t>& rgb) {
    if (rgb.size() == 768) return;
    if (rgb.size() > 768) {
        rgb.resize(768);
    } else {
        rgb.resize(768, 0);
    }
}

// --------------------------------------------------------------------------------------
// ORIGINAL API
// Loads a .lmp palette (expects 768 bytes) into outRGB.
// --------------------------------------------------------------------------------------
bool LoadPaletteLMP(const std::string& path, std::vector<uint8_t>& outRGB) {
    std::vector<uint8_t> buf;
    if(!readAll(path, buf)) return false;
    if(buf.size() < 768) return false;                 // strict like the original
    outRGB.assign(buf.begin(), buf.begin()+768);       // copy exactly the first 768 bytes
    return true;
}

// --------------------------------------------------------------------------------------
// Save a palette back to .lmp (exactly 768 bytes). Returns false if the input
// is malformed and we decide not to “fix it up” silently.
// Use SavePaletteLMPRelaxed if you want auto-normalization.
// --------------------------------------------------------------------------------------
bool SavePaletteLMP(const std::string& path, const std::vector<uint8_t>& rgb) {
    if (!IsValidPalette768(rgb)) return false;
    std::ofstream f(path, std::ios::binary);
    if(!f) return false;
    f.write(reinterpret_cast<const char*>(rgb.data()), 768);
    return static_cast<bool>(f);
}

// --------------------------------------------------------------------------------------
// Same as above, but will auto-fix the size to 768 (truncate or pad) before writing.
// Slightly more forgiving when you’re hacking on palettes.
// --------------------------------------------------------------------------------------
bool SavePaletteLMPRelaxed(const std::string& path, std::vector<uint8_t> rgb) {
    NormalizePaletteSize768(rgb);
    std::ofstream f(path, std::ios::binary);
    if(!f) return false;
    f.write(reinterpret_cast<const char*>(rgb.data()), 768);
    return static_cast<bool>(f);
}

// --------------------------------------------------------------------------------------
// Load JASC-PAL (text) format. This is convenient if you tweak colors in 2D tools.
// Expected header:
//   JASC-PAL
//   0100
//   256
//   r g b
//   ...
// Values are 0..255. We store them as packed RGB bytes (768 total).
// --------------------------------------------------------------------------------------
bool LoadPaletteJASCPAL(const std::string& path, std::vector<uint8_t>& outRGB) {
    std::ifstream f(path);
    if (!f) return false;

    std::string header;
    std::getline(f, header);
    if (header != "JASC-PAL") return false;

    std::string ver; std::getline(f, ver);
    if (ver != "0100") return false;

    int count = 0;
    f >> count;
    if (count != 256) return false;

    std::vector<uint8_t> rgb;
    rgb.reserve(768);
    for (int i = 0; i < 256; ++i) {
        int r, g, b;
        if (!(f >> r >> g >> b)) return false;
        rgb.push_back(static_cast<uint8_t>(std::clamp(r, 0, 255)));
        rgb.push_back(static_cast<uint8_t>(std::clamp(g, 0, 255)));
        rgb.push_back(static_cast<uint8_t>(std::clamp(b, 0, 255)));
    }
    if (rgb.size() != 768) return false;
    outRGB = std::move(rgb);
    return true;
}

// --------------------------------------------------------------------------------------
// Save as JASC-PAL so you can quickly edit in external tools and round-trip.
// --------------------------------------------------------------------------------------
bool SavePaletteJASCPAL(const std::string& path, const std::vector<uint8_t>& rgb) {
    if (!IsValidPalette768(rgb)) return false;
    std::ofstream f(path);
    if (!f) return false;

    f << "JASC-PAL\n";
    f << "0100\n";
    f << "256\n";
    for (int i = 0; i < 256; ++i) {
        const int base = i * 3;
        f << (int)rgb[base + 0] << " "
          << (int)rgb[base + 1] << " "
          << (int)rgb[base + 2] << "\n";
    }
    return static_cast<bool>(f);
}

// --------------------------------------------------------------------------------------
// Get a single color (r,g,b) from the palette, safely. If idx is out of range,
// we’ll return black, because “black” is usually a harmless default.
// --------------------------------------------------------------------------------------
std::array<uint8_t,3> GetPaletteColor(const std::vector<uint8_t>& rgb, int idx) {
    if (!IsValidPalette768(rgb) || idx < 0 || idx > 255) return {0,0,0};
    const int base = idx * 3;
    return { rgb[base + 0], rgb[base + 1], rgb[base + 2] };
}

// --------------------------------------------------------------------------------------
// Apply gamma to the *palette itself*. This is nice when you want a global “tone”
// without touching every texture. gamma=1.0 is a no-op; gamma=2.2 is common.
// --------------------------------------------------------------------------------------
void ApplyGammaToPalette(std::vector<uint8_t>& rgb, float gamma) {
    if (!IsValidPalette768(rgb) || std::abs(gamma - 1.0f) <= 1e-5f) return;

    const float inv = 1.0f / std::max(gamma, 1e-6f);

    auto fix = [inv](uint8_t c) -> uint8_t {
        float lin = static_cast<float>(c) / 255.0f;
        float out = std::pow(lin, inv) * 255.0f;
        out = std::clamp(out, 0.0f, 255.0f);
        return static_cast<uint8_t>(out + 0.5f);
    };

    for (int i = 0; i < 768; ++i) {
        rgb[i] = fix(rgb[i]);
    }
}

// --------------------------------------------------------------------------------------
// Quick brightness/contrast tweak. brightness and contrast are in a friendly 0..1 range:
//   brightness 0.0 = darker, 1.0 = brighter (0.5 = no change)
//   contrast   0.0 = washed out, 1.0 = punchier (0.5 = no change)
// Nothing scientific here—just practical.
// --------------------------------------------------------------------------------------
void ApplyBrightnessContrastToPalette(std::vector<uint8_t>& rgb, float brightness, float contrast) {
    if (!IsValidPalette768(rgb)) return;

    // Map [0..1] -> [-1..1] centered at 0, then scale. 0 means "no change".
    float b = (brightness - 0.5f) * 2.0f;   // -1 .. +1
    float c = (contrast   - 0.5f) * 2.0f;   // -1 .. +1

    for (int i = 0; i < 768; ++i) {
        float v = static_cast<float>(rgb[i]) / 255.0f;

        // Apply contrast around 0.5 to keep mid-tones anchored.
        v = (v - 0.5f) * (1.0f + c) + 0.5f;

        // Apply brightness shift.
        v = v + b * 0.5f;

        v = std::clamp(v, 0.0f, 1.0f);
        rgb[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
}

// --------------------------------------------------------------------------------------
// Brute-force search for the nearest palette entry to a given RGB. Great for
// quantization or remapping outside assets.
// --------------------------------------------------------------------------------------
int FindNearestPaletteIndex(const std::vector<uint8_t>& rgb, uint8_t r, uint8_t g, uint8_t b) {
    if (!IsValidPalette768(rgb)) return 0;
    int bestIdx = 0;
    int bestDist = INT32_MAX;
    for (int i = 0; i < 256; ++i) {
        const int base = i * 3;
        int dr = int(rgb[base+0]) - int(r);
        int dg = int(rgb[base+1]) - int(g);
        int db = int(rgb[base+2]) - int(b);
        int d2 = dr*dr + dg*dg + db*db;
        if (d2 < bestDist) { bestDist = d2; bestIdx = i; }
    }
    return bestIdx;
}

// --------------------------------------------------------------------------------------
// Build a remap table from one palette to another. For each index in src,
// we find the nearest color in dst and store that mapping as a byte.
// Use this when you want to “recolor” indices without touching the textures.
// --------------------------------------------------------------------------------------
std::vector<uint8_t> BuildPaletteRemapTable(const std::vector<uint8_t>& src,
                                            const std::vector<uint8_t>& dst) {
    std::vector<uint8_t> remap(256, 0);
    if (!IsValidPalette768(src) || !IsValidPalette768(dst)) return remap;
    for (int i = 0; i < 256; ++i) {
        const int base = i * 3;
        uint8_t r = src[base+0];
        uint8_t g = src[base+1];
        uint8_t b = src[base+2];
        remap[i] = static_cast<uint8_t>(FindNearestPaletteIndex(dst, r, g, b));
    }
    return remap;
}

// --------------------------------------------------------------------------------------
// Apply a remap table to a vector of indices (in-place). This does NOT touch RGBA pixels,
// just the 8-bit index buffer.
// --------------------------------------------------------------------------------------
void ApplyIndexRemap(std::vector<uint8_t>& indices, const std::vector<uint8_t>& remap) {
    if (remap.size() != 256) return;
    for (auto& ix : indices) {
        ix = remap[ix];
    }
}

} // namespace neoquake
