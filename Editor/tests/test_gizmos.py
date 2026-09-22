from __future__ import annotations

from math import isclose, pi
import unittest

from Editor.gui.gizmos import (
    GizmoDrag,
    GizmoHandle,
    GizmoMode,
    PickAxis,
    Plane,
    Ray,
    RayPlaneIntersection,
    RotationAroundAxis,
    TranslationAlongAxis,
    Vec3,
)


class GizmoMathTests(unittest.TestCase):
    def test_ray_plane_intersection(self) -> None:
        hit = RayPlaneIntersection(
            Ray(Vec3(2, 3, 5), Vec3(0, 0, -1)),
            Plane(Vec3(), Vec3(0, 0, 1)),
        )
        self.assertEqual(hit, Vec3(2, 3, 0))

    def test_axis_translation_uses_closest_ray_points(self) -> None:
        start = Ray(Vec3(0, 1, 5), Vec3(0, -1, -5))
        current = Ray(Vec3(0, 1, 5), Vec3(1, -1, -5))
        delta = TranslationAlongAxis(start, current, Vec3(), Vec3(1, 0, 0))
        self.assertTrue(isclose(delta.X, 1.0, abs_tol=1.0e-6))
        self.assertTrue(isclose(delta.Y, 0.0, abs_tol=1.0e-6))

    def test_rotation_is_signed(self) -> None:
        start = Ray(Vec3(1, 0, 5), Vec3(0, 0, -1))
        current = Ray(Vec3(0, 1, 5), Vec3(0, 0, -1))
        angle = RotationAroundAxis(start, current, Vec3(), Vec3(0, 0, 1))
        self.assertTrue(isclose(angle, pi / 2, abs_tol=1.0e-6))

    def test_axis_picker_chooses_nearest_handle(self) -> None:
        ray = Ray(Vec3(0.5, 0.02, 2), Vec3(0, 0, -1))
        self.assertEqual(PickAxis(ray, Vec3(), 1.0, 0.05), GizmoHandle.X)

    def test_drag_outputs_transform_delta_without_runtime_dependency(self) -> None:
        start = Ray(Vec3(0, 1, 5), Vec3(0, -1, -5))
        current = Ray(Vec3(0, 1, 5), Vec3(1, -1, -5))
        drag = GizmoDrag(
            GizmoMode.Translate, GizmoHandle.X, start, Vec3(), Vec3(0, 0, 1)
        )
        result = drag.Calculate(current, translation_snap=0.5)
        self.assertEqual(result.Translation, Vec3(1.0, 0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
