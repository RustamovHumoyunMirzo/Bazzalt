#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Bazzalt {

inline constexpr float Pi = 3.14159265358979323846f;
inline constexpr float Epsilon = 1.0e-6f;

constexpr float ToRadians(float degrees) { return degrees * (Pi / 180.0f); }
constexpr float ToDegrees(float radians) { return radians * (180.0f / Pi); }
constexpr float Clamp(float value, float minimum, float maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}
constexpr float Lerp(float from, float to, float amount) {
    return from + (to - from) * amount;
}
inline bool IsNearlyEqual(float left, float right, float tolerance = Epsilon) {
    return std::fabs(left - right) <= tolerance;
}

struct Vec2 {
    float X = 0.0f;
    float Y = 0.0f;

    constexpr Vec2() = default;
    constexpr explicit Vec2(float value) : X(value), Y(value) {}
    constexpr Vec2(float x, float y) : X(x), Y(y) {}

    [[nodiscard]] constexpr float LengthSquared() const { return X * X + Y * Y; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Vec2 Normalized() const {
        const float length = Length();
        return length > Epsilon ? *this / length : Vec2{};
    }
    void Normalize() { *this = Normalized(); }

    [[nodiscard]] static constexpr float Dot(Vec2 left, Vec2 right) {
        return left.X * right.X + left.Y * right.Y;
    }
    [[nodiscard]] static constexpr Vec2 Lerp(Vec2 from, Vec2 to, float amount) {
        return from + (to - from) * amount;
    }

    constexpr Vec2 operator+() const { return *this; }
    constexpr Vec2 operator-() const { return {-X, -Y}; }
    constexpr Vec2 operator+(Vec2 right) const { return {X + right.X, Y + right.Y}; }
    constexpr Vec2 operator-(Vec2 right) const { return {X - right.X, Y - right.Y}; }
    constexpr Vec2 operator*(float scalar) const { return {X * scalar, Y * scalar}; }
    constexpr Vec2 operator/(float scalar) const { return {X / scalar, Y / scalar}; }
    constexpr Vec2& operator+=(Vec2 right) { X += right.X; Y += right.Y; return *this; }
    constexpr Vec2& operator-=(Vec2 right) { X -= right.X; Y -= right.Y; return *this; }
    constexpr Vec2& operator*=(float scalar) { X *= scalar; Y *= scalar; return *this; }
    constexpr Vec2& operator/=(float scalar) { X /= scalar; Y /= scalar; return *this; }
};

constexpr Vec2 operator*(float scalar, Vec2 vector) { return vector * scalar; }
constexpr bool operator==(Vec2 left, Vec2 right) { return left.X == right.X && left.Y == right.Y; }
constexpr bool operator!=(Vec2 left, Vec2 right) { return !(left == right); }

struct Vec3 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;

    constexpr Vec3() = default;
    constexpr explicit Vec3(float value) : X(value), Y(value), Z(value) {}
    constexpr Vec3(float x, float y, float z) : X(x), Y(y), Z(z) {}

    [[nodiscard]] constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Vec3 Normalized() const {
        const float length = Length();
        return length > Epsilon ? *this / length : Vec3{};
    }
    void Normalize() { *this = Normalized(); }

    [[nodiscard]] static constexpr float Dot(Vec3 left, Vec3 right) {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
    }
    [[nodiscard]] static constexpr Vec3 Cross(Vec3 left, Vec3 right) {
        return {left.Y * right.Z - left.Z * right.Y,
                left.Z * right.X - left.X * right.Z,
                left.X * right.Y - left.Y * right.X};
    }
    [[nodiscard]] static constexpr Vec3 Lerp(Vec3 from, Vec3 to, float amount) {
        return from + (to - from) * amount;
    }

    constexpr Vec3 operator+() const { return *this; }
    constexpr Vec3 operator-() const { return {-X, -Y, -Z}; }
    constexpr Vec3 operator+(Vec3 right) const { return {X + right.X, Y + right.Y, Z + right.Z}; }
    constexpr Vec3 operator-(Vec3 right) const { return {X - right.X, Y - right.Y, Z - right.Z}; }
    constexpr Vec3 operator*(Vec3 right) const { return {X * right.X, Y * right.Y, Z * right.Z}; }
    constexpr Vec3 operator*(float scalar) const { return {X * scalar, Y * scalar, Z * scalar}; }
    constexpr Vec3 operator/(float scalar) const { return {X / scalar, Y / scalar, Z / scalar}; }
    constexpr Vec3& operator+=(Vec3 right) { X += right.X; Y += right.Y; Z += right.Z; return *this; }
    constexpr Vec3& operator-=(Vec3 right) { X -= right.X; Y -= right.Y; Z -= right.Z; return *this; }
    constexpr Vec3& operator*=(float scalar) { X *= scalar; Y *= scalar; Z *= scalar; return *this; }
    constexpr Vec3& operator/=(float scalar) { X /= scalar; Y /= scalar; Z /= scalar; return *this; }
};

