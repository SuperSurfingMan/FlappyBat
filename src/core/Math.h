#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <random>
#include <array>

inline void Ortho(
    float left, float right,
    float bottom, float top,
    float out[16]
) {
    // column-major
    out[0]  = 2.0f / (right - left);
    out[1]  = 0.0f;
    out[2]  = 0.0f;
    out[3]  = 0.0f;

    out[4]  = 0.0f;
    out[5]  = 2.0f / (top - bottom);
    out[6]  = 0.0f;
    out[7]  = 0.0f;

    out[8]  = 0.0f;
    out[9]  = 0.0f;
    out[10] = -1.0f;
    out[11] = 0.0f;

    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = 0.0f;
    out[15] = 1.0f;
}


// ============================
// Vec2
// ============================
struct Vec2
{
    float x;
    float y;

    // ==============================
    // 构造
    // ==============================
    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float _x, float _y) : x(_x), y(_y) {}

    // ==============================
    // 基础运算符
    // ==============================

    // 加法
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }

    // 减法
    Vec2 operator-(const Vec2& other) const {
        return Vec2(x - other.x, y - other.y);
    }

    // 标量乘法
    Vec2 operator*(float scalar) const {
        return Vec2(x * scalar, y * scalar);
    }

    // 标量除法
    Vec2 operator/(float scalar) const {
        return Vec2(x / scalar, y / scalar);
    }

    // ==============================
    // 复合赋值
    // ==============================

    Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vec2& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Vec2& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    // ==============================
    // 比较
    // ==============================

    bool operator==(const Vec2& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Vec2& other) const {
        return !(*this == other);
    }

    // ==============================
    // 向量数学
    // ==============================

    float Length() const {
        return std::sqrt(x * x + y * y);
    }

    float LengthSquared() const {
        return x * x + y * y;
    }

    void Normalize() {
        float len = Length();
        if (len > 0.0f) {
            x /= len;
            y /= len;
        }
    }

    Vec2 Normalized() const {
        float len = Length();
        if (len > 0.0f)
            return Vec2(x / len, y / len);
        return Vec2(0.0f, 0.0f);
    }

    float Dot(const Vec2& other) const {
        return x * other.x + y * other.y;
    }

    // ==============================
    // 静态工具函数
    // ==============================

    static float Distance(const Vec2& a, const Vec2& b) {
        return (a - b).Length();
    }

    static float DistanceSquared(const Vec2& a, const Vec2& b) {
        return (a - b).LengthSquared();
    }

    static Vec2 Lerp(const Vec2& a, const Vec2& b, float t) {
        return a + (b - a) * t;
    }

    // ==============================
    // 常量向量
    // ==============================

    static Vec2 Zero() { return Vec2(0.0f, 0.0f); }
    static Vec2 One()  { return Vec2(1.0f, 1.0f); }
    static Vec2 Up()   { return Vec2(0.0f, 1.0f); }
    static Vec2 Down() { return Vec2(0.0f, -1.0f); }
    static Vec2 Left() { return Vec2(-1.0f, 0.0f); }
    static Vec2 Right(){ return Vec2(1.0f, 0.0f); }
};

// ==============================
// 非成员函数（支持 scalar * Vec2）
// ==============================

inline Vec2 operator*(float scalar, const Vec2& v) {
    return Vec2(v.x * scalar, v.y * scalar);
}

// ============================
// Vec3
// ============================

struct Vec3
{
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float _x, float _y, float _z)
        : x(_x), y(_y), z(_z) {}
};


// ============================
// Vec4
// ============================

struct Vec4
{
    float r, g, b, a;

    Vec4() : r(0), g(0), b(0), a(0) {}
    Vec4(float _r, float _g, float _b, float _a)
        : r(_r), g(_g), b(_b), a(_a) {}
};

// ============================
// Mat4 (column-major, OpenGL风格)
// ============================

struct Mat4
{
    float m[16];  // column-major

    // 构造单位矩阵
    static Mat4 Identity()
    {
        Mat4 result;
        for (int i = 0; i < 16; i++)
            result.m[i] = 0.0f;

        result.m[0]  = 1.0f;
        result.m[5]  = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;

        return result;
    }

    float* Data() { return m; }
    const float* Data() const { return m; }

    // 矩阵乘法
    static Mat4 Multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 result;

        for (int col = 0; col < 4; col++)
        {
            for (int row = 0; row < 4; row++)
            {
                result.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }

        return result;
    }

    // 平移矩阵
    static Mat4 Translate(const Vec3& t)
    {
        Mat4 result = Identity();

        result.m[12] = t.x;
        result.m[13] = t.y;
        result.m[14] = t.z;

        return result;
    }

    // 缩放矩阵
    static Mat4 Scale(const Vec3& s)
    {
        Mat4 result = Identity();

        result.m[0]  = s.x;
        result.m[5]  = s.y;
        result.m[10] = s.z;

        return result;
    }

    // 绕 Z 轴旋转
    static Mat4 RotateZ(float radians)
    {
        Mat4 result = Identity();

        float c = std::cos(radians);
        float s = std::sin(radians);

        result.m[0] =  c;
        result.m[1] =  s;
        result.m[4] = -s;
        result.m[5] =  c;

        return result;
    }
};

// 判断目标点是否在由 left_p 到 right_p 的直线之上的函数。
// 参数：
//   left_p   - 直线的左端点
//   right_p  - 直线的右端点
//   target   - 需要判断的目标点
// 返回值：
//   如果目标点在直线之上，返回 true；否则返回 false。
inline bool AboveTheLine(Vec2 left_p, Vec2 right_p, Vec2 target) {
    Vec2 p_p = {right_p.x - left_p.x, right_p.y - left_p.y};
    Vec2 left_p_target = {target.x - left_p.x, target.y - left_p.y};
    return (p_p.x * left_p_target.y - p_p.y * left_p_target.x) > 0;
}

inline float Clamp(float min, float max, float num) {
    if (min > max) std::swap(min, max);
    if (num < min) return min;
    if (num > max) return max;
    return num;
}

inline float Lerp(float start, float end, float l) {
    if (l < 0.f) return start;
    if (l > 1.0f) return end;
    return (end - start) * l + start;
}

class Random {
private:
    std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> gapRand{0.0f, 1.0f};

    Random() = default;
    ~Random() = default;
    
    Random(const Random&) = delete;
    Random& operator=(const Random&) = delete;

public:
    static Random& Get() {
        static Random instance;
        return instance;
    }

    float GetRandomFloat() {
        return gapRand(rng);
    }

};

struct SpriteGeometry {
    Vec2 scale;
    Vec2 position;
};

struct Point2D;

bool CheckCollision(
    const std::vector<SpriteGeometry>& obstaclesGeometry,
    const SpriteGeometry& batGeometry,
    const std::array<Point2D, 3>& obsUVCords,
    const std::array<Point2D, 2>& batUVCords
);
