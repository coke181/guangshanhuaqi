#ifndef RASTER_MATH_H
#define RASTER_MATH_H

#include <algorithm>
#include <cmath>

// 最小二维向量，当前主要用于纹理坐标。
struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

// 最小三维向量，当前主要用于位置、方向、法线和颜色。
struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// 齐次坐标向量，用于矩阵变换和透视除法。
struct Vec4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

// 二维向量分量加法用于 UV 插值。
inline Vec2f operator+(const Vec2f &lhs, const Vec2f &rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

// 二维向量分量减法用于 UV 插值。
inline Vec2f operator-(const Vec2f &lhs, const Vec2f &rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

// 二维向量标量乘法用于 UV 插值。
inline Vec2f operator*(const Vec2f &value, float scalar)
{
    return {value.x * scalar, value.y * scalar};
}

// 二维向量标量除法用于归一化等操作。
inline Vec2f operator/(const Vec2f &value, float scalar)
{
    return {value.x / scalar, value.y / scalar};
}

// 分量加法用于插值和组合。
inline Vec3f operator+(const Vec3f &lhs, const Vec3f &rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

// 分量减法用于几何构造。
inline Vec3f operator-(const Vec3f &lhs, const Vec3f &rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

// 标量乘法用于颜色、坐标和方向缩放。
inline Vec3f operator*(const Vec3f &value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

// 标量除法用于单位化等操作。
inline Vec3f operator/(const Vec3f &value, float scalar)
{
    return {value.x / scalar, value.y / scalar, value.z / scalar};
}

// 点积用于投影、夹角和 lookAt 视图矩阵。
inline float dot(const Vec3f &lhs, const Vec3f &rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

// 叉积用于构造正交坐标系。
inline Vec3f cross(const Vec3f &lhs, const Vec3f &rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
    };
}

// 向量长度主要用于单位化。
inline float length(const Vec3f &value)
{
    return std::sqrt(dot(value, value));
}

// 单位化方向向量；零向量时直接返回原值，避免产生 NaN。
inline Vec3f normalize(const Vec3f &value)
{
    const float valueLength = length(value);
    if (valueLength <= 1e-6f)
        return value;
    return value / valueLength;
}

// 把颜色限制在可显示的 0..1 范围内。
inline Vec3f clamp01(const Vec3f &value)
{
    return {
        std::clamp(value.x, 0.0f, 1.0f),
        std::clamp(value.y, 0.0f, 1.0f),
        std::clamp(value.z, 0.0f, 1.0f)
    };
}

// 4x4 矩阵覆盖当前渲染器需要的最小变换集合。
struct Mat4f {
    float m[4][4] = {};

    // 单位矩阵是其他变换的基础。
    static Mat4f identity()
    {
        Mat4f result;
        result.m[0][0] = 1.0f;
        result.m[1][1] = 1.0f;
        result.m[2][2] = 1.0f;
        result.m[3][3] = 1.0f;
        return result;
    }

    // 绕 X 轴旋转，主要用于 demo 模型动画。
    static Mat4f rotationX(float radians)
    {
        Mat4f result = identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result.m[1][1] = c;
        result.m[1][2] = -s;
        result.m[2][1] = s;
        result.m[2][2] = c;
        return result;
    }

    // 绕 Y 轴旋转，主要用于 demo 模型动画。
    static Mat4f rotationY(float radians)
    {
        Mat4f result = identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result.m[0][0] = c;
        result.m[0][2] = s;
        result.m[2][0] = -s;
        result.m[2][2] = c;
        return result;
    }

    // 绕 Z 轴旋转，适合二维平面摆放或完整欧拉角组合。
    static Mat4f rotationZ(float radians)
    {
        Mat4f result = identity();
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        result.m[0][0] = c;
        result.m[0][1] = -s;
        result.m[1][0] = s;
        result.m[1][1] = c;
        return result;
    }

    // 缩放矩阵把局部模型按各轴比例拉伸。
    static Mat4f scale(float x, float y, float z)
    {
        Mat4f result = identity();
        result.m[0][0] = x;
        result.m[1][1] = y;
        result.m[2][2] = z;
        return result;
    }

    // 平移矩阵把模型从局部空间移到世界空间。
    static Mat4f translation(float x, float y, float z)
    {
        Mat4f result = identity();
        result.m[0][3] = x;
        result.m[1][3] = y;
        result.m[2][3] = z;
        return result;
    }

    // 透视投影把观察空间映射到裁剪空间。
    static Mat4f perspective(float verticalFovRadians, float aspect, float nearPlane, float farPlane)
    {
        Mat4f result;
        const float tanHalfFov = std::tan(verticalFovRadians * 0.5f);
        result.m[0][0] = 1.0f / (aspect * tanHalfFov);
        result.m[1][1] = 1.0f / tanHalfFov;
        result.m[2][2] = (farPlane + nearPlane) / (nearPlane - farPlane);
        result.m[2][3] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
        result.m[3][2] = -1.0f;
        return result;
    }

    // 正交投影适合方向光阴影图等不需要透视缩放的场景。
    static Mat4f orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        Mat4f result = identity();
        result.m[0][0] = 2.0f / (right - left);
        result.m[1][1] = 2.0f / (top - bottom);
        result.m[2][2] = -2.0f / (farPlane - nearPlane);
        result.m[0][3] = -(right + left) / (right - left);
        result.m[1][3] = -(top + bottom) / (top - bottom);
        result.m[2][3] = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return result;
    }

    // lookAt 视图矩阵把世界空间变到相机观察空间。
    static Mat4f lookAt(const Vec3f &eye, const Vec3f &target, const Vec3f &up)
    {
        const Vec3f forward = normalize(target - eye);
        const Vec3f right = normalize(cross(forward, up));
        const Vec3f cameraUp = cross(right, forward);

        Mat4f result = identity();
        result.m[0][0] = right.x;
        result.m[0][1] = right.y;
        result.m[0][2] = right.z;
        result.m[0][3] = -dot(right, eye);

        result.m[1][0] = cameraUp.x;
        result.m[1][1] = cameraUp.y;
        result.m[1][2] = cameraUp.z;
        result.m[1][3] = -dot(cameraUp, eye);

        result.m[2][0] = -forward.x;
        result.m[2][1] = -forward.y;
        result.m[2][2] = -forward.z;
        result.m[2][3] = dot(forward, eye);

        return result;
    }
};

// 矩阵乘法用于拼接 model/view/projection 之类的变换。
inline Mat4f operator*(const Mat4f &lhs, const Mat4f &rhs)
{
    Mat4f result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int i = 0; i < 4; ++i)
                result.m[row][column] += lhs.m[row][i] * rhs.m[i][column];
        }
    }
    return result;
}

// 矩阵乘齐次向量是顶点变换的核心操作。
inline Vec4f operator*(const Mat4f &matrix, const Vec4f &value)
{
    return {
        matrix.m[0][0] * value.x + matrix.m[0][1] * value.y + matrix.m[0][2] * value.z + matrix.m[0][3] * value.w,
        matrix.m[1][0] * value.x + matrix.m[1][1] * value.y + matrix.m[1][2] * value.z + matrix.m[1][3] * value.w,
        matrix.m[2][0] * value.x + matrix.m[2][1] * value.y + matrix.m[2][2] * value.z + matrix.m[2][3] * value.w,
        matrix.m[3][0] * value.x + matrix.m[3][1] * value.y + matrix.m[3][2] * value.z + matrix.m[3][3] * value.w
    };
}

// 点坐标会受平移影响，适合 position / worldPos 变换。
inline Vec3f transformPoint(const Mat4f &matrix, const Vec3f &value)
{
    const Vec4f transformed = matrix * Vec4f{value.x, value.y, value.z, 1.0f};
    if (std::fabs(transformed.w) > 1e-6f && std::fabs(transformed.w - 1.0f) > 1e-6f)
        return {transformed.x / transformed.w, transformed.y / transformed.w, transformed.z / transformed.w};
    return {transformed.x, transformed.y, transformed.z};
}

// 方向向量不受平移影响，适合法线以外的一般方向变换。
inline Vec3f transformDirection(const Mat4f &matrix, const Vec3f &value)
{
    const Vec4f transformed = matrix * Vec4f{value.x, value.y, value.z, 0.0f};
    return {transformed.x, transformed.y, transformed.z};
}

// 法线使用 3x3 逆转置近似，可正确处理旋转和非均匀缩放。
inline Vec3f transformNormal(const Mat4f &matrix, const Vec3f &normal)
{
    const float a00 = matrix.m[0][0];
    const float a01 = matrix.m[0][1];
    const float a02 = matrix.m[0][2];
    const float a10 = matrix.m[1][0];
    const float a11 = matrix.m[1][1];
    const float a12 = matrix.m[1][2];
    const float a20 = matrix.m[2][0];
    const float a21 = matrix.m[2][1];
    const float a22 = matrix.m[2][2];

    const float determinant = a00 * (a11 * a22 - a12 * a21)
        - a01 * (a10 * a22 - a12 * a20)
        + a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(determinant) <= 1e-8f)
        return normalize(transformDirection(matrix, normal));

    const float inverseDeterminant = 1.0f / determinant;
    return normalize(Vec3f{
        ((a11 * a22 - a12 * a21) * normal.x + (a12 * a20 - a10 * a22) * normal.y + (a10 * a21 - a11 * a20) * normal.z)
            * inverseDeterminant,
        ((a21 * a02 - a01 * a22) * normal.x + (a00 * a22 - a20 * a02) * normal.y + (a01 * a20 - a00 * a21) * normal.z)
            * inverseDeterminant,
        ((a01 * a12 - a02 * a11) * normal.x + (a02 * a10 - a00 * a12) * normal.y + (a00 * a11 - a01 * a10) * normal.z)
            * inverseDeterminant
    });
}

#endif // RASTER_MATH_H
