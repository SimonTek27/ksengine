#pragma once
#include <cmath>
#include <cstring>

namespace ks::sim {

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

    // Vulkan/D3D clip space: NDC z spans [0,1], and — unlike OpenGL — NDC
    // y = -1 maps to the *top* of the framebuffer (the viewport transform
    // sends y = +1 to y0 + height, i.e. downward). A GL-style projection is
    // therefore rendered upside down here, which also mirrors screen-space
    // winding and makes VK_FRONT_FACE_COUNTER_CLOCKWISE treat every front
    // face as a back face (single-sided geometry such as ground quads
    // disappears under VK_CULL_MODE_BACK_BIT). Negating the y scale fixes
    // both: image orientation and winding.
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

    // Same y convention as perspective(): top of the box must land on
    // NDC y = -1, bottom on NDC y = +1 (both y coefficients negated).
    static mat4 ortho(float left, float right, float bottom, float top,
                      float zNear, float zFar) {
        mat4 r;
        r(0,0) = 2.0f / (right - left);
        r(1,1) = -2.0f / (top - bottom);
        r(2,2) = -1.0f / (zFar - zNear);
        r(0,3) = -(right + left) / (right - left);
        r(1,3) = (top + bottom) / (top - bottom);
        r(2,3) = -zNear / (zFar - zNear);
        r(3,3) = 1.0f;
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

    /** General 4x4 inverse via cofactor expansion (adjugate / determinant). */
    mat4 inverse() const {
        float src[4][4];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                src[r][c] = (*this)(r, c);

        auto det3 = [](const float* m) {
            return m[0] * (m[4] * m[8] - m[5] * m[7])
                 - m[1] * (m[3] * m[8] - m[5] * m[6])
                 + m[2] * (m[3] * m[7] - m[4] * m[6]);
        };

        float cof[4][4];
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                float m3[9];
                int n = 0;
                for (int i = 0; i < 4; ++i) {
                    if (i == r) continue;
                    for (int j = 0; j < 4; ++j) {
                        if (j == c) continue;
                        m3[n++] = src[i][j];
                    }
                }
                cof[r][c] = ((r + c) % 2) ? -det3(m3) : det3(m3);
            }
        }

        float det = 0.0f;
        for (int c = 0; c < 4; ++c) det += src[0][c] * cof[0][c];
        if (det == 0.0f) return mat4();

        const float invDet = 1.0f / det;
        mat4 out;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out(r, c) = cof[c][r] * invDet;   // adjugate = cofactor transpose
        return out;
    }
};

} // namespace ks::sim
