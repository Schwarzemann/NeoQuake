#include "BSP.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace neoquake {

// --------------------------------------------------------------------------------------
// readAll: utility to slurp an entire file into memory.
// We use this for BSP files and palettes so we can parse them from RAM later.
// --------------------------------------------------------------------------------------
static bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize((size_t)sz);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return true;
}

// Small helpers to grab 32-bit ints/floats out of the BSP binary.
// BSP data is little-endian, and memcpy is the safest way to avoid alignment issues.
static int32_t rd32(const uint8_t* p) { int32_t v; std::memcpy(&v,p,4); return v; }
static uint32_t ru32(const uint8_t* p) { uint32_t v; std::memcpy(&v,p,4); return v; }
static float rf32(const uint8_t* p) { float v; std::memcpy(&v,p,4); return v; }

// --------------------------------------------------------------------------------------
// buildMeshes: takes the raw BSP face/edge/vertex/texinfo data and turns it into
// renderable meshes. Each mesh here is basically a list of triangles with UVs ready
// for OpenGL to consume.
// --------------------------------------------------------------------------------------
static void buildMeshes(BSPMap& map) {
    map.meshes.clear();
    map.meshes.reserve(map.faces.size());

    for(size_t fi=0; fi<map.faces.size(); ++fi) {
        const Face& face = map.faces[fi];
        BSPMesh mesh;

        // Step 1: gather all vertex indices for this polygon by walking surfedges.
        std::vector<int> vind;
        vind.reserve(face.numedges);
        for(int i=0;i<face.numedges;i++) {
            int se = map.surfedges[face.firstedge+i];
            if(se >= 0) {
                const Edge& e = map.edges[se];
                vind.push_back(e.v0);
            } else {
                const Edge& e = map.edges[-se];
                vind.push_back(e.v1);
            }
        }

        // If the face is somehow degenerate (less than 3 verts), skip it.
        if(vind.size() < 3) { 
            map.meshes.push_back(mesh); 
            continue; 
        }

        // Step 2: figure out which texture this face wants to use.
        const TexInfo& ti = map.texinfos[face.texinfo];
        int texIndex = ti.miptex;
        mesh.textureIndex = texIndex >=0 && texIndex < (int)map.textures.size() ? texIndex : -1;

        // Grab texture size for UV normalization. Avoid divide-by-zero.
        float w = 1.f, h = 1.f;
        if(mesh.textureIndex>=0) {
            w = (float)map.textures[mesh.textureIndex].width;
            h = (float)map.textures[mesh.textureIndex].height;
            if(w<=0) w=1; 
            if(h<=0) h=1;
        }

        // Step 3: triangulate. Faces can be n-gons, so we "fan" triangles around the first vertex.
        auto addVertex = [&](int vi){
            const Vec3& p = map.vertices[vi];

            // BSP stores texture mapping as S/T vectors. We dot with vertex position.
            float s = p.x*ti.s[0] + p.y*ti.s[1] + p.z*ti.s[2] + ti.s[3];
            float t = p.x*ti.t[0] + p.y*ti.t[1] + p.z*ti.t[2] + ti.t[3];

            float u = s / w;
            float v = t / h;

            // OpenGL’s UV origin is different, so we flip V.
            v = 1.0f - v;

            // Push position + UV into the mesh vertex buffer.
            mesh.vertices.push_back(p.x);
            mesh.vertices.push_back(p.y);
            mesh.vertices.push_back(p.z);
            mesh.vertices.push_back(u);
            mesh.vertices.push_back(v);
        };

        // Emit triangles: (v0, vi, vi+1) for i in [1..n-2].
        for(size_t i=1;i+1<vind.size();++i) {
            addVertex(vind[0]);
            addVertex(vind[i]);
            addVertex(vind[i+1]);
        }

        map.meshes.push_back(std::move(mesh));
    }
}

