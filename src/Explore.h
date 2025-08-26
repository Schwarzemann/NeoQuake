#pragma once
#include <GLFW/glfw3.h>
#include "BSP.h"
#include "Camera.h" // ::Camera

namespace neoquake {

    // Small state bag for exploration.
    struct ExploreState {
        bool  enabled = false;   // explore mode on/off
        bool  noclip = false;   // fly through walls/floor
        float walkSpeed = 220.f;   // units/s
        float runMult = 1.8f;    // SHIFT multiplier
        float flySpeed = 320.f;   // when noclip
        float eyeHeight = 56.f;    // camera eye above the ground
        float gravity = 800.f;   // units/s^2 downward
        float velY = 0.f;     // vertical velocity when not noclip
    };

    // API
    void Explore_Init(ExploreState& st, const BSPMap& map, ::Camera& cam, bool spawnInside);
    void Explore_Update(GLFWwindow* win, const BSPMap& map, ExploreState& st, ::Camera& cam, float dt);

} // namespace neoquake
