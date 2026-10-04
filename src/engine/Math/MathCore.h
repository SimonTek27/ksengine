#pragma once

// Prevent Windows.h from defining min/max macros
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <cstring>
#include <algorithm>
#ifdef KS_HAS_QT
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector2D>
#endif

// Math constants
namespace ks::math {
    constexpr float PI = 3.14159265358979323846f;
    constexpr float TWO_PI = 6.28318530717958647692f;
    constexpr float HALF_PI = 1.57079632679489661923f;
    constexpr float DEG_TO_RAD = PI / 180.0f;
    constexpr float RAD_TO_DEG = 180.0f / PI;
    constexpr float EPSILON = 1e-6f;
    constexpr float EPSILON_SQ = 1e-12f;
    constexpr float FLOAT_MAX = 3.402823466e+38f;
    constexpr float FLOAT_MIN = 1.175494351e-38f;

    inline float radians(float degrees) { return degrees * DEG_TO_RAD; }
    inline float degrees(float radians) { return radians * RAD_TO_DEG; }
    inline float clamp(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
    inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    inline float smoothstep(float a, float b, float t) {
        float x = clamp((t - a) / (b - a), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
    inline float sign(float v) { return (v > 0.0f) ? 1.0f : (v < 0.0f) ? -1.0f : 0.0f; }
}

struct Vec2 {
    float x;
    float y;

    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
#ifdef KS_HAS_QT
    explicit Vec2(const QVector2D& v) : x(v.x()), y(v.y()) {}
#endif

    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s)       { x *= s;   y *= s;   return *this; }
    Vec2& operator/=(float s)       { x /= s;   y /= s;   return *this; }

    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
#ifdef KS_HAS_QT
    QVector2D toQVector2D() const { return QVector2D(x, y); }
#endif
};

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2(a.x + b.x, a.y + b.y); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2(a.x - b.x, a.y - b.y); }
inline Vec2 operator*(const Vec2& v, float s)       { return Vec2(v.x * s, v.y * s); }
inline Vec2 operator*(float s, const Vec2& v)       { return Vec2(v.x * s, v.y * s); }
inline Vec2 operator/(const Vec2& v, float s)       { return Vec2(v.x / s, v.y / s); }
inline Vec2 operator-(const Vec2& v)                { return Vec2(-v.x, -v.y); }

inline float dot(const Vec2& a, const Vec2& b)  { return a.x*b.x + a.y*b.y; }
inline float length(const Vec2& v)              { return std::sqrt(v.x*v.x + v.y*v.y); }
inline float lengthSq(const Vec2& v)            { return v.x*v.x + v.y*v.y; }
inline float distance(const Vec2& a, const Vec2& b) { return length(a - b); }
inline float distanceSq(const Vec2& a, const Vec2& b) { return lengthSq(a - b); }

inline Vec2 normalize(const Vec2& v) {
    float len = length(v);
    return (len > ks::math::EPSILON) ? v / len : Vec2(0.0f, 0.0f);
}

inline Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
    return a * (1.0f - t) + b * t;
}

inline Vec2 perpendicular(const Vec2& v) { return Vec2(-v.y, v.x); }

struct Vec3 {
    float x;
    float y;
    float z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
#ifdef KS_HAS_QT
    explicit Vec3(const QVector3D& v) : x(v.x()), y(v.y()), z(v.z()) {}
#endif

    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s)       { x *= s;   y *= s;   z *= s;   return *this; }
    Vec3& operator/=(float s)       { x /= s;   y /= s;   z /= s;   return *this; }

    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }

    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    Vec3 normalized() const { float len = std::sqrt(x*x + y*y + z*z); return (len > ks::math::EPSILON) ? Vec3(x/len, y/len, z/len) : Vec3(0,0,0); }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return Vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
    }

#ifdef KS_HAS_QT
    QVector3D toQVector3D() const { return QVector3D(x, y, z); }
