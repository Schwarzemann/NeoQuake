#include "Camera.h"
#include <cmath>
#include <algorithm>

// Perspective projection matrix (right-handed, row-major layout).
// Same math you already had; I’m just narrating what it does and leaving it intact.
// f = cot(fov/2). The 3rd row encodes the z mapping and the "−1" in [3][2] does the divide-by-w.
void Camera::perspective(float fovY, float aspect, float zNear, float zFar, float out[16]) {
    float f = 1.0f / tanf(0.5f * fovY);
    for (int i = 0; i < 16; i++) out[i] = 0.f;
    out[0]  = f / aspect;          // X scale
    out[5]  = f;                   // Y scale
    out[10] = (zFar + zNear) / (zNear - zFar);   // Z mapping
    out[11] = -1.f;                // Perspective divide trigger
    out[14] = (2.f * zFar * zNear) / (zNear - zFar);
}

// ------------------------------
// Extra, opt-in utilities
// ------------------------------
// These are free functions on purpose

namespace camera_util {

// Zero and identity fillers — tiny helpers so we don’t keep repeating ourselves.
static inline void matZero(float m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
}
static inline void matIdentity(float m[16]) {
    matZero(m);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// Row-major 4×4 multiply: out = a * b
static inline void matMul(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[c + 4 * r] =
                a[0 + 4 * r] * b[c + 0] +
                a[1 + 4 * r] * b[c + 4] +
                a[2 + 4 * r] * b[c + 8] +
                a[3 + 4 * r] * b[c + 12];
        }
    }
}

// Degrees version of the standard perspective.
// Handy when your configs are in degrees (most are).
inline void MakePerspectiveDegrees(float fovY_deg, float aspect, float zNear, float zFar, float out[16]) {
    const float rad = fovY_deg * (3.14159265358979323846f / 180.f);
    // Call the exact same code path you already rely on.
    Camera tmp;
    tmp.perspective(rad, aspect, zNear, zFar, out);
}

// Orthographic projection (centered). Useful for UI or debug views.
inline void MakeOrtho(float left, float right, float bottom, float top, float zNear, float zFar, float out[16]) {
    matZero(out);
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = zFar - zNear;
    if (std::abs(rl) < 1e-6f || std::abs(tb) < 1e-6f || std::abs(fn) < 1e-6f) {
        matIdentity(out); // fall back to something sane
        return;
    }
    out[0]  = 2.f / rl;
    out[5]  = 2.f / tb;
    out[10] = -2.f / fn;                 // right-handed
    out[12] = -(right + left) / rl;
    out[13] = -(top + bottom) / tb;
    out[14] = -(zFar + zNear) / fn;
    out[15] = 1.f;
}

// Off-center perspective frustum (a.k.a. "makeFrustum").
// If you ever want jittered projections for TAA or a reversed-eye portal, this is your friend.
inline void MakeFrustum(float left, float right, float bottom, float top, float zNear, float zFar, float out[16]) {
    matZero(out);
    const float rl = right - left;
    const float tb = top - bottom;
    const float fn = zFar - zNear;
    if (std::abs(rl) < 1e-6f || std::abs(tb) < 1e-6f || std::abs(fn) < 1e-6f) {
        matIdentity(out);
        return;
    }
    out[0]  = (2.f * zNear) / rl;
    out[5]  = (2.f * zNear) / tb;
    out[8]  = (right + left) / rl;
    out[9]  = (top + bottom) / tb;
    out[10] = -(zFar + zNear) / fn;    // right-handed
    out[11] = -1.f;
    out[14] = -(2.f * zFar * zNear) / fn;
}

// Convenience: build a perspective from jitter (sub-pixel offsets in NDC).
// Useful for TAA / stochastic sampling. jitterX/Y are in normalized device coords (−0.5..+0.5 typical).
inline void MakeJitteredPerspective(float fovY, float aspect, float zNear, float zFar,
                                    float jitterX, float jitterY, float out[16]) {
    float base[16];
    Camera c;
    c.perspective(fovY, aspect, zNear, zFar, base);

    // Translate in clip space by a tiny amount; we bake it into the projection.
    // Row-major: modifying the [2][0] and [2][1] equivalents is the easiest way to shift.
    // For small jitters this is perfectly fine.
    std::copy(base, base + 16, out);
    out[8]  += jitterX;  // affects x after perspective divide
    out[9]  += jitterY;  // affects y after perspective divide
}

} // namespace camera_util
