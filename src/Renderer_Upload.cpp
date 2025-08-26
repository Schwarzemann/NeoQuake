#include "Renderer.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <cstdio>
#include <cctype>
#include <string>

// Tiny helpers live in this TU only.
namespace {
    // quick-n-dirty lowercaser so we can make wrap-mode decisions by name
    std::string toLower(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // Decide whether a texture wants clamp or repeat.
    // Quake-y convention: skies/water often look better clamped at the edges.
    bool shouldClampWrap(const std::string& name) {
        std::string n = toLower(name);
        // very light heuristics — tweak as needed
        return (!n.empty() && (n[0] == '*' || n.rfind("sky", 0) == 0 || n.rfind("env_", 0) == 0));
    }

    // Check if an extension string contains a token (GL 2.1 style).
    bool hasExtension(const char* token) {
        const GLubyte* ext = glGetString(GL_EXTENSIONS);
        if (!ext || !token) return false;
        const char* all = reinterpret_cast<const char*>(ext);
        return std::string(all).find(token) != std::string::npos;
    }

    // Try to enable some tasteful anisotropy if the driver supports it.
    void maybeEnableAnisotropy(GLenum target, GLfloat desired = 4.0f) {
        if (!hasExtension("GL_EXT_texture_filter_anisotropic")) return;
        GLfloat maxAniso = 1.0f;
        glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
        if (maxAniso < 1.0f) return;
        const GLfloat aniso = (desired > maxAniso) ? maxAniso : desired;
        glTexParameterf(target, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, aniso);
    }
}

namespace neoquake {

void Renderer::uploadLightmapAtlas(const BSPMap& map) {
    lmValid = false;
    if (map.lmAtlas.width <=0 || map.lmAtlas.height<=0 || map.lmAtlas.rgba.empty()) {
        return;
    }
    lmTex = createTextureRGBA(map.lmAtlas.rgba, map.lmAtlas.width, map.lmAtlas.height, /*nearest*/true);
    lmValid = (lmTex.glId != 0);
}

// Converts paletted BSP textures to RGBA and creates GL textures for them.
void Renderer::uploadTextures(const BSPMap& map) {
    gltex.clear();
    gltex.resize(map.textures.size());

    for (size_t i = 0; i < map.textures.size(); ++i) {
        const auto& t = map.textures[i];

        if (t.width > 0 && t.height > 0 && !t.indices.empty()) {
            // Convert the paletted texture from the BSP to RGBA bytes
            auto rgba = IndexedToRGBA(t, map.paletteRGB);

            // Create the GL texture (the helper returns a small wrapper with an ID)
            // We pass "nearest=true" to respect that crunchy Quake vibe by default.
            gltex[i].tex = createTextureRGBA(rgba, (int)t.width, (int)t.height, /*nearest*/true);

            // Set a couple of tastefully conservative parameters
            glBindTexture(GL_TEXTURE_2D, gltex[i].tex.glId);

            // Wrap mode: skies/water/etc. tend to look better clamped; everything else repeats.
            const bool clamp = shouldClampWrap(t.name);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP : GL_REPEAT);

            // Filtering: nearest already set in createTextureRGBA; add anisotropy if available.
            maybeEnableAnisotropy(GL_TEXTURE_2D, /*desired*/4.0f);

            glBindTexture(GL_TEXTURE_2D, 0);
        } else {
            // Texture data is missing or empty — we still want *something* visible.
            // We’ll make a tiny 2×2 magenta/black checker so it’s obvious.
            std::vector<unsigned char> px = {
                // (r,g,b,a) ×4
                255,   0, 255, 255,   0,   0,   0, 255,
                  0,   0,   0, 255, 255,   0, 255, 255
            };
            uploadLightmapAtlas(map);
            gltex[i].tex = createTextureRGBA(px, 2, 2, /*nearest*/true);

            // Clamp the fallback so it doesn’t smear on the edges.
            glBindTexture(GL_TEXTURE_2D, gltex[i].tex.glId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
}

} // namespace neoquake
