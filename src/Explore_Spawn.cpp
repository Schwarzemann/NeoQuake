#include "Explore.h"
#include <limits>
#include <cmath>
#include <algorithm>

namespace neoquake {

    // Compute a simple AABB of all map vertices.
    static void mapBounds(const BSPMap& map, Vec3& mn, Vec3& mx) {
        mn = { std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity() };
        mx = { -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity(),
               -std::numeric_limits<float>::infinity() };
        for (const auto& v : map.vertices) {
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
    }

    // Cheap “spawn somewhere sensible”:
    // 1) model[0].origin if present (typical worldspawn origin), a little above it.
    // 2) otherwise center of map bbox, a bit above the top.
    static void pickSpawn(const BSPMap& map, Vec3& pos, float& yaw) {
        if (!map.models.empty()) {
            pos = map.models[0].origin;
            pos.y += 64.f;
            yaw = 0.f; // 0 = look toward -Z in our math
            return;
        }
        if (!map.vertices.empty()) {
            Vec3 mn, mx; mapBounds(map, mn, mx);
            pos.x = 0.5f * (mn.x + mx.x);
            pos.z = 0.5f * (mn.z + mx.z);
            pos.y = mx.y + 64.f;
            yaw = 0.f;
            return;
        }
        // Fallback: origin
        pos = { 0.f, 64.f, 0.f };
        yaw = 0.f;
    }

    void Explore_Init(ExploreState& st, const BSPMap& map, ::Camera& cam, bool spawnInside) {
        st.enabled = spawnInside;
        st.velY = 0.f;

        if (spawnInside) {
            Vec3 p; float yaw = 0.f;
            pickSpawn(map, p, yaw);
            cam.x = p.x;
            cam.y = p.y + st.eyeHeight; // start with eye at a reasonable height
            cam.z = p.z;
            cam.yaw = yaw;
            cam.pitch = 0.f;
        }
    }

} // namespace neoquake
