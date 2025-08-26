#include "Explore.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <iostream>

namespace neoquake {

    // Very small 3D helper
    struct V3 { float x, y, z; };

    static inline V3 v3(float x, float y, float z) { return { x,y,z }; }
    static inline V3 add(const V3& a, const V3& b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
    static inline V3 sub(const V3& a, const V3& b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
    static inline V3 mul(const V3& a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
    static inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static inline V3 cross(const V3& a, const V3& b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }

    // Ray (origin, dir) vs triangle (v0,v1,v2). Returns t>0 if hit.
    static bool rayTri(const V3& ro, const V3& rd, const V3& v0, const V3& v1, const V3& v2, float& tOut) {
        // Möller–Trumbore
        const float EPS = 1e-6f;
        V3 e1 = sub(v1, v0);
        V3 e2 = sub(v2, v0);
        V3 p = cross(rd, e2);
        float det = dot(e1, p);
        if (std::fabs(det) < EPS) return false;
        float invDet = 1.0f / det;
        V3 tvec = sub(ro, v0);
        float u = dot(tvec, p) * invDet;
        if (u < 0.f || u > 1.f) return false;
        V3 qvec = cross(tvec, e1);
        float v = dot(rd, qvec) * invDet;
        if (v < 0.f || (u + v) > 1.f) return false;
        float t = dot(e2, qvec) * invDet;
        if (t <= EPS) return false;
        tOut = t;
        return true;
    }

    // Cast a vertical ray straight down from (x, y, z) and find the nearest floor.
    // Returns true if something was hit; outY is the Y coordinate of the hit.
    static bool raycastDownToFloor(const BSPMap& map, float x, float y, float z, float maxDist, float& outY) {
        V3 ro = v3(x, y, z);
        V3 rd = v3(0.f, -1.f, 0.f);

        float bestT = std::numeric_limits<float>::infinity();
        bool  found = false;

        for (const auto& m : map.meshes) {
            const auto& v = m.vertices;
            if (v.size() < 15) continue; // at least one triangle (3 * 5 floats)
            for (size_t i = 0; i + 14 < v.size(); i += 15) {
                V3 v0 = v3(v[i + 0], v[i + 1], v[i + 2]);
                V3 v1 = v3(v[i + 5], v[i + 6], v[i + 7]);
                V3 v2 = v3(v[i + 10], v[i + 11], v[i + 12]);
                float t;
                if (rayTri(ro, rd, v0, v1, v2, t)) {
                    if (t < bestT && t <= maxDist) {
                        bestT = t;
                        found = true;
                    }
                }
            }
        }
        if (found) {
            outY = y - bestT; // rd is downward (0,-1,0)
        }
        return found;
    }

    // Movement helpers
    static inline float clampf(float v, float lo, float hi) {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    void Explore_Update(GLFWwindow* win, const BSPMap& map, ExploreState& st, ::Camera& cam, float dt) {
        // Toggle explore on/off with E (optional convenience)
        static int prevE = GLFW_RELEASE;
        int eNow = glfwGetKey(win, GLFW_KEY_E);
        if (eNow == GLFW_PRESS && prevE == GLFW_RELEASE) {
            st.enabled = !st.enabled;
            std::cout << "[Explore] " << (st.enabled ? "Enabled\n" : "Disabled\n");
        }
        prevE = eNow;

        // Toggle noclip with N
        static int prevN = GLFW_RELEASE;
        int nNow = glfwGetKey(win, GLFW_KEY_N);
        if (nNow == GLFW_PRESS && prevN == GLFW_RELEASE) {
            st.noclip = !st.noclip;
            std::cout << "[Explore] noclip: " << (st.noclip ? "ON\n" : "OFF\n");
            st.velY = 0.f;
        }
        prevN = nNow;

        if (!st.enabled) return;
        if (dt <= 0.f) return;

        // Base speeds
        bool running = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
        float moveSpeed = (st.noclip ? st.flySpeed : st.walkSpeed) * (running ? st.runMult : 1.f);

        // Movement input in camera-local space
        float fwd = 0.f, str = 0.f, up = 0.f;
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) fwd += 1.f;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) fwd -= 1.f;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) str += 1.f;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) str -= 1.f;

        if (st.noclip) {
            if (glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS)        up += 1.f;
            if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) up -= 1.f;
        }

        // Compute world-space move vector from yaw
        float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
        V3 forward = v3(sy, 0.f, -cy);      // matches Camera::moveForward()
        V3 right = v3(cy, 0.f, sy);      // matches Camera::moveRight()

        V3 move = add(mul(forward, fwd), mul(right, str));
        if (st.noclip) move = add(move, v3(0.f, up, 0.f));

        // Normalize to prevent faster diagonal
        float len = std::sqrt(std::max(1e-6f, move.x * move.x + move.y * move.y + move.z * move.z));
        move = mul(move, 1.0f / len);

        // Apply horizontal move
        float step = moveSpeed * dt;
        cam.x += move.x * step;
        cam.z += move.z * step;

        if (st.noclip) {
            cam.y += move.y * step;
            return; // no gravity
        }

        // Gravity + floor follow
        st.velY -= st.gravity * dt;
        cam.y += st.velY * dt;

        // Keep the eye locked to the floor with a ray cast
        float groundY;
        if (raycastDownToFloor(map, cam.x, cam.y + 32.f, cam.z, 4096.f, groundY)) {
            float targetY = groundY + st.eyeHeight;
            if (cam.y < targetY) {
                cam.y = targetY;
                st.velY = 0.f; // landed
            }
        }
    }

} // namespace neoquake
