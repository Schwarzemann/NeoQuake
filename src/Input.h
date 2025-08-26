#pragma once
#include <GLFW/glfw3.h>

// Camera lives in the global namespace
struct Camera;

namespace neoquake {

// Mouse-look state (relative mode)
struct MouseLook {
    bool   looking   = false;
    bool   haveLast  = false;  // <<< robust first-sample gate
    double lastX     = 0.0;
    double lastY     = 0.0;
    float  accumDx   = 0.0f;   // accumulated dx since last frame
    float  accumDy   = 0.0f;   // accumulated dy since last frame
};

// Input config you can tweak at runtime
struct InputConfig {
    float sensYaw    = 0.0009f;          // radians per pixel (tame default)
    float sensPitch  = 0.0009f;
    bool  invertPitch = false;           // P toggles
    float pitchLimit  = 1.5533430f;      // ~89 degrees
};

// All input state lives here; attach one per window
struct InputContext {
    MouseLook   mlook;
    InputConfig cfg;
    int  winW = 0;
    int  winH = 0;
    bool rawEnabled = false; // true if GLFW_RAW_MOUSE_MOTION is on
    bool ignoreNextCursorEvent = false;
};

// --- Setup / callbacks -------------------------------------------------------
void Input_Init(GLFWwindow* win, InputContext* ictx);        // sets user pointer + cursor-pos callback
void Input_OnResize(GLFWwindow* win, int width, int height); // updates cached size
void Input_CursorPosCallback(GLFWwindow* win, double xpos, double ypos); // installed by Input_Init

// --- Runtime controls --------------------------------------------------------
void Input_BeginLook(GLFWwindow* win, InputContext* ictx);   // disable cursor, reset deltas
void Input_EndLook(GLFWwindow* win, InputContext* ictx);     // enable cursor

void Input_UpdateMouseLook(InputContext* ictx, ::Camera& cam, float dt); // apply accum deltas to yaw/pitch
void Input_UpdateMovement(GLFWwindow* win, ::Camera& cam, float dt);     // WASD/Space/Ctrl + Shift speed

// Convenience tweaks
void Input_AdjustSensitivity(InputContext* ictx, float factor); // e.g., 0.98f or 1.02f
void Input_ToggleInvertPitch(InputContext* ictx);

} // namespace neoquake
