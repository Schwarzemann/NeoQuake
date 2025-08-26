// File: src/Camera_Move.cpp
#include "Camera.h"
#include <cmath>
#include <algorithm>

void Camera::moveForward(float d) {
    x += sinf(yaw) * d;   // walk in the facing direction (XZ plane)
    z -= cosf(yaw) * d;
}
void Camera::moveRight(float d) {
    x += cosf(yaw) * d;   // strafe perpendicular to facing
    z += sinf(yaw) * d;
}
void Camera::moveUp(float d) {
    y += d;               // simple lift (world up)
}

// ------------------------------
// Extra, opt-in movement helpers
// ------------------------------
// These are free functions that work with our existing Camera fields. No header changes needed
// unless you want to call them from other translation units

namespace camera_util {

// Move in camera-local space in one call: (forward, right, up).
// Feels nice for input code where you’ve already computed a frame delta.
inline void MoveLocal(Camera& cam, float forward, float right, float up) {
    if (forward != 0.0f) { cam.moveForward(forward); }
    if (right   != 0.0f) { cam.moveRight(right); }
    if (up      != 0.0f) { cam.moveUp(up); }
}

// World-space move — sometimes you want to “teleport” by axes rather than camera space.
inline void MoveWorld(Camera& cam, float dx, float dy, float dz) {
    cam.x += dx; cam.y += dy; cam.z += dz;
}

// Rotate camera by deltas and clamp pitch to avoid flipping (gimbal guard).
inline void RotateYawPitch(Camera& cam, float dYaw, float dPitch,
                           float minPitch = -1.55f, float maxPitch = 1.55f) {
    cam.yaw   += dYaw;
    cam.pitch += dPitch;
    cam.pitch = std::clamp(cam.pitch, minPitch, maxPitch);
}

// Sugar: “back/left” without thinking in negatives.
inline void MoveBackward(Camera& cam, float d) { cam.moveForward(-d); }
inline void MoveLeft    (Camera& cam, float d) { cam.moveRight  (-d); }

// Snap to a position, or set both orientation angles at once.
inline void SetPosition(Camera& cam, float nx, float ny, float nz) {
    cam.x = nx; cam.y = ny; cam.z = nz;
}
inline void SetYawPitch(Camera& cam, float yaw, float pitch,
                        float minPitch = -1.55f, float maxPitch = 1.55f) {
    cam.yaw = yaw;
    cam.pitch = std::clamp(pitch, minPitch, maxPitch);
}

// Smooth damp for a single value — great for “weighty” camera motion.
// Returns the new value; velocity is updated in place.
inline float SmoothDamp(float current, float target, float& velocity, float smoothTime, float deltaTime) {
    // Basic critically-damped spring (same flavor you see in Unity).
    const float eps = 1e-6f;
    smoothTime = std::max(eps, smoothTime);
    float omega = 2.0f / smoothTime;

    float x = omega * deltaTime;
    float expTerm = 1.0f / (1.0f + x + 0.48f*x*x + 0.235f*x*x*x);

    float change = current - target;
    float temp = (velocity + omega * change) * deltaTime;
    velocity = (velocity - omega * temp) * expTerm;

    float result = target + (change + temp) * expTerm;
    return result;
}

} // namespace camera_util
