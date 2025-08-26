#include "Texture.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <vector>
#include <cstdio>

// Thin convenience wrapper that forwards to the pointer version, but with a tiny guard: if your vector is smaller than w*h*4,
// we don’t read past the end — we just fall back to a tiny checker
// so the bug is obvious on screen instead of crashing.
Texture createTextureRGBA(const std::vector<unsigned char>& rgba,
                          int width, int height, bool nearest) {
    const size_t needed = (width > 0 && height > 0)
                        ? static_cast<size_t>(width) * static_cast<size_t>(height) * 4u
                        : 0u;

    if (needed == 0 || rgba.size() < needed) {
        std::fprintf(stderr,
            "[Texture] Warning: vector size (%zu) < expected (%zu) for %dx%d RGBA. "
            "Using a small checker fallback.\n",
            rgba.size(), needed, width, height);
        // Call the pointer variant with a null to trigger its internal fallback.
        return createTextureRGBA(static_cast<const unsigned char*>(nullptr), width, height, nearest);
    }

    return createTextureRGBA(rgba.data(), width, height, nearest);
}