#endif
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(const Vec3& a, float s)       { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, const Vec3& a)       { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline Vec3 operator/(const Vec3& a, float s)       { return Vec3(a.x / s, a.y / s, a.z / s); }
inline Vec3 operator-(const Vec3& v)                { return Vec3(-v.x, -v.y, -v.z); }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

inline float dot(const Vec3& a, const Vec3& b)  { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline float length(const Vec3& v)              { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
inline float lengthSq(const Vec3& v)            { return v.x*v.x + v.y*v.y + v.z*v.z; }
inline float distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline float distanceSq(const Vec3& a, const Vec3& b) { return lengthSq(a - b); }

inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return (len > ks::math::EPSILON) ? v / len : Vec3(0.0f, 0.0f, 0.0f);
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return a * (1.0f - t) + b * t;
}

inline Vec3 reflect(const Vec3& v, const Vec3& n) {
    return v - n * (2.0f * dot(v, n));
}

inline bool refract(const Vec3& v, const Vec3& n, float eta, Vec3& result) {
    float d = dot(v, n);
    float k = 1.0f - eta * eta * (1.0f - d * d);
    if (k < 0.0f) return false;
    result = v * eta - n * (eta * d + std::sqrt(k));
    return true;
}

inline Vec3 project(const Vec3& v, const Vec3& onto) {
    float lenSq = lengthSq(onto);
    return (lenSq > ks::math::EPSILON_SQ) ? onto * (dot(v, onto) / lenSq) : Vec3(0, 0, 0);
}

inline Vec3 reject(const Vec3& v, const Vec3& onto) {
    return v - project(v, onto);
}

inline float angleBetween(const Vec3& a, const Vec3& b) {
    float d = dot(a, b);
    float lenSq = lengthSq(a) * lengthSq(b);
    return (lenSq > ks::math::EPSILON_SQ) ? std::acos(ks::math::clamp(d / std::sqrt(lenSq), -1.0f, 1.0f)) : 0.0f;
}

inline Vec3 minVec(const Vec3& a, const Vec3& b) {
    return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
}

inline Vec3 maxVec(const Vec3& a, const Vec3& b) {
    return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
}

inline Vec3 abs(const Vec3& v) {
    return Vec3(std::abs(v.x), std::abs(v.y), std::abs(v.z));
}

// Aliases for code expecting Vector2/Vector3 naming
using Vector2 = Vec2;
using Vector3 = Vec3;

// ============================================================================
// Mat3 - 3x3 Matrix (column-major, for rotation/scale)
// ============================================================================
struct Mat3 {
    float m[9]; // column-major: m[col*3+row]

    Mat3() { memset(m, 0, sizeof(m)); m[0] = m[4] = m[8] = 1.0f; }

    explicit Mat3(const float* data) { memcpy(m, data, sizeof(m)); }

    float& operator()(int row, int col) { return m[col * 3 + row]; }
    const float& operator()(int row, int col) const { return m[col * 3 + row]; }

    float* data() { return m; }
    const float* data() const { return m; }

    static Mat3 identity();
    static Mat3 fromRotationX(float angleRad);
    static Mat3 fromRotationY(float angleRad);
    static Mat3 fromRotationZ(float angleRad);
    static Mat3 fromScale(float sx, float sy, float sz);
    static Mat3 fromEuler(float xRad, float yRad, float zRad);

    Mat3 transposed() const;
    float determinant() const;
    Mat3 inverse() const;

    Vec3 operator*(const Vec3& v) const {
        return Vec3(
            m[0]*v.x + m[3]*v.y + m[6]*v.z,
            m[1]*v.x + m[4]*v.y + m[7]*v.z,
            m[2]*v.x + m[5]*v.y + m[8]*v.z
        );
    }

    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++) {
                float sum = 0.0f;
                for (int k = 0; k < 3; k++)
                    sum += (*this)(row, k) * o(k, col);
                r(row, col) = sum;
            }
        return r;
    }
};

inline Mat3 Mat3::identity() { Mat3 r; return r; }

inline Mat3 Mat3::fromRotationX(float a) {
    Mat3 r;
    float c = std::cos(a), s = std::sin(a);
    r(1,1)=c; r(1,2)=-s;
    r(2,1)=s; r(2,2)=c;
    return r;
}

inline Mat3 Mat3::fromRotationY(float a) {
    Mat3 r;
    float c = std::cos(a), s = std::sin(a);
    r(0,0)=c;  r(0,2)=s;
    r(2,0)=-s; r(2,2)=c;
    return r;
}

inline Mat3 Mat3::fromRotationZ(float a) {
    Mat3 r;
    float c = std::cos(a), s = std::sin(a);
    r(0,0)=c; r(0,1)=-s;
    r(1,0)=s; r(1,1)=c;
    return r;
}

inline Mat3 Mat3::fromScale(float sx, float sy, float sz) {
    Mat3 r;
    r(0,0)=sx; r(1,1)=sy; r(2,2)=sz;
    return r;
}

inline Mat3 Mat3::fromEuler(float x, float y, float z) {
    return fromRotationZ(z) * fromRotationY(y) * fromRotationX(x);
}

