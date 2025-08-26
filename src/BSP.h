#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

namespace neoquake {

#pragma pack(push,1)
struct Lump { int32_t offset; int32_t size; };
struct BSPHeader {
    int32_t version;       // Quake 1 is 29
    Lump lumps[15];
};
#pragma pack(pop)

enum LumpIndex {
    LUMP_ENTITIES=0,
    LUMP_PLANES=1,
    LUMP_MIPTEX=2,
    LUMP_VERTEXES=3,
    LUMP_VISLIST=4,
    LUMP_NODES=5,
    LUMP_TEXINFO=6,
    LUMP_FACES=7,
    LUMP_LIGHTING=8,
    LUMP_CLIPNODES=9,
    LUMP_LEAFS=10,
    LUMP_MARKSURFACES=11,
    LUMP_EDGES=12,
    LUMP_SURFEDGES=13,
    LUMP_MODELS=14
};

struct Vec3 { float x,y,z; };

struct Plane { float normal[3]; float dist; int32_t type; };
struct Node {
    int32_t planenum;
    int16_t child_front; // note: high bit indicates leaf
    int16_t child_back;
    int16_t mins[3], maxs[3];
    uint16_t firstface;
    uint16_t numfaces;
};
struct TexInfo {
    float s[4];
    float t[4];
    int32_t miptex; // index
    int32_t flags;
};
struct Face {
    int16_t planenum;
    int16_t side;
    int32_t firstedge;
    int16_t numedges;
    int16_t texinfo;
    uint8_t styles[4];
    int32_t lightofs; // offset into lighting lump
};
struct Edge { uint16_t v0, v1; };

struct MipTexHeader { // dmiptexlump_t
    int32_t nummiptex;
    // followed by int32 offsets[nummiptex]
};
struct MipTex { // miptex_t
    char name[16];
    uint32_t width;
    uint32_t height;
    uint32_t offsets[4];
    // followed by pixel data
};

struct Model {
    float mins[3], maxs[3];
    Vec3 origin;
    int32_t headnode[4];
    int32_t visleafs;
    int32_t firstface;
    int32_t numfaces;
};

#pragma pack(push,1)
struct Leaf {
    int32_t contents;
    int32_t visofs;
    int16_t mins[3], maxs[3];
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    uint8_t ambient_level[4];
};
#pragma pack(pop)

struct BSPTexture {
    std::string name;
    uint32_t width=0, height=0;
    std::vector<uint8_t> indices; // 8-bit indices into palette (level 0 only)
};

struct LightmapRect {
    int x = 0, y = 0, w = 0, h = 0;
    int lightofs = -1;     // BSP lighting offset for this face (first style)
    bool valid = false;    // no LM data? then false -> draw fullbright
};

struct LightmapAtlas {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;       // grayscale into RGB, A=255
    std::vector<LightmapRect> perFace; // 1:1 with faces
};

struct BSPMesh {
    int textureIndex = -1;           // diffuse texture index (miptex)
    int faceIndex = -1;           // which BSP face this mesh belongs to (1:1)
    // Vertex layout after lightmap build: pos(3) + uv0(2) + uv1(2) = 7 floats
    std::vector<float> vertices;     // [x y z u0 v0 u1 v1] * N
};

// --- Entities (key/value blobs) ---
struct BSPEntityKV {
    std::string key;
    std::string value;
};
struct BSPEntity {
    std::vector<BSPEntityKV> kv;
    const char* find(const char* k) const {
        for (auto& p : kv) if (p.key == k) return p.value.c_str();
        return nullptr;
    }
    std::string classname() const {
        if (auto c = find("classname")) return c; return {};
    }
};

struct BSPMap {
    int32_t version = 0;
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    std::vector<int32_t> surfedges;
    std::vector<Face> faces;
    std::vector<TexInfo> texinfos;
    std::vector<BSPTexture> textures; // decoded from miplump (indices)
    std::vector<Model> models;
    // palette (optional)
    std::vector<uint8_t> paletteRGB; // size 768 (256*3)
    std::vector<BSPEntity> entities; // NEW
    std::vector<uint8_t> lighting;    // raw BSP LUMP_LIGHTING (keep ONE declaration)

    std::vector<BSPMesh> meshes;      // triangulated faces (keep ONE declaration)

    // NEW: lightmap atlas (filled after meshes are built)
    LightmapAtlas lmAtlas;
};

// Load palette.lmp (768 bytes: 256*RGB)
bool LoadPaletteLMP(const std::string& path, std::vector<uint8_t>& outRGB);

// Palette I/O
bool SavePaletteLMP(const std::string& path, const std::vector<uint8_t>& rgb);
bool SavePaletteLMPRelaxed(const std::string& path, std::vector<uint8_t> rgb);
bool LoadPaletteJASCPAL(const std::string& path, std::vector<uint8_t>& outRGB);
bool SavePaletteJASCPAL(const std::string& path, const std::vector<uint8_t>& rgb);

// Palette ops
std::array<uint8_t,3> GetPaletteColor(const std::vector<uint8_t>& rgb, int idx);
void ApplyGammaToPalette(std::vector<uint8_t>& rgb, float gamma);
void ApplyBrightnessContrastToPalette(std::vector<uint8_t>& rgb, float brightness, float contrast);
int  FindNearestPaletteIndex(const std::vector<uint8_t>& rgb, uint8_t r, uint8_t g, uint8_t b);
std::vector<uint8_t> BuildPaletteRemapTable(const std::vector<uint8_t>& src,
                                            const std::vector<uint8_t>& dst);
void ApplyIndexRemap(std::vector<uint8_t>& indices, const std::vector<uint8_t>& remap);


// Load BSP v29 (Quake 1). Returns nullopt on failure, else a filled map with triangulated meshes.
std::optional<BSPMap> LoadBSP(const std::string& bspPath, const std::string& palettePath, std::string& error);

// Utility: convert indexed texture to RGBA using palette (index 255 becomes transparent).
std::vector<uint8_t> IndexedToRGBA(const BSPTexture& tex, const std::vector<uint8_t>& palette);

} // namespace neoquake
