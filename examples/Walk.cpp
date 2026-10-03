#include <Bazzalt/Script.h>
#include <Bazzalt/Math.h>
#include <Bazzalt/Entity.h>

COMPONENT(Walk) {
public:
    // Assign the cube from the hierarchy to this field in the inspector.
    PROPERTY(Bazzalt::Entity, Cube, {})
    PROPERTY(float, Speed, 5.0f)

    void OnUpdate(float deltaTime) override {
        if (!Bazzalt::Input::IsActive()) return;

        Bazzalt::Vec3 direction{
            Bazzalt::Input::GetAxis(Bazzalt::InputAxis::Horizontal),
            0.0f,
            -Bazzalt::Input::GetAxis(Bazzalt::InputAxis::Vertical)
        };
        if (direction.LengthSquared() == 0.0f) return;

        if (!Cube.IsValid()) return;
        Bazzalt::Transform transform = Cube.GetWorldTransform();
        // World-space X/Z movement; normalized to avoid faster diagonals.
        transform.Translate(direction.Normalized() * Speed * deltaTime);
        Cube.SetWorldTransform(transform);
    }
};
