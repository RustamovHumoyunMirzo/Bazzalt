"""Renderer-independent geometry and interaction math for editor 3D gizmos."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from math import atan2, isfinite, sqrt

_EPSILON = 1.0e-8


class GizmoMode(Enum):
    Select = "select"
    Translate = "translate"
    Rotate = "rotate"
    Scale = "scale"


class GizmoHandle(Enum):
    X = "x"
    Y = "y"
    Z = "z"
    XY = "xy"
    YZ = "yz"
    ZX = "zx"
    Screen = "screen"
    Uniform = "uniform"


@dataclass(frozen=True, slots=True)
class Vec3:
    X: float = 0.0
    Y: float = 0.0
    Z: float = 0.0

    def __add__(self, other: "Vec3") -> "Vec3":
        return Vec3(self.X + other.X, self.Y + other.Y, self.Z + other.Z)

    def __sub__(self, other: "Vec3") -> "Vec3":
        return Vec3(self.X - other.X, self.Y - other.Y, self.Z - other.Z)

    def __mul__(self, scalar: float) -> "Vec3":
        return Vec3(self.X * scalar, self.Y * scalar, self.Z * scalar)

    __rmul__ = __mul__

    def Dot(self, other: "Vec3") -> float:
        return self.X * other.X + self.Y * other.Y + self.Z * other.Z

    def Cross(self, other: "Vec3") -> "Vec3":
        return Vec3(
            self.Y * other.Z - self.Z * other.Y,
            self.Z * other.X - self.X * other.Z,
            self.X * other.Y - self.Y * other.X,
        )

    def Length(self) -> float:
        return sqrt(self.Dot(self))

    def Normalized(self) -> "Vec3":
        length = self.Length()
        if length <= _EPSILON:
            raise ValueError("cannot normalize a zero-length vector")
        return self * (1.0 / length)


@dataclass(frozen=True, slots=True)
class Ray:
    Origin: Vec3
    Direction: Vec3

    def __post_init__(self) -> None:
        object.__setattr__(self, "Direction", self.Direction.Normalized())

    def PointAt(self, distance: float) -> Vec3:
        return self.Origin + self.Direction * distance


@dataclass(frozen=True, slots=True)
class Plane:
    Point: Vec3
    Normal: Vec3

    def __post_init__(self) -> None:
        object.__setattr__(self, "Normal", self.Normal.Normalized())


@dataclass(frozen=True, slots=True)
class GizmoDelta:
    Translation: Vec3 = field(default_factory=Vec3)
    RotationAxis: Vec3 = field(default_factory=Vec3)
    RotationRadians: float = 0.0
    Scale: Vec3 = field(default_factory=lambda: Vec3(1.0, 1.0, 1.0))


def AxisVector(handle: GizmoHandle) -> Vec3:
    axes = {
        GizmoHandle.X: Vec3(1.0, 0.0, 0.0),
        GizmoHandle.Y: Vec3(0.0, 1.0, 0.0),
        GizmoHandle.Z: Vec3(0.0, 0.0, 1.0),
    }
    if handle not in axes:
        raise ValueError(f"{handle.value!r} is not an axis handle")
    return axes[handle]


def RayPlaneIntersection(ray: Ray, plane: Plane) -> Vec3 | None:
    denominator = ray.Direction.Dot(plane.Normal)
    if abs(denominator) <= _EPSILON:
        return None
    distance = (plane.Point - ray.Origin).Dot(plane.Normal) / denominator
    return ray.PointAt(distance) if distance >= 0.0 else None


def ClosestRayAxisParameter(ray: Ray, origin: Vec3, axis: Vec3) -> float | None:
    """Return the signed axis parameter nearest a forward ray."""
    axis = axis.Normalized()
    direction = ray.Direction
    offset = ray.Origin - origin
    cross = direction.Dot(axis)
    denominator = 1.0 - cross * cross
    if abs(denominator) <= _EPSILON:
        return None
    ray_parameter = (cross * offset.Dot(axis) - offset.Dot(direction)) / denominator
    axis_parameter = (offset.Dot(axis) - cross * offset.Dot(direction)) / denominator
    if ray_parameter < 0.0:
        return (ray.Origin - origin).Dot(axis)
    return axis_parameter


def DistanceToAxisHandle(ray: Ray, origin: Vec3, axis: Vec3,
                         length: float = 1.0) -> float:
    axis = axis.Normalized()
    parameter = ClosestRayAxisParameter(ray, origin, axis)
    if parameter is None:
        parameter = 0.0
    axis_point = origin + axis * max(0.0, min(length, parameter))
    ray_distance = max(0.0, (axis_point - ray.Origin).Dot(ray.Direction))
    return (axis_point - ray.PointAt(ray_distance)).Length()


def PickAxis(ray: Ray, origin: Vec3, length: float,
             tolerance: float) -> GizmoHandle | None:
    if length <= 0.0 or tolerance < 0.0:
        raise ValueError("length must be positive and tolerance cannot be negative")
    candidates = [
        (DistanceToAxisHandle(ray, origin, AxisVector(handle), length), handle)
        for handle in (GizmoHandle.X, GizmoHandle.Y, GizmoHandle.Z)
    ]
    distance, handle = min(candidates, key=lambda item: item[0])
    return handle if distance <= tolerance else None


def PickRotationAxis(ray: Ray, origin: Vec3, radius: float,
                     tolerance: float) -> GizmoHandle | None:
    """Pick the nearest axis ring by intersecting its rotation plane."""
    if radius <= 0.0 or tolerance < 0.0:
        raise ValueError("radius must be positive and tolerance cannot be negative")
    candidates: list[tuple[float, GizmoHandle]] = []
    for handle in (GizmoHandle.X, GizmoHandle.Y, GizmoHandle.Z):
        hit = RayPlaneIntersection(ray, Plane(origin, AxisVector(handle)))
        if hit is not None:
            candidates.append((abs((hit - origin).Length() - radius), handle))
    if not candidates:
        return None
    distance, handle = min(candidates, key=lambda item: item[0])
    return handle if distance <= tolerance else None


def TranslationAlongAxis(start: Ray, current: Ray, origin: Vec3,
                         axis: Vec3) -> Vec3:
    first = ClosestRayAxisParameter(start, origin, axis)
    second = ClosestRayAxisParameter(current, origin, axis)
    if first is None or second is None:
        return Vec3()
    return axis.Normalized() * (second - first)


def TranslationOnPlane(start: Ray, current: Ray, plane: Plane) -> Vec3:
    first = RayPlaneIntersection(start, plane)
    second = RayPlaneIntersection(current, plane)
    return second - first if first is not None and second is not None else Vec3()


def RotationAroundAxis(start: Ray, current: Ray, origin: Vec3,
                       axis: Vec3) -> float:
    axis = axis.Normalized()
    plane = Plane(origin, axis)
    first = RayPlaneIntersection(start, plane)
    second = RayPlaneIntersection(current, plane)
    if first is None or second is None:
        return 0.0
    first_vector, second_vector = first - origin, second - origin
    if first_vector.Length() <= _EPSILON or second_vector.Length() <= _EPSILON:
        return 0.0
    first_vector, second_vector = first_vector.Normalized(), second_vector.Normalized()
    return atan2(axis.Dot(first_vector.Cross(second_vector)),
                 max(-1.0, min(1.0, first_vector.Dot(second_vector))))


def ScaleAlongAxis(start: Ray, current: Ray, origin: Vec3, axis: Vec3,
                   reference_length: float = 1.0) -> float:
    if reference_length <= _EPSILON:
        raise ValueError("reference_length must be positive")
    first = ClosestRayAxisParameter(start, origin, axis)
    second = ClosestRayAxisParameter(current, origin, axis)
    if first is None or second is None:
        return 1.0
    return max(_EPSILON, 1.0 + (second - first) / reference_length)


def UniformScale(start: Ray, current: Ray, origin: Vec3,
                 view_normal: Vec3) -> float:
    plane = Plane(origin, view_normal)
    first = RayPlaneIntersection(start, plane)
    second = RayPlaneIntersection(current, plane)
    if first is None or second is None:
        return 1.0
    start_radius = (first - origin).Length()
    if start_radius <= _EPSILON:
        return 1.0
    return max(_EPSILON, (second - origin).Length() / start_radius)


def Snap(value: float, increment: float | None) -> float:
    if increment is None or increment <= _EPSILON:
        return value
    snapped = round(value / increment) * increment
    return snapped if isfinite(snapped) else value


class GizmoDrag:
    """Immutable-start drag solver suitable for a Scene viewport controller."""

    def __init__(self, mode: GizmoMode, handle: GizmoHandle, start_ray: Ray,
                 origin: Vec3, view_normal: Vec3) -> None:
        if mode is GizmoMode.Select:
            raise ValueError("Select mode does not create a gizmo drag")
        self.Mode = mode
        self.Handle = handle
        self.StartRay = start_ray
        self.Origin = origin
        self.ViewNormal = view_normal.Normalized()

    def Calculate(self, current_ray: Ray, translation_snap: float | None = None,
                  rotation_snap: float | None = None,
                  scale_snap: float | None = None) -> GizmoDelta:
        if self.Mode is GizmoMode.Translate:
            delta = self._Translation(current_ray)
            if translation_snap:
                delta = Vec3(*(Snap(value, translation_snap)
                               for value in (delta.X, delta.Y, delta.Z)))
            return GizmoDelta(Translation=delta)
        if self.Mode is GizmoMode.Rotate:
            axis = AxisVector(self.Handle)
            angle = Snap(RotationAroundAxis(
                self.StartRay, current_ray, self.Origin, axis), rotation_snap)
            return GizmoDelta(RotationAxis=axis, RotationRadians=angle)
        factor = self._Scale(current_ray)
        factor = max(_EPSILON, Snap(factor, scale_snap))
        if self.Handle is GizmoHandle.Uniform:
            scale = Vec3(factor, factor, factor)
        else:
            scale = Vec3(
                factor if self.Handle is GizmoHandle.X else 1.0,
                factor if self.Handle is GizmoHandle.Y else 1.0,
                factor if self.Handle is GizmoHandle.Z else 1.0,
            )
        return GizmoDelta(Scale=scale)

    def _Translation(self, current_ray: Ray) -> Vec3:
        if self.Handle in (GizmoHandle.X, GizmoHandle.Y, GizmoHandle.Z):
            return TranslationAlongAxis(
                self.StartRay, current_ray, self.Origin, AxisVector(self.Handle))
        normals = {
            GizmoHandle.XY: Vec3(0.0, 0.0, 1.0),
            GizmoHandle.YZ: Vec3(1.0, 0.0, 0.0),
            GizmoHandle.ZX: Vec3(0.0, 1.0, 0.0),
            GizmoHandle.Screen: self.ViewNormal,
        }
        return TranslationOnPlane(
            self.StartRay, current_ray, Plane(self.Origin, normals[self.Handle]))

    def _Scale(self, current_ray: Ray) -> float:
        if self.Handle is GizmoHandle.Uniform:
            return UniformScale(
                self.StartRay, current_ray, self.Origin, self.ViewNormal)
        return ScaleAlongAxis(
            self.StartRay, current_ray, self.Origin, AxisVector(self.Handle))


__all__ = [
    "AxisVector", "ClosestRayAxisParameter", "DistanceToAxisHandle", "GizmoDelta",
    "GizmoDrag", "GizmoHandle", "GizmoMode", "PickAxis", "PickRotationAxis", "Plane", "Ray",
    "RayPlaneIntersection", "RotationAroundAxis", "ScaleAlongAxis", "Snap",
    "TranslationAlongAxis", "TranslationOnPlane", "UniformScale", "Vec3",
]
