#include "BSP.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace neoquake {

    // Quantize to Quake lightmap space (16 texel “luxel” blocks)
    static inline int qfloor16(float s) { return (int)std::floor(s / 16.0f) * 16; }
    static inline int qceil16(float s) { return (int)std::ceil(s / 16.0f) * 16; }

    // Compute S,T (texture space) for a world point using TexInfo
    static inline void computeST(const Vec3& p, const TexInfo& ti, float& s, float& t) {
        s = p.x * ti.s[0] + p.y * ti.s[1] + p.z * ti.s[2] + ti.s[3];
        t = p.x * ti.t[0] + p.y * ti.t[1] + p.z * ti.t[2] + ti.t[3];
    }

    // For one face, compute lightmap mins/size and return (w,h, mins[2])
    // Output: w/h in "luxels", and mins aligned to 16s.
    static void faceLightmapExtents(const BSPMap& map, int faceIndex,
        int& outW, int& outH, float& outSminAligned, float& outTminAligned)
    {
        const Face& f = map.faces[faceIndex];
        const TexInfo& ti = map.texinfos[f.texinfo];

        // Collect face vertices in polygon order (same as buildMeshes did)
        std::vector<int> vind;
        vind.reserve(f.numedges);
        for (int i = 0; i < f.numedges; ++i) {
            int se = map.surfedges[f.firstedge + i];
            if (se >= 0) {
                const Edge& e = map.edges[se];
                vind.push_back(e.v0);
            }
            else {
                const Edge& e = map.edges[-se];
                vind.push_back(e.v1);
            }
        }
        if (vind.size() < 3) { outW = outH = 0; outSminAligned = outTminAligned = 0; return; }

        // Find S/T bounds across the polygon
        float smin = 1e30f, tmin = 1e30f, smax = -1e30f, tmax = -1e30f;
        for (int vi : vind) {
            const Vec3& p = map.vertices[vi];
            float s, t; computeST(p, ti, s, t);
            smin = std::min(smin, s); smax = std::max(smax, s);
            tmin = std::min(tmin, t); tmax = std::max(tmax, t);
        }

        // Align to 16s, compute luxel dims = (extents/16)+1
        float sminA = (float)qfloor16(smin);
        float tminA = (float)qfloor16(tmin);
        float smaxA = (float)qceil16(smax);
        float tmaxA = (float)qceil16(tmax);

        int extS = (int)(smaxA - sminA);
        int extT = (int)(tmaxA - tminA);

        int smaxLux = extS / 16;
        int tmaxLux = extT / 16;

        outW = std::max(1, smaxLux + 1);
        outH = std::max(1, tmaxLux + 1);
        outSminAligned = sminA;
        outTminAligned = tminA;
    }

    // Stupid-simple shelf packer (good enough for Q1 maps)
    struct ShelfPacker {
        int W = 0, H = 0, x = 0, y = 0, shelfH = 0;
        ShelfPacker(int w, int h) : W(w), H(h), x(0), y(0), shelfH(0) {}
        bool place(int w, int h, int& outx, int& outy) {
            if (w > W || h > H) return false;
            if (x + w > W) { // new shelf
                y += shelfH; x = 0; shelfH = 0;
            }
            if (y + h > H) return false;
            outx = x; outy = y;
            x += w; shelfH = std::max(shelfH, h);
            return true;
        }
    };

    // Build a single RGBA atlas, compute per-face LM UVs, and store them in meshes
    void BuildLightmaps(BSPMap& map)
    {
        if (map.faces.empty() || map.meshes.size() != map.faces.size()) {
            // we rely on 1:1 mesh:face
            std::cerr << "[LM] No faces or meshes mismatch; skipping lightmaps.\n";
            return;
        }

        // 1) Gather per-face LM sizes + mins
        struct FaceLM { int w = 0, h = 0; float sminA = 0, tminA = 0; bool hasLM = false; };
        std::vector<FaceLM> faceLM(map.faces.size());
        int totalArea = 0;
        for (size_t fi = 0; fi < map.faces.size(); ++fi) {
            const Face& f = map.faces[fi];
            FaceLM info{};
            if (f.lightofs >= 0 && (size_t)f.lightofs < map.lighting.size()) {
                faceLightmapExtents(map, (int)fi, info.w, info.h, info.sminA, info.tminA);
                if (info.w > 0 && info.h > 0) {
                    info.hasLM = true;
                    totalArea += info.w * info.h;
                }
            }
            faceLM[fi] = info;
        }

        if (totalArea == 0) {
            std::cerr << "[LM] No valid lightmaps found; leaving fullbright.\n";
            map.lmAtlas = {}; // nothing
            // But still add dummy u1,v1 = 0,0 so stride is 7
            for (auto& m : map.meshes) {
                m.vertices.reserve(m.vertices.size() / 5 * 7);
                std::vector<float> nv; nv.reserve(m.vertices.size() / 5 * 7);
                for (size_t i = 0; i < m.vertices.size(); i += 5) {
                    nv.push_back(m.vertices[i + 0]);
                    nv.push_back(m.vertices[i + 1]);
                    nv.push_back(m.vertices[i + 2]);
                    nv.push_back(m.vertices[i + 3]);
                    nv.push_back(m.vertices[i + 4]);
                    nv.push_back(0.0f); // u1
                    nv.push_back(0.0f); // v1
                }
                m.vertices.swap(nv);
            }
            return;
        }

        // 2) Decide atlas size (power-of-two-ish). Start at 1024 and grow if needed.
        int atlasW = 1024, atlasH = 1024;
        for (;;) {
            ShelfPacker pack(atlasW, atlasH);
            bool ok = true;
            for (size_t fi = 0; fi < map.faces.size(); ++fi) {
                if (!faceLM[fi].hasLM) continue;
                int x, y;
                if (!pack.place(faceLM[fi].w, faceLM[fi].h, x, y)) { ok = false; break; }
            }
            if (ok) break;
            // grow: (simple) double the smallest dimension
            if (atlasW <= atlasH) atlasW *= 2; else atlasH *= 2;
            if (atlasW > 8192 || atlasH > 8192) { // safety
                std::cerr << "[LM] Atlas grew too large; aborting lightmaps.\n";
                map.lmAtlas = {};
                return;
            }
        }

        // 3) Actually pack and store rects
        ShelfPacker pack(atlasW, atlasH);
        map.lmAtlas.perFace.resize(map.faces.size());
        for (size_t fi = 0; fi < map.faces.size(); ++fi) {
            auto& outR = map.lmAtlas.perFace[fi];
            const FaceLM& inf = faceLM[fi];
            if (!inf.hasLM) { outR.valid = false; continue; }
            int x, y; bool placed = pack.place(inf.w, inf.h, x, y);
            if (!placed) { outR.valid = false; continue; } // shouldn't happen after sizing loop
            outR.x = x; outR.y = y; outR.w = inf.w; outR.h = inf.h;
            outR.lightofs = map.faces[fi].lightofs;
            outR.valid = true;
        }

        // 4) Allocate RGBA atlas (grayscale into RGB, alpha=255)
        map.lmAtlas.width = atlasW;
        map.lmAtlas.height = atlasH;
        map.lmAtlas.rgba.assign((size_t)atlasW * (size_t)atlasH * 4, 255);

        // 5) Copy each face’s raw BSP light bytes into the atlas block
        for (size_t fi = 0; fi < map.faces.size(); ++fi) {
            const auto& rect = map.lmAtlas.perFace[fi];
            const auto& inf = faceLM[fi];
            if (!rect.valid) continue;

            // BSP stores one byte per luxel (Quake 1). Multiple styles are ignored here (first style only).
            size_t ofs = (size_t)map.faces[fi].lightofs;
            size_t faceBytes = (size_t)rect.w * (size_t)rect.h;
            if (ofs + faceBytes > map.lighting.size()) {
                // malformed; fill white block
                for (int y = 0; y < rect.h; y++) {
                    for (int x = 0; x < rect.w; x++) {
                        size_t a = ((size_t)(rect.y + y) * atlasW + (size_t)(rect.x + x)) * 4;
                        map.lmAtlas.rgba[a + 0] = 255;
                        map.lmAtlas.rgba[a + 1] = 255;
                        map.lmAtlas.rgba[a + 2] = 255;
                        map.lmAtlas.rgba[a + 3] = 255;
                    }
                }
                continue;
            }

            for (int y = 0; y < rect.h; y++) {
                for (int x = 0; x < rect.w; x++) {
                    uint8_t v = map.lighting[ofs + (size_t)y * rect.w + (size_t)x];
                    size_t a = ((size_t)(rect.y + y) * atlasW + (size_t)(rect.x + x)) * 4;
                    map.lmAtlas.rgba[a + 0] = v;
                    map.lmAtlas.rgba[a + 1] = v;
                    map.lmAtlas.rgba[a + 2] = v;
                    map.lmAtlas.rgba[a + 3] = 255;
                }
            }
        }

        // 6) Add the SECOND UV set (u1,v1) to every mesh vertex (stride -> 7)
        for (size_t fi = 0; fi < map.faces.size(); ++fi) {
            auto& mesh = map.meshes[fi];
            mesh.faceIndex = (int)fi;

            // Expand 5f -> 7f by recomputing S/T and mapping into atlas rect
            std::vector<float> nv; nv.reserve(mesh.vertices.size() / 5 * 7);

            // retrieve extents for this face (even if !hasLM, we still compute u1,v1=0)
            float sminA = 0, tminA = 0;
            int w = 1, h = 1;
            if (faceLM[fi].hasLM) {
                sminA = faceLM[fi].sminA;
                tminA = faceLM[fi].tminA;
                w = faceLM[fi].w; h = faceLM[fi].h;
            }
            const auto& rect = map.lmAtlas.perFace[fi];
            const TexInfo& ti = map.texinfos[map.faces[fi].texinfo];

            // Walk triangles already generated (in pos/uv0 format). We need 'pos' to recompute S/T.
            for (size_t i = 0; i < mesh.vertices.size(); i += 5) {
                float px = mesh.vertices[i + 0];
                float py = mesh.vertices[i + 1];
                float pz = mesh.vertices[i + 2];
                float u0 = mesh.vertices[i + 3];
                float v0 = mesh.vertices[i + 4];

                // original point
                Vec3 p{ px,py,pz };

                // S/T for lightmap, convert to luxel space and into atlas normalized coords
                float s, t; computeST(p, ti, s, t);

                float ls = (s - sminA) / 16.0f;  // 0..w-1
                float lt = (t - tminA) / 16.0f;  // 0..h-1

                float u1 = 0.0f, v1 = 0.0f;
                if (rect.valid && w > 0 && h > 0) {
                    // +0.5f to sample center of texel; divide by atlas size
                    u1 = ((rect.x + ls + 0.5f) / (float)atlasW);
                    v1 = ((rect.y + lt + 0.5f) / (float)atlasH);
                    // Note: no flip; your diffuse v was flipped for GL earlier; lightmap follows same rule here
                }

                nv.push_back(px); nv.push_back(py); nv.push_back(pz);
                nv.push_back(u0); nv.push_back(v0);
                nv.push_back(u1); nv.push_back(v1);
            }

            mesh.vertices.swap(nv);
        }

        std::cerr << "[LM] Built atlas " << atlasW << "x" << atlasH << " for " << map.faces.size() << " faces.\n";
    }

} // namespace neoquake
