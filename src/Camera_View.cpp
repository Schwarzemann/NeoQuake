#include "Camera.h"
#include <cmath>
#include <algorithm>

// These small matrix helpers stay local to this translation unit. They do exactly what
// their names say — no magic, no macros.
static void matIdentity(float m[16]) {
    for (int i = 0; i < 16; i++) m[i] = 0.f;
    m[0] = m[5] = m[10] = m[15] = 1.f;
}
static void matMul(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
        out[c + 4 * r] =
            a[0 + 4 * r] * b[c + 0] +
            a[1 + 4 * r] * b[c + 4] +
            a[2 + 4 * r] * b[c + 8] +
            a[3 + 4 * r] * b[c + 12];
    }
}
static void matTranslate(float tx, float ty, float tz, float out[16]) {
    matIdentity(out);
    out[12] = tx; out[13] = ty; out[14] = tz;
}
static void matRotateY(float a, float out[16]) {
    float c = cosf(a), s = sinf(a);
    matIdentity(out);
    out[0] =  c; out[2] =  s;
    out[8] = -s; out[10] = c;
}
static void matRotateX(float a, float out[16]) {
    float c = cosf(a), s = sinf(a);
    matIdentity(out);
    out[5] =  c; out[6] =  s;
    out[9] = -s; out[10] = c;
}

// Rotate (yaw, pitch), then translate.
void Camera::viewMatrix(float out[16]) const {
    float rx[16], ry[16], t[16], tmp[16];
    matRotateY(-yaw, ry);
    matRotateX(-pitch, rx);
    matTranslate(-x, -y, -z, t);
    matMul(rx, t, tmp);
    matMul(ry, tmp, out);
}

// ------------------------------
// Extra, opt-in utilities
// ------------------------------
namespace camera_util {

// Turn yaw/pitch into basis vectors. This is the “what direction is forward/right/up?” helper.
// Forward convention here matches your view: +pitch looks up, +yaw turns right.
inline void BasisFromYawPitch(float yaw, float pitch,
                              float outFwd[3], float outRight[3], float outUp[3]) {
    // Forward first
    const float cp = cosf(pitch), sp = sinf(pitch);
    const float cy = cosf(yaw),   sy = sinf(yaw);

    // With your rotations (yaw around Y, then pitch around X), a consistent forward is:
    //   f = ( sin(yaw)*cos(pitch),  sin(pitch),  -cos(yaw)*cos(pitch) )
    const float fwd[3] = { sy * cp, sp, -cy * cp };

    // Right is the yaw-only right vector (on XZ plane)
    const float right[3] = { cy, 0.0f, sy };

    // Up = right × forward
    const float up[3] = {
        right[1]*fwd[2] - right[2]*fwd[1],
        right[2]*fwd[0] - right[0]*fwd[2],
        right[0]*fwd[1] - right[1]*fwd[0]
    };

    if (outFwd)   { outFwd[0] = fwd[0];   outFwd[1] = fwd[1];   outFwd[2] = fwd[2]; }
    if (outRight) { outRight[0] = right[0]; outRight[1] = right[1]; outRight[2] = right[2]; }
    if (outUp)    { outUp[0] = up[0];     outUp[1] = up[1];     outUp[2] = up[2]; }
}

// Make a view matrix directly from yaw/pitch/pos without touching Camera.
// Mirrors Camera::viewMatrix exactly (handy for tools/tests).
inline void MakeViewYawPitchPos(float yaw, float pitch, float x, float y, float z, float out[16]) {
    float rx[16], ry[16], t[16], tmp[16];
    matRotateY(-yaw, ry);
    matRotateX(-pitch, rx);
    matTranslate(-x, -y, -z, t);
    matMul(rx, t, tmp);
    matMul(ry, tmp, out);
}

// Classic lookAt (row-major, right-handed). Doesn’t modify Camera; just gives you the matrix.
// Useful for cutscenes or debug cameras that aren’t driven by yaw/pitch.
inline void MakeLookAt(const float eye[3], const float target[3], const float upHint[3], float out[16]) {
    // Build orthonormal basis
    float f[3] = { target[0]-eye[0], target[1]-eye[1], target[2]-eye[2] };
    float fl = std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); if (fl < 1e-6f) fl = 1.f;
    f[0]/=fl; f[1]/=fl; f[2]/=fl;

    float up[3] = { upHint[0], upHint[1], upHint[2] };
    float ul = std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]); if (ul < 1e-6f) { up[0]=0; up[1]=1; up[2]=0; } else { up[0]/=ul; up[1]/=ul; up[2]/=ul; }

    // Right = up × fwd (right-handed)
    float r[3] = {
        up[1]*f[2] - up[2]*f[1],
        up[2]*f[0] - up[0]*f[2],
        up[0]*f[1] - up[1]*f[0]
    };
    float rl = std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]); if (rl < 1e-6f) rl = 1.f;
    r[0]/=rl; r[1]/=rl; r[2]/=rl;

    // Recompute up to ensure orthogonality: up = f × r
    up[0] = f[1]*r[2] - f[2]*r[1];
    up[1] = f[2]*r[0] - f[0]*r[2];
    up[2] = f[0]*r[1] - f[1]*r[0];

    // Row-major view = R * T
    float R[16]; matIdentity(R);
    R[0]=r[0]; R[1]=r[1]; R[2]=r[2];
    R[4]=up[0]; R[5]=up[1]; R[6]=up[2];
    R[8]=-f[0]; R[9]=-f[1]; R[10]=-f[2];

    float T[16]; matTranslate(-eye[0], -eye[1], -eye[2], T);
    matMul(R, T, out);
}

// Compose a view-projection: VP = P * V (row-major convention used here).
inline void MakeViewProjection(const float proj[16], const float view[16], float outVP[16]) {
    matMul(proj, view, outVP);
}

// Invert a rigid transform (rotation + translation, no scale/shear). Handy for camera <-> world hops.
inline void InvertRigid(const float m[16], float out[16]) {
    // Extract rotation (upper-left 3×3) and transpose it
    float R[9] = { m[0], m[1], m[2],
                   m[4], m[5], m[6],
                   m[8], m[9], m[10] };
    float Rt[9] = { R[0], R[3], R[6],
                    R[1], R[4], R[7],
                    R[2], R[5], R[8] };

    // Translation
    float t[3] = { m[12], m[13], m[14] };

    // out = [ Rt  -Rt*t ]
    matIdentity(out);
    out[0]=Rt[0]; out[1]=Rt[1]; out[2]=Rt[2];
    out[4]=Rt[3]; out[5]=Rt[4]; out[6]=Rt[5];
    out[8]=Rt[6]; out[9]=Rt[7]; out[10]=Rt[8];

    out[12] = -(Rt[0]*t[0] + Rt[3]*t[1] + Rt[6]*t[2]);
    out[13] = -(Rt[1]*t[0] + Rt[4]*t[1] + Rt[7]*t[2]);
    out[14] = -(Rt[2]*t[0] + Rt[5]*t[1] + Rt[8]*t[2]);
}

} // namespace camera_util