constexpr Vec3 operator*(float scalar, Vec3 vector) { return vector * scalar; }
constexpr bool operator==(Vec3 left, Vec3 right) {
    return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
}
constexpr bool operator!=(Vec3 left, Vec3 right) { return !(left == right); }

struct Vec4 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 0.0f;

    constexpr Vec4() = default;
    constexpr explicit Vec4(float value) : X(value), Y(value), Z(value), W(value) {}
    constexpr Vec4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
    constexpr Vec4(Vec3 xyz, float w) : X(xyz.X), Y(xyz.Y), Z(xyz.Z), W(w) {}

    [[nodiscard]] constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z + W * W; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Vec4 Normalized() const {
        const float length = Length();
        return length > Epsilon ? *this / length : Vec4{};
    }
    [[nodiscard]] constexpr Vec3 XYZ() const { return {X, Y, Z}; }
    [[nodiscard]] static constexpr float Dot(Vec4 left, Vec4 right) {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
    }

    constexpr Vec4 operator+(Vec4 right) const { return {X + right.X, Y + right.Y, Z + right.Z, W + right.W}; }
    constexpr Vec4 operator-(Vec4 right) const { return {X - right.X, Y - right.Y, Z - right.Z, W - right.W}; }
    constexpr Vec4 operator*(float scalar) const { return {X * scalar, Y * scalar, Z * scalar, W * scalar}; }
    constexpr Vec4 operator/(float scalar) const { return {X / scalar, Y / scalar, Z / scalar, W / scalar}; }
};

constexpr Vec4 operator*(float scalar, Vec4 vector) { return vector * scalar; }

struct Mat3 {
    std::array<float, 9> Values{1, 0, 0, 0, 1, 0, 0, 0, 1};

    constexpr Mat3() = default;
    constexpr explicit Mat3(float diagonal)
        : Values{diagonal, 0, 0, 0, diagonal, 0, 0, 0, diagonal} {}

    constexpr float& operator()(std::size_t row, std::size_t column) { return Values[row * 3 + column]; }
    constexpr float operator()(std::size_t row, std::size_t column) const { return Values[row * 3 + column]; }
    [[nodiscard]] static constexpr Mat3 Identity() { return Mat3{}; }
    [[nodiscard]] Mat3 Transposed() const {
        Mat3 result{0.0f};
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                result(row, column) = (*this)(column, row);
        return result;
    }
    [[nodiscard]] constexpr float Determinant() const {
        return (*this)(0, 0) * ((*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1))
             - (*this)(0, 1) * ((*this)(1, 0) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 0))
             + (*this)(0, 2) * ((*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0));
    }
    [[nodiscard]] Mat3 Inversed() const {
        const float determinant = Determinant();
        if (std::fabs(determinant) <= Epsilon) return Mat3{0.0f};
        Mat3 result{0.0f};
        result(0, 0) = (*this)(1, 1) * (*this)(2, 2) - (*this)(1, 2) * (*this)(2, 1);
        result(0, 1) = (*this)(0, 2) * (*this)(2, 1) - (*this)(0, 1) * (*this)(2, 2);
        result(0, 2) = (*this)(0, 1) * (*this)(1, 2) - (*this)(0, 2) * (*this)(1, 1);
        result(1, 0) = (*this)(1, 2) * (*this)(2, 0) - (*this)(1, 0) * (*this)(2, 2);
        result(1, 1) = (*this)(0, 0) * (*this)(2, 2) - (*this)(0, 2) * (*this)(2, 0);
        result(1, 2) = (*this)(0, 2) * (*this)(1, 0) - (*this)(0, 0) * (*this)(1, 2);
        result(2, 0) = (*this)(1, 0) * (*this)(2, 1) - (*this)(1, 1) * (*this)(2, 0);
        result(2, 1) = (*this)(0, 1) * (*this)(2, 0) - (*this)(0, 0) * (*this)(2, 1);
        result(2, 2) = (*this)(0, 0) * (*this)(1, 1) - (*this)(0, 1) * (*this)(1, 0);
        for (float& value : result.Values) value /= determinant;
        return result;
    }
};

inline Mat3 operator*(const Mat3& left, const Mat3& right) {
    Mat3 result{0.0f};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t index = 0; index < 3; ++index)
                result(row, column) += left(row, index) * right(index, column);
    return result;
}

