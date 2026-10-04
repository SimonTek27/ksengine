#pragma once
// Qt-free vector/matrix math for ksengine.
//
// ksengine (src/engine) must be a pure library — graphics, audio, physics,
// devices, network — with zero trace of Qt; Qt is used only by kseditor.exe
// for its desktop GUI. This is the math primitives layer that makes that
// possible: plain structs, no QVector3D/QMatrix4x4, usable identically from
// kseditor (Qt) and SimulatorApp/SimulatorAppServer (Qt-free).
//
// Originally written as ks::sim::vec2/vec3/vec4/quat/mat4 in
// src/simulator/MathTypes.h (proven working there: ShadowSystem,
// NativeRenderer, TerrainMesh, SimulationLoop all build on it). Promoted
// here, into ks::math, as the canonical engine-owned copy; MathTypes.h now
// just aliases these types into ks::sim for source compatibility with that
// existing code, instead of holding a second, divergent copy.
#include <cmath>
#include <cstring>

namespace ks::math {

struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    vec2(float x, float y) : x(x), y(y) {}
};

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    vec3 operator+(const vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    vec3 operator-(const vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3& operator+=(const vec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    vec3 normalized() const { float l = length(); return l > 0 ? vec3{x/l, y/l, z/l} : vec3{}; }
    float dot(const vec3& o) const { return x*o.x + y*o.y + z*o.z; }
    vec3 cross(const vec3& o) const { return {y*o.z-z*o.y, z*o.x-x*o.z, x*o.y-y*o.x}; }
};

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    vec4(vec3 v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
};

struct quat {
    float x = 0, y = 0, z = 0, w = 1;
    quat() = default;
    quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    static quat fromAxisAngle(vec3 axis, float angle) {
        float half = angle * 0.5f;
        float s = std::sin(half);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
    }
    vec3 rotatedVec(vec3 v) const {
        vec3 qv{x, y, z};
        vec3 uv = qv.cross(v);
        vec3 uuv = qv.cross(uv);
        return v + (uv * w + uuv) * 2.0f;
    }
    quat operator*(const quat& o) const {
        return {
            w*o.x + x*o.w + y*o.z - z*o.y,
            w*o.y - x*o.z + y*o.w + z*o.x,
            w*o.z + x*o.y - y*o.x + z*o.w,
            w*o.w - x*o.x - y*o.y - z*o.z
        };
    }
};

struct mat4 {
    float m[16] = {};

    mat4() {
        m[0]=1; m[5]=1; m[10]=1; m[15]=1;
    }

    float& operator()(int row, int col) { return m[col * 4 + row]; }
    float operator()(int row, int col) const { return m[col * 4 + row]; }

    vec3 column(int i) const { return {m[i*4], m[i*4+1], m[i*4+2]}; }
    void setColumn(int i, vec3 v) { m[i*4]=v.x; m[i*4+1]=v.y; m[i*4+2]=v.z; }
    void setColumn(int i, vec4 v) { m[i*4]=v.x; m[i*4+1]=v.y; m[i*4+2]=v.z; m[i*4+3]=v.w; }

    static mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
        vec3 f = (center - eye).normalized();
        vec3 s = f.cross(up).normalized();
        vec3 u = s.cross(f);
        mat4 r;
        r(0,0)=s.x;  r(0,1)=s.y;  r(0,2)=s.z;  r(0,3)=-s.dot(eye);
        r(1,0)=u.x;  r(1,1)=u.y;  r(1,2)=u.z;  r(1,3)=-u.dot(eye);
        r(2,0)=-f.x; r(2,1)=-f.y; r(2,2)=-f.z; r(2,3)=f.dot(eye);
        r(3,0)=0;    r(3,1)=0;    r(3,2)=0;    r(3,3)=1;
        return r;
    }

    // Vulkan/D3D clip space: NDC z spans [0,1] (not OpenGL's [-1,1]) — the
    // renderer this feeds is Vulkan-only (gl_FragCoord.z is [0,1] there too),
    // so an OpenGL-style projection would push half the depth range through a
    // clamped viewport and desynchronise every depth comparison — shadow
    // tests included. NDC y also points downward in Vulkan (y = -1 is the top
    // of the framebuffer), so the y scale is negated: without it the image is
    // mirrored vertically and screen-space winding flips, which makes
    // VK_FRONT_FACE_COUNTER_CLOCKWISE + back-face culling discard front faces.
    static mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        float tanHalf = std::tan(fovY * 0.5f);
        mat4 r;
        r(0,0) = 1.0f / (aspect * tanHalf);
        r(1,1) = -1.0f / tanHalf;
        r(2,2) = -zFar / (zFar - zNear);
        r(2,3) = -(zFar * zNear) / (zFar - zNear);
        r(3,2) = -1.0f;
        r(3,3) = 0;
        return r;
    }

    // Orthographic projection (needed for directional-light / cascaded
    // shadow-map frustums, which don't converge to a point like perspective).
    // Same Vulkan [0,1] depth convention as perspective() above.
    // y row negated to match perspective(): in Vulkan NDC y = -1 is the top
    // of the framebuffer, so world-space `top` must map to -1 and `bottom`
    // to +1. Shadow cascades build their light matrix with this, and both the
    // depth pass and the shadow lookup use the same matrix, so the mirroring
    // stays self-consistent while the winding stays front-facing.
    static mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        mat4 r;
        memset(r.m, 0, sizeof(r.m));
        r(0,0) = 2.0f / (right - left);
        r(1,1) = -2.0f / (top - bottom);
        r(2,2) = -1.0f / (zFar - zNear);
        r(0,3) = -(right + left) / (right - left);
        r(1,3) = (top + bottom) / (top - bottom);
        r(2,3) = -zNear / (zFar - zNear);
        r(3,3) = 1.0f;
        return r;
    }

    // General 4x4 inverse (Gauss-Jordan with partial pivoting). Used to
    // reconstruct camera-frustum corners in world space from view*proj when
    // fitting shadow cascades. Returns the identity if the matrix is singular.
    mat4 inverse() const {
        float a[4][8];
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) a[row][col] = (*this)(row, col);
            for (int col = 0; col < 4; ++col) a[row][4 + col] = (row == col) ? 1.0f : 0.0f;
        }
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            float best = std::fabs(a[col][col]);
            for (int row = col + 1; row < 4; ++row) {
                float v = std::fabs(a[row][col]);
                if (v > best) { best = v; pivot = row; }
            }
            if (best < 1e-8f) return mat4();
            if (pivot != col) { for (int k = 0; k < 8; ++k) { float t = a[col][k]; a[col][k] = a[pivot][k]; a[pivot][k] = t; } }
            float diag = a[col][col];
            for (int k = 0; k < 8; ++k) a[col][k] /= diag;
            for (int row = 0; row < 4; ++row) {
                if (row == col) continue;
                float factor = a[row][col];
                for (int k = 0; k < 8; ++k) a[row][k] -= factor * a[col][k];
            }
        }
        mat4 r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                r(row, col) = a[row][4 + col];
        return r;
    }

    mat4 operator*(const mat4& o) const {
        mat4 r;
        memset(r.m, 0, sizeof(r.m));
        for (int c = 0; c < 4; c++)
            for (int row = 0; row < 4; row++)
                for (int k = 0; k < 4; k++)
                    r(row, c) += (*this)(row, k) * o(k, c);
        return r;
    }

    vec3 operator*(vec3 v) const {
        return {
            m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12],
            m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13],
            m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]
        };
    }

    vec4 operator*(vec4 v) const {
        return {
            m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w,
            m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w,
            m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w,
            m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w
        };
    }

    const float* data() const { return m; }

    static mat4 translation(vec3 t) {
        mat4 r;
        r(0,3) = t.x; r(1,3) = t.y; r(2,3) = t.z;
        return r;
    }

    // Builds a world matrix from a position and roll/pitch/yaw Euler angles
    // (radians), matching the (roll, pitch, yaw) field order already used by
    // ks::physics::MotionState/SimulationState elsewhere in this codebase.
    // Rotation order: Ry(yaw) * Rx(pitch) * Rz(roll), Y-up world.
    static mat4 fromPositionRollPitchYaw(vec3 pos, float roll, float pitch, float yaw) {
        float cy = std::cos(yaw),   sy = std::sin(yaw);
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cr = std::cos(roll),  sr = std::sin(roll);

        mat4 ry; ry(0,0)=cy; ry(0,2)=sy; ry(2,0)=-sy; ry(2,2)=cy;
        mat4 rx; rx(1,1)=cp; rx(1,2)=-sp; rx(2,1)=sp; rx(2,2)=cp;
        mat4 rz; rz(0,0)=cr; rz(0,1)=-sr; rz(1,0)=sr; rz(1,1)=cr;

        mat4 r = ry * rx * rz;
        r(0,3) = pos.x; r(1,3) = pos.y; r(2,3) = pos.z;
        return r;
    }
};

} // namespace ks::math