inline Mat3 Mat3::transposed() const {
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r(i, j) = (*this)(j, i);
    return r;
}

inline float Mat3::determinant() const {
    return m[0]*(m[4]*m[8]-m[5]*m[7])
         - m[3]*(m[1]*m[8]-m[2]*m[7])
         + m[6]*(m[1]*m[5]-m[2]*m[4]);
}

inline Mat3 Mat3::inverse() const {
    float det = determinant();
    if (std::abs(det) < ks::math::EPSILON) return identity();
    float invDet = 1.0f / det;
    Mat3 r;
    r(0,0) = (m[4]*m[8]-m[5]*m[7])*invDet;
    r(0,1) = (m[2]*m[7]-m[1]*m[8])*invDet;
    r(0,2) = (m[1]*m[5]-m[2]*m[4])*invDet;
    r(1,0) = (m[5]*m[6]-m[3]*m[8])*invDet;
    r(1,1) = (m[0]*m[8]-m[2]*m[6])*invDet;
    r(1,2) = (m[2]*m[3]-m[0]*m[5])*invDet;
    r(2,0) = (m[3]*m[7]-m[4]*m[6])*invDet;
    r(2,1) = (m[1]*m[6]-m[0]*m[7])*invDet;
    r(2,2) = (m[0]*m[4]-m[1]*m[3])*invDet;
    return r;
}

// ============================================================================
// Quaternion
// ============================================================================
struct Quat {
    float x, y, z, w;

    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return Quat(0, 0, 0, 1); }
    static Quat fromAxisAngle(const Vec3& axis, float angleRad);
    static Quat fromEuler(float xRad, float yRad, float zRad);
    static Quat fromEulerDeg(float xDeg, float yDeg, float zDeg) {
        return fromEuler(ks::math::radians(xDeg), ks::math::radians(yDeg), ks::math::radians(zDeg));
    }
    static Quat fromMatrix(const Mat3& m);
    static Quat fromTwoVectors(const Vec3& from, const Vec3& to);

    Quat conjugated() const { return Quat(-x, -y, -z, w); }
    Quat inverse() const;
    float length() const { return std::sqrt(x*x + y*y + z*z + w*w); }
    float lengthSq() const { return x*x + y*y + z*z + w*w; }
    Quat normalized() const;

    Quat operator*(const Quat& o) const {
        return Quat(
            w*o.x + x*o.w + y*o.z - z*o.y,
            w*o.y - x*o.z + y*o.w + z*o.x,
            w*o.z + x*o.y - y*o.x + z*o.w,
            w*o.w - x*o.x - y*o.y - z*o.z
        );
    }

    Vec3 operator*(const Vec3& v) const {
        Quat p(v.x, v.y, v.z, 0);
        Quat q = *this * p * conjugated();
        return Vec3(q.x, q.y, q.z);
    }

    Mat3 toMat3() const;
    Vec3 toEuler() const;

    Quat slerp(const Quat& to, float t) const;
    Quat operator/(float s) const { return Quat(x/s, y/s, z/s, w/s); }

    Vec3 axis() const {
        float len = std::sqrt(x*x + y*y + z*z);
        return (len > ks::math::EPSILON) ? Vec3(x/len, y/len, z/len) : Vec3(1, 0, 0);
    }

    float angle() const {
        return 2.0f * std::acos(ks::math::clamp(w, -1.0f, 1.0f));
    }
};

inline Quat Quat::fromAxisAngle(const Vec3& axis, float angleRad) {
    float halfAngle = angleRad * 0.5f;
    float s = std::sin(halfAngle);
    Vec3 n = normalize(axis);
    return Quat(n.x * s, n.y * s, n.z * s, std::cos(halfAngle));
}

inline Quat Quat::fromEuler(float xRad, float yRad, float zRad) {
    float cx = std::cos(xRad * 0.5f), sx = std::sin(xRad * 0.5f);
    float cy = std::cos(yRad * 0.5f), sy = std::sin(yRad * 0.5f);
    float cz = std::cos(zRad * 0.5f), sz = std::sin(zRad * 0.5f);
    return Quat(
        sx*cy*cz + cx*sy*sz,
        cx*sy*cz - sx*cy*sz,
        cx*cy*sz + sx*sy*cz,
        cx*cy*cz - sx*sy*sz
    );
}