inline Vec3 operator*(const Mat3& matrix, Vec3 vector) {
    return {matrix(0, 0) * vector.X + matrix(0, 1) * vector.Y + matrix(0, 2) * vector.Z,
            matrix(1, 0) * vector.X + matrix(1, 1) * vector.Y + matrix(1, 2) * vector.Z,
            matrix(2, 0) * vector.X + matrix(2, 1) * vector.Y + matrix(2, 2) * vector.Z};
}

struct Quaternion;

struct Mat4 {
    std::array<float, 16> Values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    constexpr Mat4() = default;
    constexpr explicit Mat4(float diagonal)
        : Values{diagonal, 0, 0, 0, 0, diagonal, 0, 0,
                 0, 0, diagonal, 0, 0, 0, 0, diagonal} {}

    constexpr float& operator()(std::size_t row, std::size_t column) { return Values[row * 4 + column]; }
    constexpr float operator()(std::size_t row, std::size_t column) const { return Values[row * 4 + column]; }
    [[nodiscard]] const float* Data() const { return Values.data(); }
    [[nodiscard]] float* Data() { return Values.data(); }
    [[nodiscard]] static constexpr Mat4 Identity() { return Mat4{}; }

    [[nodiscard]] static Mat4 Translation(Vec3 translation) {
        Mat4 result;
        result(0, 3) = translation.X; result(1, 3) = translation.Y; result(2, 3) = translation.Z;
        return result;
    }
    [[nodiscard]] static Mat4 Scaling(Vec3 scale) {
        Mat4 result{1.0f};
        result(0, 0) = scale.X; result(1, 1) = scale.Y; result(2, 2) = scale.Z;
        return result;
    }
    [[nodiscard]] static Mat4 Rotation(const Quaternion& rotation);
    [[nodiscard]] static Mat4 Transform(Vec3 position, const Quaternion& rotation, Vec3 scale);
    [[nodiscard]] static Mat4 Perspective(float verticalFieldOfViewRadians, float aspectRatio,
                                          float nearPlane, float farPlane) {
        const float tangent = std::tan(verticalFieldOfViewRadians * 0.5f);
        Mat4 result{0.0f};
        result(0, 0) = 1.0f / (aspectRatio * tangent);
        result(1, 1) = 1.0f / tangent;
        result(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
        result(2, 3) = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
        result(3, 2) = -1.0f;
        return result;
    }
    [[nodiscard]] static Mat4 Orthographic(float left, float right, float bottom, float top,
                                           float nearPlane, float farPlane) {
        Mat4 result;
        result(0, 0) = 2.0f / (right - left);
        result(1, 1) = 2.0f / (top - bottom);
        result(2, 2) = -2.0f / (farPlane - nearPlane);
        result(0, 3) = -(right + left) / (right - left);
        result(1, 3) = -(top + bottom) / (top - bottom);
        result(2, 3) = -(farPlane + nearPlane) / (farPlane - nearPlane);
        return result;
    }
    [[nodiscard]] static Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
        const Vec3 forward = (target - eye).Normalized();
        const Vec3 right = Vec3::Cross(forward, up).Normalized();
        const Vec3 correctedUp = Vec3::Cross(right, forward);
        Mat4 result;
        result(0, 0) = right.X; result(0, 1) = right.Y; result(0, 2) = right.Z;
        result(1, 0) = correctedUp.X; result(1, 1) = correctedUp.Y; result(1, 2) = correctedUp.Z;
        result(2, 0) = -forward.X; result(2, 1) = -forward.Y; result(2, 2) = -forward.Z;
        result(0, 3) = -Vec3::Dot(right, eye);
        result(1, 3) = -Vec3::Dot(correctedUp, eye);
        result(2, 3) = Vec3::Dot(forward, eye);
        return result;
    }
    [[nodiscard]] Mat4 Transposed() const {
        Mat4 result{0.0f};
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                result(row, column) = (*this)(column, row);
        return result;
    }
    [[nodiscard]] bool TryInverse(Mat4& result) const {
        float augmented[4][8]{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) augmented[row][column] = (*this)(row, column);
            augmented[row][row + 4] = 1.0f;
        }
        for (std::size_t pivot = 0; pivot < 4; ++pivot) {
            std::size_t best = pivot;
            for (std::size_t row = pivot + 1; row < 4; ++row)
                if (std::fabs(augmented[row][pivot]) > std::fabs(augmented[best][pivot])) best = row;
            if (std::fabs(augmented[best][pivot]) <= Epsilon) return false;
            if (best != pivot) for (std::size_t column = 0; column < 8; ++column) std::swap(augmented[pivot][column], augmented[best][column]);
            const float divisor = augmented[pivot][pivot];
            for (float& value : augmented[pivot]) value /= divisor;
            for (std::size_t row = 0; row < 4; ++row) {
                if (row == pivot) continue;
                const float factor = augmented[row][pivot];
                for (std::size_t column = 0; column < 8; ++column) augmented[row][column] -= factor * augmented[pivot][column];
            }
        }
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                result(row, column) = augmented[row][column + 4];
        return true;
    }
    [[nodiscard]] Mat4 Inversed() const { Mat4 result; return TryInverse(result) ? result : Mat4{0.0f}; }
    [[nodiscard]] bool Decompose(Vec3& position, Quaternion& rotation, Vec3& scale) const;
    [[nodiscard]] Vec3 TransformPoint(Vec3 point) const;
    [[nodiscard]] Vec3 TransformDirection(Vec3 direction) const;
};

