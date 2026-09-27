#include <cassert>

#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Math.h"

namespace {

bool Near(float left, float right) {
    return Bazzalt::IsNearlyEqual(left, right, 1.0e-4f);
}

bool Near(Bazzalt::Vec3 left, Bazzalt::Vec3 right) {
    return Near(left.X, right.X) && Near(left.Y, right.Y) && Near(left.Z, right.Z);
}

} // namespace

int main() {
    using namespace Bazzalt;

    assert(Near(Vec3::Cross({1, 0, 0}, {0, 1, 0}), {0, 0, 1}));
    assert(Near(Vec3{3, 0, 4}.Normalized().Length(), 1.0f));

    const Quaternion rotation = Quaternion::FromAxisAngle({0, 1, 0}, Pi * 0.5f);
    assert(Near(rotation.Rotate({0, 0, -1}), {-1, 0, 0}));

    const Mat4 transform = Mat4::Transform({2, 3, 4}, rotation, {2, 2, 2});
    const Vec3 transformed = transform.TransformPoint({0, 0, -1});
    assert(Near(transformed, {0, 3, 4}));
    assert(Near(transform.Inversed().TransformPoint(transformed), {0, 0, -1}));

    Transform component;
    component.Position = {2, 3, 4};
    component.Rotation = rotation;
    component.Scale = {2, 2, 2};
    assert(Near(component.GetMatrix().TransformPoint({0, 0, -1}), transformed));

    Bazzalt::Vec3 decomposedPosition, decomposedScale;
    Bazzalt::Quaternion decomposedRotation;
    const Bazzalt::Mat4 composed = Bazzalt::Mat4::Transform(
        {3, -2, 7}, Bazzalt::Quaternion::FromEuler({.2f, -.4f, .7f}), {2, 3, 4});
    assert(composed.Decompose(decomposedPosition, decomposedRotation, decomposedScale));
    assert(Near(decomposedPosition, {3, -2, 7}));
    assert(Near(decomposedScale, {2, 3, 4}));
    assert(Near(Bazzalt::Mat4::Transform(decomposedPosition, decomposedRotation,
                                        decomposedScale).TransformPoint({1, 2, 3}),
                composed.TransformPoint({1, 2, 3})));
    assert(Near(component.GetForward(), {-1, 0, 0}));

    return 0;
}
