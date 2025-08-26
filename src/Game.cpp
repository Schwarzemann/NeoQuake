#include "Game.h"
#include <cmath>
#include <iostream>

namespace neoquake {

    static inline float deg2rad(float d) { return d * 3.1415926535f / 180.0f; }

    // Find an info_player_start and map its Quake angles to our Camera
    static void spawnFromEntities(const BSPMap& map, Player& out, Camera& cam) {
        for (auto& e : map.entities) {
            if (e.classname() == "info_player_start") {
                float sx = 0, sy = 0, sz = 0, ang = 0;
                if (auto s = e.find("origin")) {
                    // origin string is "x y z" in Quake (Z up)
                    std::sscanf(s, "%f %f %f", &sx, &sy, &sz);
                }
                if (auto a = e.find("angle")) { ang = std::atof(a); }

                // Quake axes: X,Y in plane, Z up. Our Camera uses Y up.
                // A simple way: keep positions as-is (we already load verts as x,y,z),
                // and treat Quake 'angle' as yaw around up-axis.
                out.x = sx; out.y = sz + 32.0f; out.z = sy; // lift a bit
                out.yaw = deg2rad(ang); // if facing backwards, use -ang

                cam.x = out.x; cam.y = out.y; cam.z = out.z;
                cam.yaw = out.yaw; cam.pitch = 0;
                std::cout << "Spawned at info_player_start: (" << out.x << "," << out.y << "," << out.z << ") yaw=" << out.yaw << "\n";
                return;
            }
        }
        // Fallback: world model origin or +Y offset
        if (!map.models.empty()) {
            cam.x = out.x = map.models[0].origin.x;
            cam.y = out.y = map.models[0].origin.y + 64.f;
            cam.z = out.z = map.models[0].origin.z;
        }
    }

    void Game_Init(Game& g, const BSPMap& map, Camera& cam) {
        spawnFromEntities(map, g.player, cam);
    }

    void Game_Update(Game& g, const BSPMap& /*map*/, Camera& cam, float /*dt*/) {
        // Mirror player -> camera for now (we’ll add proper physics later)
        cam.x = g.player.x; cam.y = g.player.y; cam.z = g.player.z;
        cam.yaw = g.player.yaw; cam.pitch = g.player.pitch;
    }

} // namespace neoquake