inline Mat4 operator*(const Mat4& left, const Mat4& right) {
    Mat4 result{0.0f};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t index = 0; index < 4; ++index)
                result(row, column) += left(row, index) * right(index, column);
    return result;
}

inline Vec4 operator*(const Mat4& matrix, Vec4 vector) {
    return {matrix(0, 0) * vector.X + matrix(0, 1) * vector.Y + matrix(0, 2) * vector.Z + matrix(0, 3) * vector.W,
            matrix(1, 0) * vector.X + matrix(1, 1) * vector.Y + matrix(1, 2) * vector.Z + matrix(1, 3) * vector.W,
            matrix(2, 0) * vector.X + matrix(2, 1) * vector.Y + matrix(2, 2) * vector.Z + matrix(2, 3) * vector.W,
            matrix(3, 0) * vector.X + matrix(3, 1) * vector.Y + matrix(3, 2) * vector.Z + matrix(3, 3) * vector.W};
}

struct Quaternion {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 1.0f;

    constexpr Quaternion() = default;
    constexpr Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
    [[nodiscard]] static constexpr Quaternion Identity() { return {}; }
    [[nodiscard]] static Quaternion FromAxisAngle(Vec3 axis, float radians) {
        axis = axis.Normalized();
        const float half = radians * 0.5f;
        const float sine = std::sin(half);
        return {axis.X * sine, axis.Y * sine, axis.Z * sine, std::cos(half)};
    }
    [[nodiscard]] static Quaternion FromEuler(Vec3 radians) {
        const Quaternion x = FromAxisAngle({1, 0, 0}, radians.X);
        const Quaternion y = FromAxisAngle({0, 1, 0}, radians.Y);
        const Quaternion z = FromAxisAngle({0, 0, 1}, radians.Z);
        return (z * y * x).Normalized();
    }
    [[nodiscard]] constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z + W * W; }
    [[nodiscard]] float Length() const { return std::sqrt(LengthSquared()); }
    [[nodiscard]] Quaternion Normalized() const {
        const float length = Length();
        return length > Epsilon ? Quaternion{X / length, Y / length, Z / length, W / length} : Quaternion{};
    }
    void Normalize() { *this = Normalized(); }
    [[nodiscard]] constexpr Quaternion Conjugated() const { return {-X, -Y, -Z, W}; }
    [[nodiscard]] Quaternion Inversed() const {
        const float lengthSquared = LengthSquared();
        if (lengthSquared <= Epsilon) return {};
        const Quaternion conjugate = Conjugated();
        return {conjugate.X / lengthSquared, conjugate.Y / lengthSquared,
                conjugate.Z / lengthSquared, conjugate.W / lengthSquared};
    }
    [[nodiscard]] Vec3 Rotate(Vec3 vector) const {
        const Vec3 axis{X, Y, Z};
        return vector + 2.0f * Vec3::Cross(axis, Vec3::Cross(axis, vector) + W * vector);
    }
    [[nodiscard]] static constexpr float Dot(Quaternion left, Quaternion right) {
        return left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
    }
    [[nodiscard]] static Quaternion Slerp(Quaternion from, Quaternion to, float amount) {
        float dot = Dot(from, to);
        if (dot < 0.0f) { to = {-to.X, -to.Y, -to.Z, -to.W}; dot = -dot; }
        if (dot > 0.9995f) {
            return Quaternion{Bazzalt::Lerp(from.X, to.X, amount), Bazzalt::Lerp(from.Y, to.Y, amount),
                              Bazzalt::Lerp(from.Z, to.Z, amount), Bazzalt::Lerp(from.W, to.W, amount)}.Normalized();
        }
        const float angle = std::acos(Clamp(dot, -1.0f, 1.0f));
        const float denominator = std::sin(angle);
        const float fromWeight = std::sin((1.0f - amount) * angle) / denominator;
        const float toWeight = std::sin(amount * angle) / denominator;
        return {from.X * fromWeight + to.X * toWeight, from.Y * fromWeight + to.Y * toWeight,
                from.Z * fromWeight + to.Z * toWeight, from.W * fromWeight + to.W * toWeight};
    }
    constexpr Quaternion operator*(Quaternion right) const {
        return {W * right.X + X * right.W + Y * right.Z - Z * right.Y,
                W * right.Y - X * right.Z + Y * right.W + Z * right.X,
                W * right.Z + X * right.Y - Y * right.X + Z * right.W,
                W * right.W - X * right.X - Y * right.Y - Z * right.Z};
    }
};

