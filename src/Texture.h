#pragma once
#include <vector>
#include <cstdint>

struct Texture {
    unsigned int glId = 0;
    int width = 0;
    int height = 0;
    bool hasAlpha = false;
};

// create GL texture from RGBA8 data
Texture createTextureRGBA(const unsigned char* rgba, int width, int height, bool nearest=true);

// convenience overload
Texture createTextureRGBA(const std::vector<unsigned char>& rgba, int width, int height, bool nearest=true);
