#pragma once
#include "BSP.h"
#include "Texture.h"
#include <vector>

namespace neoquake {

struct GLFace {
    int tex = -1;
    std::vector<float> verts; // x y z u v per vertex
};

struct GLTexture {
    Texture tex;
};

struct Renderer {
    // OpenGL textures aligned with map.textures
    std::vector<GLTexture> gltex;
    Texture             lmTex;     // lightmap atlas
    bool                lmValid = false;

    // upload textures from BSP (requires palette converted RGBA)
    void uploadTextures(const BSPMap& map);
    void uploadLightmapAtlas(const BSPMap& map); // NEW

    // draw meshes (fixed-function GL 1.x)
    void drawMap(const BSPMap& map);

    // View modes for winding/culling
    enum class ViewMode { Interior = 0, Exterior = 1, TwoSided = 2 };

    void setViewMode(ViewMode m);
    int  cycleViewMode();          // returns new mode index (0..2)
    ViewMode getViewMode() const;
    const char* viewModeName() const;

};

} // namespace neoquake
