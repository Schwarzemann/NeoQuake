#include "Texture.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <cstdlib>   // getenv, atof
#include <cstring>
#include <string>
#include <algorithm>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

typedef void (APIENTRY *PFNGLGENERATEMIPMAPPROC)(GLenum target);

namespace {

// Quick check for a substring in the legacy extension string.
bool hasExtension(const char* token) {
    const GLubyte* ext = glGetString(GL_EXTENSIONS);
    if (!ext || !token) return false;
    return std::string(reinterpret_cast<const char*>(ext)).find(token) != std::string::npos;
}

// Try to enable anisotropy if the user asked for it and the driver supports it.
// Value comes from NEOQUAKE_TEX_ANISO (e.g. "4", "8", "16"). Non-positive → skip.
void maybeEnableAnisotropy(GLenum target) {
    const char* env = std::getenv("NEOQUAKE_TEX_ANISO");
    if (!env || !*env) return;
    const float requested = std::max(0.0f, static_cast<float>(std::atof(env)));
    if (requested <= 0.0f) return;
    if (!hasExtension("GL_EXT_texture_filter_anisotropic")) return;

    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso < 1.0f) return;

    const GLfloat aniso = std::min(maxAniso, requested);
    glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
}

// Map NEOQUAKE_TEX_WRAP env to GL enum. Defaults to REPEAT (classic Quake behavior).
GLint wrapFromEnv() {
    const char* w = std::getenv("NEOQUAKE_TEX_WRAP");
    if (!w || !*w) return GL_REPEAT;
    std::string s(w);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s == "clamp" || s == "edge" || s == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
#ifdef GL_MIRRORED_REPEAT
    if (s == "mirror" || s == "mirrored") return GL_MIRRORED_REPEAT;
#endif
    return GL_REPEAT;
}

// Should we build mipmaps for this upload? (off by default to keep things crispy)
bool wantMipmaps() {
    const char* m = std::getenv("NEOQUAKE_TEX_MIPS");
    return (m && *m && *m != '0');
}

} // anon

// Take raw RGBA bytes and turn them into a GL texture.
Texture createTextureRGBA(const unsigned char* rgba, int width, int height, bool nearest) {
    Texture t;
    t.width = width;
    t.height = height;

    // If input is nonsense, create a tiny magenta checker so it’s obvious.
    if (!rgba || width <= 0 || height <= 0) {
        unsigned char fallback[16] = {
            255, 0, 255, 255,   0, 0, 0, 255,
              0, 0,   0, 255, 255, 0,255, 255
        };
        glGenTextures(1, &t.glId);
        glBindTexture(GL_TEXTURE_2D, t.glId);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, fallback);
        return t;
    }

    glGenTextures(1, &t.glId);
    glBindTexture(GL_TEXTURE_2D, t.glId);

    // When building mipmaps we want a mip-aware MIN filter; otherwise the classic pair.
    const bool buildMips = wantMipmaps();
    if (buildMips) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    }

    // Wrap mode can be overridden via env; default remains REPEAT like your original code.
    const GLint wrap = wrapFromEnv();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Upload the base level.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // Optional extras (mips + anisotropy), fully safe to skip if the platform can’t do it.
    if (buildMips) {
        // Try modern glGenerateMipmap first, otherwise fall back to legacy auto-mipmap.
        PFNGLGENERATEMIPMAPPROC genMips =
            reinterpret_cast<PFNGLGENERATEMIPMAPPROC>(glfwGetProcAddress("glGenerateMipmap"));
        if (genMips) {
            genMips(GL_TEXTURE_2D);
        } else {
            // Some GL 1.4-era drivers support this flag: generate mipmaps on next upload.
#ifndef GL_GENERATE_MIPMAP
#define GL_GENERATE_MIPMAP 0x8191
#endif
            glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
            // Re-upload to trigger generation (cheap for small textures).
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE);
        }
        // Nice to have if available.
        maybeEnableAnisotropy(GL_TEXTURE_2D);
    }

    return t;
}