#pragma once
#include "BSP.h"
#include "Camera.h"

namespace neoquake {

    struct Player {
        // Quake is Z-up; our view math is Y-up. We’ll keep using Camera (Y-up) and
        // just map spawn angles sensibly.
        float x = 0, y = 64, z = 0;  // position in BSP world units
        float yaw = 0, pitch = 0;  // radians
        bool noclip = false;
    };

    struct Game {
        bool   running = true;
        Player player;
    };

    void Game_Init(Game& g, const BSPMap& map, Camera& cam);
    void Game_Update(Game& g, const BSPMap& map, Camera& cam, float dt);

} // namespace neoquake
