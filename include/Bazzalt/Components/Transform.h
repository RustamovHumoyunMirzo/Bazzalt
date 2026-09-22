#pragma once

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"

namespace Bazzalt {

struct Transform : Component {
    Vec3 Position{};
    Quaternion Rotation{};
    Vec3 Scale{1.0f};

    [[nodiscard]] Mat4 GetMatrix() const {
        return Mat4::Transform(Position, Rotation, Scale);
    }

    [[nodiscard]] Vec3 GetForward() const { return Rotation.Rotate({0.0f, 0.0f, -1.0f}); }
    [[nodiscard]] Vec3 GetRight() const { return Rotation.Rotate({1.0f, 0.0f, 0.0f}); }
    [[nodiscard]] Vec3 GetUp() const { return Rotation.Rotate({0.0f, 1.0f, 0.0f}); }

    void Translate(Vec3 translation) { Position += translation; }
    void Rotate(Vec3 axis, float radians) {
        Rotation = (Quaternion::FromAxisAngle(axis, radians) * Rotation).Normalized();
    }
};

template<>
struct ComponentInheritance<Transform> {
    static constexpr ComponentInheritanceMode Mode = ComponentInheritanceMode::Composed;
};

} // namespace Bazzalt
