#include "Script/LuaBindings.h"
#include "Bazzalt/Spline.h"
namespace Bazzalt::Runtime {
void BindLuaSpline(sol::table api) {
    auto point = api.new_usertype<SplinePoint>("SplinePoint", sol::constructors<SplinePoint()>());
    point["Position"] = &SplinePoint::Position;
    point["InTangent"] = &SplinePoint::InTangent;
    point["OutTangent"] = &SplinePoint::OutTangent;
    point["Up"] = &SplinePoint::Up;
    point["Scale"] = &SplinePoint::Scale;
    point["Roll"] = &SplinePoint::Roll;
    auto hit = api.new_usertype<SplineQuery>("SplineQuery", sol::no_constructor);
    hit["Parameter"] = &SplineQuery::Parameter;
    hit["Distance"] = &SplineQuery::Distance;
    hit["DistanceAlongSpline"] = &SplineQuery::DistanceAlongSpline;
    hit["Position"] = &SplineQuery::Position;
    hit["Tangent"] = &SplineQuery::Tangent;
    auto spline = api.new_usertype<Spline>("Spline", sol::constructors<Spline()>());
    spline[sol::meta_function::construct] =
        sol::factories([] { return Spline{}; },
                       [](sol::as_table_t<std::vector<SplinePoint>> points, sol::optional<bool> closed) {
                           return Spline(std::move(points.value()), closed.value_or(false));
                       });
    spline["AddPoint"] = &Spline::AddPoint;
    spline["SetPoint"] = &Spline::SetPoint;
    spline["SetPoints"] = [](Spline &value, sol::as_table_t<std::vector<SplinePoint>> points) {
        value.SetPoints(std::move(points.value()));
    };
    spline["GetPoints"] = [](const Spline &value) { return sol::as_table(value.GetPoints()); };
    spline["GetPointCount"] = &Spline::GetPointCount;
    spline["RemovePoint"] = &Spline::RemovePoint;
    spline["Clear"] = &Spline::Clear;
    spline["SetClosed"] = &Spline::SetClosed;
    spline["IsClosed"] = &Spline::IsClosed;
    spline["GetLength"] = &Spline::GetLength;
    spline["Evaluate"] = &Spline::Evaluate;
    spline["EvaluateTangent"] = &Spline::EvaluateTangent;
    spline["EvaluateTransform"] = [](const Spline &value, float t, sol::optional<Mat4> world) {
        return value.EvaluateTransform(t, world.value_or(Mat4{}));
    };
    spline["EvaluateDistance"] = [](const Spline &value, float d, sol::optional<Mat4> world) {
        return value.EvaluateDistance(d, world.value_or(Mat4{}));
    };
    spline["GetParameterAtDistance"] = &Spline::GetParameterAtDistance;
    spline["FindClosestPoint"] = [](const Spline &value, Vec3 p, sol::optional<Mat4> world) {
        return value.FindClosestPoint(p, world.value_or(Mat4{}));
    };
    spline["Sample"] = [](const Spline &value, sol::optional<unsigned> divisions) {
        return sol::as_table(value.Sample(divisions.value_or(16)));
    };
    spline["SampleTransforms"] = [](const Spline &value, unsigned count, sol::optional<bool> evenly,
                                    sol::optional<Mat4> world) {
        return sol::as_table(value.SampleTransforms(count, evenly.value_or(true), world.value_or(Mat4{})));
    };
    spline["PlaceEntities"] = [](const Spline &value, sol::as_table_t<std::vector<Entity>> entities,
                                 sol::optional<float> spacing, sol::optional<float> start,
                                 sol::optional<Mat4> world) {
        return value.PlaceEntities(entities.value(), spacing.value_or(1), start.value_or(0),
                                   world.value_or(Mat4{}));
    };
}
} // namespace Bazzalt::Runtime