inline Quat Quat::fromMatrix(const Mat3& m) {
    float trace = m(0,0) + m(1,1) + m(2,2);
    if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        return Quat((m(2,1) - m(1,2)) * s, (m(0,2) - m(2,0)) * s, (m(1,0) - m(0,1)) * s, 0.25f / s);
    } else if (m(0,0) > m(1,1) && m(0,0) > m(2,2)) {
        float s = 2.0f * std::sqrt(1.0f + m(0,0) - m(1,1) - m(2,2));
        return Quat(0.25f * s, (m(0,1) + m(1,0)) / s, (m(0,2) + m(2,0)) / s, (m(2,1) - m(1,2)) / s);
    } else if (m(1,1) > m(2,2)) {
        float s = 2.0f * std::sqrt(1.0f + m(1,1) - m(0,0) - m(2,2));
        return Quat((m(0,1) + m(1,0)) / s, 0.25f * s, (m(1,2) + m(2,1)) / s, (m(0,2) - m(2,0)) / s);
    } else {
        float s = 2.0f * std::sqrt(1.0f + m(2,2) - m(0,0) - m(1,1));
        return Quat((m(0,2) + m(2,0)) / s, (m(1,2) + m(2,1)) / s, 0.25f * s, (m(1,0) - m(0,1)) / s);
    }
}

inline Quat Quat::fromTwoVectors(const Vec3& from, const Vec3& to) {
    Vec3 f = normalize(from);
    Vec3 t = normalize(to);
    float d = dot(f, t);
    if (d > 0.9999f) return identity();
    if (d < -0.9999f) {
        Vec3 axis = Vec3::cross(f, Vec3(1, 0, 0));
        if (::lengthSq(axis) < ks::math::EPSILON_SQ) axis = Vec3::cross(f, Vec3(0, 1, 0));
        axis = normalize(axis);
        return Quat(axis.x, axis.y, axis.z, 0);
    }
    Vec3 axis = Vec3::cross(f, t);
    return Quat(axis.x, axis.y, axis.z, 1.0f + d).normalized();
}

inline Quat Quat::inverse() const {
    float lenSq = this->lengthSq();
    return (lenSq > ks::math::EPSILON_SQ) ? conjugated() / lenSq : identity();
}

inline Quat Quat::normalized() const {
    float len = length();
    return (len > ks::math::EPSILON) ? Quat(x/len, y/len, z/len, w/len) : identity();
}

inline Quat Quat::slerp(const Quat& to, float t) const {
    float cosOmega = x*to.x + y*to.y + z*to.z + w*to.w;
    Quat to2 = to;
    if (cosOmega < 0.0f) { cosOmega = -cosOmega; to2 = Quat(-to.x, -to.y, -to.z, -to.w); }
    float k0, k1;
    if (cosOmega > 0.9999f) {
        k0 = 1.0f - t;
        k1 = t;
    } else {
        float sinOmega = std::sqrt(1.0f - cosOmega*cosOmega);
        float omega = std::atan2(sinOmega, cosOmega);
        float invSin = 1.0f / sinOmega;
        k0 = std::sin((1.0f - t) * omega) * invSin;
        k1 = std::sin(t * omega) * invSin;
    }
    return Quat(x*k0 + to2.x*k1, y*k0 + to2.y*k1, z*k0 + to2.z*k1, w*k0 + to2.w*k1);
}

inline Mat3 Quat::toMat3() const {
    float x2 = x*x, y2 = y*y, z2 = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;
    Mat3 r;
    r(0,0) = 1.0f - 2.0f*(y2 + z2); r(0,1) = 2.0f*(xy - wz);        r(0,2) = 2.0f*(xz + wy);
    r(1,0) = 2.0f*(xy + wz);        r(1,1) = 1.0f - 2.0f*(x2 + z2); r(1,2) = 2.0f*(yz - wx);
    r(2,0) = 2.0f*(xz - wy);        r(2,1) = 2.0f*(yz + wx);        r(2,2) = 1.0f - 2.0f*(x2 + y2);
    return r;
}

inline Vec3 Quat::toEuler() const {
    float sinr = 2.0f*(w*x + y*z);
    float cosr = 1.0f - 2.0f*(x*x + y*y);
    float roll = std::atan2(sinr, cosr);
    float sinp = 2.0f*(w*y - z*x);
    float pitch = (std::abs(sinp) >= 1.0f) ? std::copysign(ks::math::HALF_PI, sinp) : std::asin(sinp);
    float siny = 2.0f*(w*z + x*y);
    float cosy = 1.0f - 2.0f*(y*y + z*z);
    float yaw = std::atan2(siny, cosy);
    return Vec3(roll, pitch, yaw);
}