// --------------------------------------------------------------------------------------
// LoadBSP: main entry point for reading a .bsp file into a BSPMap structure.
// Reads each "lump" (subsection) of the BSP file, validates it, and copies into memory.
// At the end, it also calls buildMeshes() so we have something to draw right away.
// --------------------------------------------------------------------------------------
std::optional<BSPMap> LoadBSP(const std::string& bspPath, const std::string& palettePath, std::string& error) {
    error.clear();
    std::vector<uint8_t> data;

    // Read whole file into memory.
    if(!readAll(bspPath,data)) { error = "Failed to read BSP file"; return std::nullopt; }
    if(data.size() < sizeof(BSPHeader)) { error = "File too small"; return std::nullopt; }

    // Quick header sanity.
    const BSPHeader* hdr = reinterpret_cast<const BSPHeader*>(data.data());
    BSPMap map; 
    map.version = hdr->version;
    if(map.version != 29) {
        error = "Unsupported BSP version (expected 29)";
        // Still continue — some derivatives claim 29 but are compatible.
    }

    // Helper to grab a "lump" (section) by index.
    auto lumpSpan = [&](int idx)->std::pair<const uint8_t*, size_t> {
        const Lump& l = hdr->lumps[idx];
        size_t off = (size_t)l.offset;
        size_t sz  = (size_t)l.size;
        if(off+sz > data.size()) return {nullptr, 0};
        return { data.data()+off, sz };
    };

    // --- Parse vertex data ---
    {
        auto [p,sz] = lumpSpan(LUMP_VERTEXES);
        if(sz % (sizeof(float)*3) != 0) { error = "Bad vertex lump size"; return std::nullopt; }
        size_t n = sz/(sizeof(float)*3);
        map.vertices.resize(n);
        for(size_t i=0;i<n;i++) {
            const uint8_t* q = p + i*(sizeof(float)*3);
            map.vertices[i].x = rf32(q+0);
            map.vertices[i].y = rf32(q+4);
            map.vertices[i].z = rf32(q+8);
        }
    }

    // --- Parse edges ---
    {
        auto [p,sz] = lumpSpan(LUMP_EDGES);
        if(sz % 4 != 0) { error = "Bad edges lump size"; return std::nullopt; }
        size_t n = sz/4;
        map.edges.resize(n);
        for(size_t i=0;i<n;i++) {
            map.edges[i].v0 = *(const uint16_t*)(p + i*4 + 0);
            map.edges[i].v1 = *(const uint16_t*)(p + i*4 + 2);
        }
    }

    // --- Parse surfedges (basically references into the edge list, with sign flips) ---
    {
        auto [p,sz] = lumpSpan(LUMP_SURFEDGES);
        if(sz % 4 != 0) { error = "Bad surfedges lump size"; return std::nullopt; }
        size_t n = sz/4;
        map.surfedges.resize(n);
        for(size_t i=0;i<n;i++) map.surfedges[i] = rd32(p + i*4);
    }

    // --- Parse faces (polygons) ---
    {
        auto [p,sz] = lumpSpan(LUMP_FACES);
        if(sz % sizeof(Face) != 0) { error = "Bad faces lump size"; return std::nullopt; }
        size_t n = sz/sizeof(Face);
        map.faces.resize(n);
        std::memcpy(map.faces.data(), p, sz);
    }

    // --- Parse texture info ---
    {
        auto [p,sz] = lumpSpan(LUMP_TEXINFO);
        if(sz % sizeof(TexInfo) != 0) { error = "Bad texinfo lump size"; return std::nullopt; }
        size_t n = sz/sizeof(TexInfo);
        map.texinfos.resize(n);
        std::memcpy(map.texinfos.data(), p, sz);
    }

    // --- Grab raw lightmap data (we don’t process yet, just keep bytes) ---
    {
        auto [p,sz] = lumpSpan(LUMP_LIGHTING);
        map.lighting.assign(p, p+sz);
    }

    // --- Parse models (brush models, including worldspawn at index 0) ---
    {
        auto [p,sz] = lumpSpan(LUMP_MODELS);
        if(sz % sizeof(Model) == 0 && sz>0) {
            size_t n = sz/sizeof(Model);
            map.models.resize(n);
            std::memcpy(map.models.data(), p, sz);
        }
    }

    // --- Parse MipTex textures embedded in BSP ---
    {
        auto [p,sz] = lumpSpan(LUMP_MIPTEX);
        if(sz >= (int)sizeof(MipTexHeader)) {
            int32_t nummip = rd32(p+0);
            if(nummip < 0 || (size_t)(4 + 4*nummip) > sz) {
                // Malformed, ignore.
            } else {
                const uint8_t* offbase = p + 4;
                std::vector<int32_t> offsets(nummip);
                for(int i=0;i<nummip;i++) offsets[i] = rd32(offbase + 4*i);
                map.textures.resize(nummip);

                for(int i=0;i<nummip;i++) {
                    int32_t off = offsets[i];
                    if(off <= 0 || (size_t)off >= sz) {
                        // Texture might be external (WAD). Leave empty.
                        continue;
                    }
                    const uint8_t* tptr = p + off;
                    if((size_t)off + sizeof(MipTex) > sz) continue;

                    // Copy header
                    MipTex mt;
                    std::memcpy(&mt, tptr, sizeof(MipTex));

                    // Build our texture object
                    BSPTexture tex;
                    tex.name = std::string(mt.name, mt.name + strnlen(mt.name,16));
                    tex.width = mt.width;
                    tex.height = mt.height;

                    // Grab pixel indices if valid
                    if(tex.width>0 && tex.height>0) {
                        uint32_t lvl0ofs = mt.offsets[0];
                        if(lvl0ofs>0 && (size_t)off + lvl0ofs + tex.width*tex.height <= sz) {
                            const uint8_t* pix = tptr + lvl0ofs;
                            tex.indices.assign(pix, pix + tex.width*tex.height);
                        }
                    }
                    map.textures[i] = std::move(tex);
                }
            }
        }
    }

    // --- Load palette if provided (palette.lmp) ---
    if(!palettePath.empty()) {
        LoadPaletteLMP(palettePath, map.paletteRGB);
    }

    // Print some stats to stderr so we know what was parsed.
    std::cerr << "Parsed: verts=" << map.vertices.size()
          << " faces=" << map.faces.size()
          << " edges=" << map.edges.size()
          << " surfedges=" << map.surfedges.size()
          << " texinfos=" << map.texinfos.size()
          << " textures=" << map.textures.size() << "\n";

    // Finally, construct meshes so the renderer can draw right away.
    buildMeshes(map);

    return map;
}

} // namespace neoquake
