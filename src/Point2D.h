#pragma once
#include <cmath>
#include <array>
#include <vector>

struct Point2D {
    float x;
    float y;

    Point2D(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    Point2D operator+(const Point2D& other) const {
        return Point2D(x + other.x, y + other.y);
    }

    Point2D operator-(const Point2D& other) const {
        return Point2D(x - other.x, y - other.y);
    }

    Point2D operator*(float scalar) const {
        return Point2D(x * scalar, y * scalar);
    }

    Point2D operator/(float scalar) const {
        return Point2D(x / scalar, y / scalar);
    }

    Point2D& operator+=(const Point2D& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    Point2D& operator-=(const Point2D& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Point2D& operator*=(float scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    Point2D& operator/=(float scalar) {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    float Length() const {
        return std::sqrt(x * x + y * y);
    }

    Point2D Normalized() const {
        float len = Length();
        if (len == 0) return Point2D(0, 0);
        return Point2D(x / len, y / len);
    }

    float Dot(const Point2D& other) const {
        return x * other.x + y * other.y;
    }

    float Cross(const Point2D& other) const {
        return x * other.y - y * other.x;
    }

    static float Distance(const Point2D& a, const Point2D& b) {
        return (a - b).Length();
    }
};

static float Dot(const Point2D& a, const Point2D& b)
{
    return a.x * b.x + a.y * b.y;
}

static Point2D Normalize(const Point2D& v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len == 0.0f) return {0,0};
    return { v.x / len, v.y / len };
}

static void ProjectOntoAxis(
    const std::vector<Point2D>& poly,
    const Point2D& axis,
    float& min,
    float& max)
{
    min = max = poly[0].Dot(axis);

    for (size_t i = 1; i < poly.size(); ++i)
    {
        float p = poly[i].Dot(axis);
        min = std::min(min, p);
        max = std::max(max, p);
    }
}

static bool OverlapOnAxis(
    const std::vector<Point2D>& polyA,
    const std::vector<Point2D>& polyB,
    const Point2D& axis)
{
    float minA, maxA;
    float minB, maxB;

    ProjectOntoAxis(polyA, axis, minA, maxA);
    ProjectOntoAxis(polyB, axis, minB, maxB);

    return !(maxA < minB || maxB < minA);
}