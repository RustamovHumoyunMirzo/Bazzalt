# Math

```cpp
#include <Bazzalt/Math.h>
```

## Conventions

- Scalars are `float`.
- Angles are radians unless stated otherwise.
- Matrices use row-major storage and multiply column vectors.
- Transform composition is translation × rotation × scale.
- Negative Z is forward.
- `Epsilon` is `1.0e-6f`.

## Scalars and vectors

Scalar helpers are `Pi`, `Epsilon`, `ToRadians`, `ToDegrees`, `Clamp`, `Lerp`,
and `IsNearlyEqual`.

`Vec2`, `Vec3`, and `Vec4` expose uppercase fields, scalar/component
constructors, length, normalization, dot products, and arithmetic. `Vec3` adds
component multiplication and `Cross`; `Vec4::XYZ()` extracts a `Vec3`.

```cpp
Bazzalt::Vec3 direction = (target - origin).Normalized();
float facing = Bazzalt::Vec3::Dot(forward, direction);
Bazzalt::Vec3 normal = Bazzalt::Vec3::Cross(edgeA, edgeB).Normalized();
```

Equality is exact. Use tolerances for computed floating-point results.

## Matrices

`Mat3` supports identity, row/column access, transpose, determinant, inverse,
matrix multiplication, and vector multiplication.

`Mat4` supports:

- `Identity`, `Translation`, `Scaling`, `Rotation`, `Transform`;
- `Perspective`, `Orthographic`, `LookAt`;
- `Transposed`, `TryInverse`, `Inversed`;
- `TransformPoint`, `TransformDirection`;
- matrix/matrix and matrix/`Vec4` multiplication;
- contiguous `Data()` access.

`TransformPoint` includes translation/homogeneous behavior;
`TransformDirection` ignores translation. Prefer `TryInverse` when singularity
is possible. `Inversed` returns a zero matrix on failure.

## Quaternions

`Quaternion` stores `X`, `Y`, `Z`, `W`; identity is `{0,0,0,1}`. It supports
axis-angle/Euler construction, normalization, conjugate, inverse, vector
rotation, dot, `Slerp`, and multiplication.

```cpp
using namespace Bazzalt;
Quaternion yaw = Quaternion::FromAxisAngle({0,1,0}, ToRadians(90.0f));
Vec3 rotated = yaw.Rotate({0,0,-1});
Quaternion halfway = Quaternion::Slerp(Quaternion::Identity(), yaw, 0.5f);
Mat4 model = Mat4::Transform({2,0,0}, yaw, {1,1,1});
```

`Transform::Rotate` pre-multiplies an axis-angle rotation and normalizes it.