// ============================================================================
// Ray
// ============================================================================
struct Ray {
    Vec3 origin;
    Vec3 direction;

    Ray() = default;
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(normalize(d)) {}

    Vec3 at(float t) const { return origin + direction * t; }
};

// ============================================================================
// Plane
// ============================================================================
struct Plane {
    Vec3 normal;
    float distance; // signed distance from origin (n·p = d)

    Plane() : normal(0, 1, 0), distance(0) {}
    Plane(const Vec3& n, float d) : normal(normalize(n)), distance(d) {}
    Plane(const Vec3& a, const Vec3& b, const Vec3& c) {
        normal = normalize(cross(b - a, c - a));
        distance = dot(normal, a);
    }

    float signedDistance(const Vec3& p) const { return dot(normal, p) - distance; }
    Vec3 projectPoint(const Vec3& p) const { return p - normal * signedDistance(p); }
    bool isFrontFacing(const Vec3& viewDir) const { return dot(normal, viewDir) < 0.0f; }
};

// ============================================================================
// Axis-Aligned Bounding Box
// ============================================================================
struct AABB {
    Vec3 min;
    Vec3 max;

    AABB() : min(ks::math::FLOAT_MAX, ks::math::FLOAT_MAX, ks::math::FLOAT_MAX), max(-ks::math::FLOAT_MAX, -ks::math::FLOAT_MAX, -ks::math::FLOAT_MAX) {}
    AABB(const Vec3& p) : min(p), max(p) {}
    AABB(const Vec3& min_, const Vec3& max_) : min(min_), max(max_) {}

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 extents() const { return (max - min) * 0.5f; }
    Vec3 size() const { return max - min; }
    float surfaceArea() const {
        Vec3 s = size();
        return 2.0f * (s.x*s.y + s.x*s.z + s.y*s.z);
    }
    float volume() const {
        Vec3 s = size();
        return s.x * s.y * s.z;
    }

    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void expand(const Vec3& p) { min = minVec(min, p); max = maxVec(max, p); }
    void expand(const AABB& other) { min = minVec(min, other.min); max = maxVec(max, other.max); }

    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y
            && min.z <= other.max.z && max.z >= other.min.z;
    }

    bool intersect(const Ray& ray, float& tMin, float& tMax) const;
};

inline bool AABB::intersect(const Ray& ray, float& tMin, float& tMax) const {
    Vec3 invDir(1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z);
    Vec3 t0 = (min - ray.origin) * invDir;
    Vec3 t1 = (max - ray.origin) * invDir;
    Vec3 tNear = minVec(t0, t1);
    Vec3 tFar = maxVec(t0, t1);
    tMin = std::max({tNear.x, tNear.y, tNear.z});
    tMax = std::min({tFar.x, tFar.y, tFar.z});
    return tMin <= tMax && tMax >= 0.0f;
}

// ============================================================================
// Intersection helpers
// ============================================================================
inline bool intersectRayPlane(const Ray& ray, const Plane& plane, float& t) {
    float denom = dot(plane.normal, ray.direction);
    if (std::abs(denom) < ks::math::EPSILON) return false;
    t = (plane.distance - dot(plane.normal, ray.origin)) / denom;
    return t >= 0.0f;
}

