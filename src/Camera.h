#pragma once
#include <cmath>

struct Camera {
    float x=0.f, y=0.f, z=0.f;
    float yaw=0.f, pitch=0.f;

    // Build a column-major 4x4 projection matrix
    static void perspective(float fovY, float aspect, float zNear, float zFar, float out[16]);

    // Build a column-major 4x4 view matrix (look from yaw/pitch)
    void viewMatrix(float out[16]) const;

    // simple helpers
    void moveForward(float d);
    void moveRight(float d);
    void moveUp(float d);
};
