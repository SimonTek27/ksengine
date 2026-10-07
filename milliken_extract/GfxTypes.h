#pragma once
/** Minimal math types for Qt-free Graphics (no QVector3D/QMatrix4x4). */
#include <array>
#include <cmath>
#include <cstdint>

namespace ks {
namespace engine {
namespace graphics {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator*(float s) const { return {x*s, y*s, z*s}; }
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

struct Mat4 {
    std::array<float, 16> m{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    static Mat4 identity() { return Mat4{}; }
};

inline float clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}

} // namespace graphics
} // namespace engine
} // namespace ks
