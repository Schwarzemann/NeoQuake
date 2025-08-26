#include "Renderer.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib> // getenv
#include <cstring>
#include <algorithm>
#include <cctype>

namespace neoquake {

static bool iequals(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

static int s_viewMode = 0; // 0=Interior, 1=Exterior, 2=TwoSided

void Renderer::setViewMode(ViewMode m) {
    int v = static_cast<int>(m);
    if (v < 0) v = 0;
    if (v > 2) v = 2;
    s_viewMode = v;
}
int Renderer::cycleViewMode() {
    s_viewMode = (s_viewMode + 1) % 3;
    return s_viewMode;
}
Renderer::ViewMode Renderer::getViewMode() const {
    return static_cast<ViewMode>(s_viewMode);
}
const char* Renderer::viewModeName() const {
    switch (s_viewMode) {
        case 0: return "Interior (CW front, cull back)";
        case 1: return "Exterior (CCW front, cull back)";
        case 2: return "Two-Sided (no cull)";
    }
    return "Unknown";
}

// Good old fixed-function rendering: bind a texture and dump triangles.
void Renderer::drawMap(const BSPMap& map) {
    // ----- One-time env config -----
    static bool envInit = false;
    static bool wireframe = false;
    static bool texless   = false;
    static float texMult  = 1.0f;

    if (!envInit) {
        envInit = true;
        if (const char* w = std::getenv("NEOQUAKE_WIREFRAME")) wireframe = (std::strcmp(w, "0") != 0);
        if (const char* t = std::getenv("NEOQUAKE_TEXLESS"))   texless   = (std::strcmp(t, "0") != 0);
        if (const char* m = std::getenv("NEOQUAKE_TEX_MULT")) {
            const float v = static_cast<float>(std::atof(m));
            texMult = std::max(0.1f, std::min(3.0f, v));
        }
        // Seed initial view mode from env (optional):
        //   NEOQUAKE_TWOSIDED=1  -> TwoSided
        //   NEOQUAKE_WINDING=CCW -> Exterior
        if (const char* ts = std::getenv("NEOQUAKE_TWOSIDED"); ts && std::strcmp(ts, "0") != 0) {
            s_viewMode = 2;
        } else if (const char* f = std::getenv("NEOQUAKE_WINDING"); f && iequals(f, "CCW")) {
            s_viewMode = 1;
        } else {
            s_viewMode = 0; // Interior default
        }
    }

    glEnable(GL_DEPTH_TEST);

    // ----- Apply current view mode -----
    const bool twoSided  = (s_viewMode == 2);
    const bool frontIsCW = (s_viewMode != 1); // 0 or 2 -> CW; 1 -> CCW

    if (twoSided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glFrontFace(frontIsCW ? GL_CW : GL_CCW);
        glCullFace(GL_BACK);
    }

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    if (!texless) {
        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(texMult, texMult, texMult, 1.0f);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor4f(1,1,1,1);
    }

    // ----- Draw (unchanged) -----
    GLuint lastBound = (GLuint)-1;
    for (const auto& mesh : map.meshes) {
        if (!texless) {
            GLuint t = 0;
            if (mesh.textureIndex >= 0 && mesh.textureIndex < (int)gltex.size())
                t = gltex[mesh.textureIndex].tex.glId;
            if (t != lastBound) { glBindTexture(GL_TEXTURE_2D, t); lastBound = t; }
        }

        const auto& v = mesh.vertices;
        if (v.size() < 5) continue;

        glBegin(GL_TRIANGLES);
        for (size_t i = 0; i + 4 < v.size(); i += 5) {
            if (!texless) glTexCoord2f(v[i+3], v[i+4]);
            glVertex3f(v[i+0], v[i+1], v[i+2]);
        }
        glEnd();
    }

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (!texless) glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace neoquake