inline bool intersectRayTriangle(const Ray& ray, const Vec3& v0, const Vec3& v1, const Vec3& v2, float& t, float& u, float& v) {
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 h = cross(ray.direction, e2);
    float a = dot(e1, h);
    if (std::abs(a) < ks::math::EPSILON) return false;
    float f = 1.0f / a;
    Vec3 s = ray.origin - v0;
    u = f * dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    Vec3 q = cross(s, e1);
    v = f * dot(ray.direction, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * dot(e2, q);
    return t >= 0.0f;
}

inline bool intersectRaySphere(const Ray& ray, const Vec3& center, float radius, float& t0, float& t1) {
    Vec3 oc = ray.origin - center;
    float a = dot(ray.direction, ray.direction);
    float b = 2.0f * dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    float sqrtDisc = std::sqrt(disc);
    t0 = (-b - sqrtDisc) / (2.0f * a);
    t1 = (-b + sqrtDisc) / (2.0f * a);
    return true;
}

// ============================================================================
// Matrix4 (existing, extended)
// ============================================================================
namespace ks {

struct Matrix4 {
    struct Translation { float x, y, z; } translation_ = {0,0,0};
    Vec3 rotation_ = {0,0,0};
    Vec3 scale_ = {1,1,1};
    float m[4][4];

    const float* data() const { return &m[0][0]; }
    float* data() { return &m[0][0]; }

    operator const float*() const { return &m[0][0]; }
    operator float*() { return &m[0][0]; }

    Matrix4() { rebuild(); }

    void rebuild() {
        float cx = cosf(rotation_.x), sx = sinf(rotation_.x);
        float cy = cosf(rotation_.y), sy = sinf(rotation_.y);
        float cz = cosf(rotation_.z), sz = sinf(rotation_.z);

        float r00 = cy * cz;
        float r01 = cz * sx * sy - cx * sz;
        float r02 = cx * cz * sy + sx * sz;
        float r10 = cy * sz;
        float r11 = sx * sy * sz + cx * cz;
        float r12 = cx * sy * sz - cz * sx;
        float r20 = -sy;
        float r21 = cy * sx;
        float r22 = cx * cy;

        m[0][0] = r00 * scale_.x; m[0][1] = r01 * scale_.x; m[0][2] = r02 * scale_.x;
        m[1][0] = r10 * scale_.y; m[1][1] = r11 * scale_.y; m[1][2] = r12 * scale_.y;
        m[2][0] = r20 * scale_.z; m[2][1] = r21 * scale_.z; m[2][2] = r22 * scale_.z;
        m[0][3] = m[1][3] = m[2][3] = 0.0f;

        m[3][0] = translation_.x; m[3][1] = translation_.y; m[3][2] = translation_.z; m[3][3] = 1.0f;
    }

    void setTranslation(const Vec3& t) { translation_.x = t.x; translation_.y = t.y; translation_.z = t.z; rebuild(); }
    Translation translation() const { return translation_; }
    Vec3 rotation() const { return rotation_; }
    void setRotation(const Vec3& r) { rotation_ = r; rebuild(); }
    Vec3 scale() const { return scale_; }
    void setScale(const Vec3& s) { scale_ = s; rebuild(); }

    Matrix4 operator*(const Matrix4& o) const {
        Matrix4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                r.m[i][j] = 0;
                for (int k = 0; k < 4; k++)
                    r.m[i][j] += m[i][k] * o.m[k][j];
            }
        return r;
    }

    Vec3 operator*(const Vec3& v) const {
        float invW = 1.0f / (m[3][0]*v.x + m[3][1]*v.y + m[3][2]*v.z + m[3][3]);
        return Vec3(
            (m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3]) * invW,
            (m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3]) * invW,
            (m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3]) * invW
        );
    }

    // Determinant of 4x4 matrix
    float determinant() const;

    // Transpose (upper 3x3 only for rotation)
    Matrix4 transposed() const;

    // Full 4x4 inverse (for affine transforms)
    Matrix4 inverse() const;

    // Static constructors for view/projection matrices
    static Matrix4 Identity() { Matrix4 m; return m; }
    static Matrix4 fromTranslation(const Vec3& t) { Matrix4 m; m.setTranslation(t); return m; }
    static Matrix4 fromScale(const Vec3& s) { Matrix4 m; m.setScale(s); return m; }
    static Matrix4 fromRotation(const Vec3& r) { Matrix4 m; m.setRotation(r); return m; }
    static Matrix4 fromQuat(const Quat& q);
    static Matrix4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
    static Matrix4 perspective(float fovRad, float aspect, float near, float far);
    static Matrix4 orthographic(float left, float right, float bottom, float top, float near, float far);
    static Matrix4 frustum(float left, float right, float bottom, float top, float near, float far);

    Mat3 toMat3() const {
        Mat3 r;
        r(0,0)=m[0][0]; r(0,1)=m[0][1]; r(0,2)=m[0][2];
        r(1,0)=m[1][0]; r(1,1)=m[1][1]; r(1,2)=m[1][2];
        r(2,0)=m[2][0]; r(2,1)=m[2][1]; r(2,2)=m[2][2];
        return r;
    }

    Quat toQuat() const { return Quat::fromMatrix(toMat3()); }

#ifdef KS_HAS_QT
    QMatrix4x4 toQMatrix4x4() const {
        QMatrix4x4 qm;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                qm(i, j) = m[i][j];
        return qm;
    }
#endif
};

inline Matrix4 Matrix4::fromQuat(const Quat& q) {
    Mat3 r = q.toMat3();
    Matrix4 m;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m.m[i][j] = r(i, j);
    m.m[3][3] = 1.0f;
    return m;
}