inline Mat4 Mat4::Rotation(const Quaternion& value) {
    const Quaternion q = value.Normalized();
    const float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z;
    const float xy = q.X * q.Y, xz = q.X * q.Z, yz = q.Y * q.Z;
    const float wx = q.W * q.X, wy = q.W * q.Y, wz = q.W * q.Z;
    Mat4 result;
    result(0, 0) = 1 - 2 * (yy + zz); result(0, 1) = 2 * (xy - wz); result(0, 2) = 2 * (xz + wy);
    result(1, 0) = 2 * (xy + wz); result(1, 1) = 1 - 2 * (xx + zz); result(1, 2) = 2 * (yz - wx);
    result(2, 0) = 2 * (xz - wy); result(2, 1) = 2 * (yz + wx); result(2, 2) = 1 - 2 * (xx + yy);
    return result;
}

inline Mat4 Mat4::Transform(Vec3 position, const Quaternion& rotation, Vec3 scale) {
    return Translation(position) * Rotation(rotation) * Scaling(scale);
}

inline bool Mat4::Decompose(Vec3& position, Quaternion& rotation, Vec3& scale) const {
    if (std::fabs((*this)(3, 3)) <= Epsilon) return false;
    const float inverseW = 1.0f / (*this)(3, 3);
    position = {(*this)(0, 3) * inverseW, (*this)(1, 3) * inverseW,
                (*this)(2, 3) * inverseW};
    Vec3 x{(*this)(0, 0), (*this)(1, 0), (*this)(2, 0)};
    Vec3 y{(*this)(0, 1), (*this)(1, 1), (*this)(2, 1)};
    Vec3 z{(*this)(0, 2), (*this)(1, 2), (*this)(2, 2)};
    scale = {x.Length(), y.Length(), z.Length()};
    if (scale.X <= Epsilon || scale.Y <= Epsilon || scale.Z <= Epsilon) return false;
    if (Vec3::Dot(Vec3::Cross(x, y), z) < 0.0f) { scale.X = -scale.X; x = -x; }
    x /= scale.X; y /= scale.Y; z /= scale.Z;
    const float trace = x.X + y.Y + z.Z;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        rotation = {(y.Z - z.Y) / s, (z.X - x.Z) / s, (x.Y - y.X) / s, 0.25f * s};
    } else if (x.X > y.Y && x.X > z.Z) {
        const float s = std::sqrt(1.0f + x.X - y.Y - z.Z) * 2.0f;
        rotation = {0.25f * s, (y.X + x.Y) / s, (z.X + x.Z) / s, (y.Z - z.Y) / s};
    } else if (y.Y > z.Z) {
        const float s = std::sqrt(1.0f + y.Y - x.X - z.Z) * 2.0f;
        rotation = {(y.X + x.Y) / s, 0.25f * s, (z.Y + y.Z) / s, (z.X - x.Z) / s};
    } else {
        const float s = std::sqrt(1.0f + z.Z - x.X - y.Y) * 2.0f;
        rotation = {(z.X + x.Z) / s, (z.Y + y.Z) / s, 0.25f * s, (x.Y - y.X) / s};
    }
    rotation.Normalize();
    return true;
}

inline Vec3 Mat4::TransformPoint(Vec3 point) const {
    const Vec4 result = *this * Vec4{point, 1.0f};
    return std::fabs(result.W) > Epsilon ? result.XYZ() * (1.0f / result.W) : result.XYZ();
}

inline Vec3 Mat4::TransformDirection(Vec3 direction) const {
    return (*this * Vec4{direction, 0.0f}).XYZ();
}

using Vector2 = Vec2;
using Vector3 = Vec3;
using Vector4 = Vec4;
using Matrix3 = Mat3;
using Matrix4 = Mat4;

} // namespace Bazzalt