inline Matrix4 Matrix4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Matrix4 m;
    m.m[0][0] = s.x; m.m[0][1] = s.y; m.m[0][2] = s.z;
    m.m[1][0] = u.x; m.m[1][1] = u.y; m.m[1][2] = u.z;
    m.m[2][0] = -f.x; m.m[2][1] = -f.y; m.m[2][2] = -f.z;
    m.m[3][0] = -dot(s, eye); m.m[3][1] = -dot(u, eye); m.m[3][2] = dot(f, eye);
    m.m[3][3] = 1.0f;
    m.m[0][3] = m.m[1][3] = m.m[2][3] = 0.0f;
    m.translation_.x = m.m[3][0]; m.translation_.y = m.m[3][1]; m.translation_.z = m.m[3][2];
    return m;
}

inline Matrix4 Matrix4::perspective(float fovRad, float aspect, float nearZ, float farZ) {
    float f = 1.0f / std::tan(fovRad * 0.5f);
    Matrix4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (farZ + nearZ) / (nearZ - farZ);
    m.m[2][3] = -1.0f;
    m.m[3][2] = (2.0f * farZ * nearZ) / (nearZ - farZ);
    m.m[3][3] = 0.0f;
    return m;
}

inline Matrix4 Matrix4::orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) {
    Matrix4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0][0] = 2.0f / (right - left);
    m.m[1][1] = 2.0f / (top - bottom);
    m.m[2][2] = -2.0f / (farZ - nearZ);
    m.m[3][0] = -(right + left) / (right - left);
    m.m[3][1] = -(top + bottom) / (top - bottom);
    m.m[3][2] = -(farZ + nearZ) / (farZ - nearZ);
    m.m[3][3] = 1.0f;
    return m;
}

inline Matrix4 Matrix4::frustum(float left, float right, float bottom, float top, float nearZ, float farZ) {
    Matrix4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0][0] = 2.0f * nearZ / (right - left);
    m.m[1][1] = 2.0f * nearZ / (top - bottom);
    m.m[2][0] = (right + left) / (right - left);
    m.m[2][1] = (top + bottom) / (top - bottom);
    m.m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m.m[2][3] = -1.0f;
    m.m[3][2] = -2.0f * farZ * nearZ / (farZ - nearZ);
    m.m[3][3] = 0.0f;
    return m;
}

inline float Matrix4::determinant() const {
    return m[0][0] * (m[1][1]*(m[2][2]*m[3][3]-m[2][3]*m[3][2]) - m[1][2]*(m[2][1]*m[3][3]-m[2][3]*m[3][1]) + m[1][3]*(m[2][1]*m[3][2]-m[2][2]*m[3][1]))
         - m[0][1] * (m[1][0]*(m[2][2]*m[3][3]-m[2][3]*m[3][2]) - m[1][2]*(m[2][0]*m[3][3]-m[2][3]*m[3][0]) + m[1][3]*(m[2][0]*m[3][2]-m[2][2]*m[3][0]))
         + m[0][2] * (m[1][0]*(m[2][1]*m[3][3]-m[2][3]*m[3][1]) - m[1][1]*(m[2][0]*m[3][3]-m[2][3]*m[3][0]) + m[1][3]*(m[2][0]*m[3][1]-m[2][1]*m[3][0]))
         - m[0][3] * (m[1][0]*(m[2][1]*m[3][2]-m[2][2]*m[3][1]) - m[1][1]*(m[2][0]*m[3][2]-m[2][2]*m[3][0]) + m[1][2]*(m[2][0]*m[3][1]-m[2][1]*m[3][0]));
}

inline Matrix4 Matrix4::transposed() const {
    Matrix4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = m[j][i];
    r.translation_ = translation_;
    r.rotation_ = rotation_;
    r.scale_ = scale_;
    return r;
}

inline Matrix4 Matrix4::inverse() const {
    float det = static_cast<const Matrix4*>(this)->determinant();
    if (std::abs(det) < ks::math::EPSILON) return Identity();
    float invDet = 1.0f / det;
    Matrix4 r;
    auto& a = m;
    r.m[0][0] = (a[1][1]*(a[2][2]*a[3][3]-a[2][3]*a[3][2]) - a[1][2]*(a[2][1]*a[3][3]-a[2][3]*a[3][1]) + a[1][3]*(a[2][1]*a[3][2]-a[2][2]*a[3][1])) * invDet;
    r.m[0][1] = -(a[0][1]*(a[2][2]*a[3][3]-a[2][3]*a[3][2]) - a[0][2]*(a[2][1]*a[3][3]-a[2][3]*a[3][1]) + a[0][3]*(a[2][1]*a[3][2]-a[2][2]*a[3][1])) * invDet;
    r.m[0][2] = (a[0][1]*(a[1][2]*a[3][3]-a[1][3]*a[3][2]) - a[0][2]*(a[1][1]*a[3][3]-a[1][3]*a[3][1]) + a[0][3]*(a[1][1]*a[3][2]-a[1][2]*a[3][1])) * invDet;
    r.m[0][3] = -(a[0][1]*(a[1][2]*a[2][3]-a[1][3]*a[2][2]) - a[0][2]*(a[1][1]*a[2][3]-a[1][3]*a[2][1]) + a[0][3]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])) * invDet;
    r.m[1][0] = -(a[1][0]*(a[2][2]*a[3][3]-a[2][3]*a[3][2]) - a[1][2]*(a[2][0]*a[3][3]-a[2][3]*a[3][0]) + a[1][3]*(a[2][0]*a[3][2]-a[2][2]*a[3][0])) * invDet;
    r.m[1][1] = (a[0][0]*(a[2][2]*a[3][3]-a[2][3]*a[3][2]) - a[0][2]*(a[2][0]*a[3][3]-a[2][3]*a[3][0]) + a[0][3]*(a[2][0]*a[3][2]-a[2][2]*a[3][0])) * invDet;
    r.m[1][2] = -(a[0][0]*(a[1][2]*a[3][3]-a[1][3]*a[3][2]) - a[0][2]*(a[1][0]*a[3][3]-a[1][3]*a[3][0]) + a[0][3]*(a[1][0]*a[3][2]-a[1][2]*a[3][0])) * invDet;
    r.m[1][3] = (a[0][0]*(a[1][2]*a[2][3]-a[1][3]*a[2][2]) - a[0][2]*(a[1][0]*a[2][3]-a[1][3]*a[2][0]) + a[0][3]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])) * invDet;
    r.m[2][0] = (a[1][0]*(a[2][1]*a[3][3]-a[2][3]*a[3][1]) - a[1][1]*(a[2][0]*a[3][3]-a[2][3]*a[3][0]) + a[1][3]*(a[2][0]*a[3][1]-a[2][1]*a[3][0])) * invDet;
    r.m[2][1] = -(a[0][0]*(a[2][1]*a[3][3]-a[2][3]*a[3][1]) - a[0][1]*(a[2][0]*a[3][3]-a[2][3]*a[3][0]) + a[0][3]*(a[2][0]*a[3][1]-a[2][1]*a[3][0])) * invDet;
    r.m[2][2] = (a[0][0]*(a[1][1]*a[3][3]-a[1][3]*a[3][1]) - a[0][1]*(a[1][0]*a[3][3]-a[1][3]*a[3][0]) + a[0][3]*(a[1][0]*a[3][1]-a[1][1]*a[3][0])) * invDet;
    r.m[2][3] = -(a[0][0]*(a[1][1]*a[2][3]-a[1][3]*a[2][1]) - a[0][1]*(a[1][0]*a[2][3]-a[1][3]*a[2][0]) + a[0][3]*(a[1][0]*a[2][1]-a[1][1]*a[2][0])) * invDet;
    r.m[3][0] = -(a[1][0]*(a[2][1]*a[3][2]-a[2][2]*a[3][1]) - a[1][1]*(a[2][0]*a[3][2]-a[2][2]*a[3][0]) + a[1][2]*(a[2][0]*a[3][1]-a[2][1]*a[3][0])) * invDet;
    r.m[3][1] = (a[0][0]*(a[2][1]*a[3][2]-a[2][2]*a[3][1]) - a[0][1]*(a[2][0]*a[3][2]-a[2][2]*a[3][0]) + a[0][2]*(a[2][0]*a[3][1]-a[2][1]*a[3][0])) * invDet;
    r.m[3][2] = -(a[0][0]*(a[1][1]*a[3][2]-a[1][2]*a[3][1]) - a[0][1]*(a[1][0]*a[3][2]-a[1][2]*a[3][0]) + a[0][2]*(a[1][0]*a[3][1]-a[1][1]*a[3][0])) * invDet;
    r.m[3][3] = (a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1]) - a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0]) + a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0])) * invDet;
    return r;
}

} // namespace ks

// Vector4 (simple 4-float vector for vertex data)
struct Vector4 {
    float x, y, z, w;
    Vector4() : x(0), y(0), z(0), w(0) {}
    Vector4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

// Global aliases
using Matrix4 = ks::Matrix4;
using Vec4 = Vector4;
using Mat4 = Matrix4;
