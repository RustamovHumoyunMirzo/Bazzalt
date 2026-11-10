#include "Bazzalt/Spline.h"
#include <cmath>
#include <limits>
#include <numeric>
#include <set>
#include <mutex>
namespace Bazzalt {
struct Spline::ArcCache {
    struct Entry {
        float Parameter, Distance;
        Vec3 Position;
    };
    std::once_flag Ready;
    std::vector<Entry> Values;
};
Spline::Spline() : m_cache(std::make_shared<ArcCache>()) {}

namespace {
bool Finite(Vec3 value) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}
float Parameter(float value, bool closed) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Spline parameter must be finite");
    if (closed && (value < 0 || value > 1))
        return value - std::floor(value);
    return std::clamp(value, 0.f, 1.f);
}
} // namespace
Spline::Spline(std::vector<SplinePoint> points, bool closed) : m_closed(closed) {
    SetPoints(std::move(points));
}
void Spline::Validate(const SplinePoint &p) {
    if (!Finite(p.Position) || !Finite(p.InTangent) || !Finite(p.OutTangent) || !Finite(p.Up) ||
        !Finite(p.Scale) || !std::isfinite(p.Roll) || p.Position.Length() > 2000000 ||
        p.InTangent.Length() > 1000000 || p.OutTangent.Length() > 1000000 || p.Up.LengthSquared() < Epsilon)
        throw std::invalid_argument("Invalid spline point");
}
void Spline::SetPoints(std::vector<SplinePoint> points) {
    if (points.size() > 1024)
        throw std::length_error("Spline supports at most 1024 points");
    for (const auto &point : points)
        Validate(point);
    auto cache = std::make_shared<ArcCache>();
    m_points = std::move(points);
    m_cache = std::move(cache);
}
void Spline::AddPoint(const SplinePoint &point) {
    Validate(point);
    if (m_points.size() == 1024)
        throw std::length_error("Spline point limit");
    m_points.push_back(point);
    m_cache = std::make_shared<ArcCache>();
}
void Spline::SetPoint(std::size_t index, const SplinePoint &point) {
    Validate(point);
    m_points.at(index) = point;
    m_cache = std::make_shared<ArcCache>();
}
void Spline::RemovePoint(std::size_t index) {
    if (index >= m_points.size())
        throw std::out_of_range("Spline point");
    m_points.erase(m_points.begin() + index);
    m_cache = std::make_shared<ArcCache>();
}
void Spline::Clear() {
    m_points.clear();
    m_cache = std::make_shared<ArcCache>();
}
void Spline::SetClosed(bool closed) {
    m_closed = closed;
    m_cache = std::make_shared<ArcCache>();
}
unsigned Spline::Segment(float &t) const {
    if (m_points.empty())
        throw std::logic_error("Spline has no points");
    t = Parameter(t, m_closed);
    const unsigned count = unsigned(m_points.size() - (m_closed ? 0 : 1));
    if (!count) {
        t = 0;
        return 0;
    }
    float scaled = t * count;
    unsigned index = std::min(unsigned(scaled), count - 1);
    t = scaled - index;
    return index;
}
Vec3 Spline::Evaluate(float t) const {
    auto index = Segment(t);
    const auto &a = m_points[index];
    const auto &b = m_points[(index + 1) % m_points.size()];
    if (a.OutTangent.LengthSquared() + b.InTangent.LengthSquared() < Epsilon)
        return a.Position + (b.Position - a.Position) * t;
    float u = 1 - t;
    return a.Position * (u * u * u) + (a.Position + a.OutTangent) * (3 * u * u * t) +
           (b.Position + b.InTangent) * (3 * u * t * t) + b.Position * (t * t * t);
}
Vec3 Spline::EvaluateTangent(float t) const {
    auto index = Segment(t);
    const auto &a = m_points[index];
    const auto &b = m_points[(index + 1) % m_points.size()];
    float u = 1 - t;
    Vec3 tangent = b.Position - a.Position;
    if (a.OutTangent.LengthSquared() + b.InTangent.LengthSquared() > Epsilon)
        tangent = a.OutTangent * (3 * u * u) +
                  (b.Position + b.InTangent - a.Position - a.OutTangent) * (6 * u * t) -
                  b.InTangent * (3 * t * t);
    if (tangent.LengthSquared() < Epsilon)
        tangent = b.Position - a.Position;
    return tangent.LengthSquared() < Epsilon ? Vec3{0, 0, -1} : tangent.Normalized();
}
Transform Spline::EvaluateTransform(float parameter, const Mat4 &world) const {
    auto position = Evaluate(parameter), forward = EvaluateTangent(parameter);
    float t = parameter;
    auto index = Segment(t);
    const auto &a = m_points[index];
    const auto &b = m_points[(index + 1) % m_points.size()];
    Vec3 up = a.Up + (b.Up - a.Up) * t;
    if (Vec3::Cross(forward, up).LengthSquared() < Epsilon)
        up = std::abs(forward.Y) < .99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    auto matrix = Mat4::LookAt(position, position + forward, up).Inversed();
    Transform local;
    if (!matrix.Decompose(local.Position, local.Rotation, local.Scale))
        throw std::invalid_argument("Invalid spline frame");
    local.Rotation = local.Rotation * Quaternion::FromAxisAngle({0, 0, -1}, a.Roll + (b.Roll - a.Roll) * t);
    local.Scale = a.Scale + (b.Scale - a.Scale) * t;
    Transform result;
    if (!(world * local.GetMatrix()).Decompose(result.Position, result.Rotation, result.Scale))
        throw std::invalid_argument("Singular spline transform");
    return result;
}
std::vector<Vec3> Spline::Sample(unsigned subdivisions) const {
    if (subdivisions < 1 || subdivisions > 64)
        throw std::invalid_argument("Spline subdivisions must be 1 to 64");
    std::vector<Vec3> result;
    if (m_points.empty())
        return result;
    result.push_back(m_points.front().Position);
    if (m_points.size() == 1)
        return result;
    unsigned segments = unsigned(m_points.size() - (m_closed ? 0 : 1));
    for (unsigned index = 0; index < segments; ++index) {
        const auto &a = m_points[index];
        const auto &b = m_points[(index + 1) % m_points.size()];
        unsigned steps =
            a.OutTangent.LengthSquared() + b.InTangent.LengthSquared() > Epsilon ? subdivisions : 1;
        for (unsigned step = 1; step <= steps; ++step)
            result.push_back(Evaluate((index + float(step) / steps) / segments));
    }
    return result;
}
void Spline::BuildArc() const {
    std::call_once(m_cache->Ready, [&] {
        std::vector<ArcCache::Entry> values;
        if (m_points.empty())
            return;
        unsigned segments = unsigned(m_points.size() - (m_closed ? 0 : 1));
        unsigned count = std::max(1u, segments * 32);
        Vec3 previous = Evaluate(0);
        float distance = 0;
        values.push_back({0, 0, previous});
        for (unsigned i = 1; i <= count; ++i) {
            float t = float(i) / count;
            auto point = Evaluate(t);
            distance += (point - previous).Length();
            values.push_back({t, distance, point});
            previous = point;
        }
        m_cache->Values = std::move(values);
    });
}
float Spline::GetLength() const {
    BuildArc();
    return m_cache->Values.empty() ? 0 : m_cache->Values.back().Distance;
}
float Spline::GetParameterAtDistance(float distance) const {
    if (!std::isfinite(distance))
        throw std::invalid_argument("Invalid spline distance");
    BuildArc();
    float length = GetLength();
    if (length < Epsilon)
        return 0;
    if (m_closed)
        distance = std::fmod(std::fmod(distance, length) + length, length);
    else
        distance = std::clamp(distance, 0.f, length);
    auto where =
        std::lower_bound(m_cache->Values.begin(), m_cache->Values.end(), distance,
                         [](const ArcCache::Entry &arc, float value) { return arc.Distance < value; });
    if (where == m_cache->Values.begin())
        return 0;
    if (where == m_cache->Values.end())
        return 1;
    auto previous = where - 1;
    float span = where->Distance - previous->Distance;
    return previous->Parameter + (where->Parameter - previous->Parameter) *
                                     (span > Epsilon ? (distance - previous->Distance) / span : 0);
}
Transform Spline::EvaluateDistance(float distance, const Mat4 &world) const {
    return EvaluateTransform(GetParameterAtDistance(distance), world);
}
SplineQuery Spline::FindClosestPoint(Vec3 position, const Mat4 &world) const {
    if (!Finite(position))
        throw std::invalid_argument("Invalid spline query position");
    BuildArc();
    if (m_cache->Values.empty())
        throw std::logic_error("Spline has no points");
    float best = std::numeric_limits<float>::max(), parameter = 0, along = 0;
    for (unsigned i = 1; i < m_cache->Values.size(); ++i) {
        auto a = world.TransformPoint(m_cache->Values[i - 1].Position),
             b = world.TransformPoint(m_cache->Values[i].Position), delta = b - a;
        float t = delta.LengthSquared() > Epsilon
                      ? std::clamp(Vec3::Dot(position - a, delta) / delta.LengthSquared(), 0.f, 1.f)
                      : 0;
        float distance = (position - a - delta * t).LengthSquared();
        if (distance < best) {
            best = distance;
            parameter = m_cache->Values[i - 1].Parameter +
                        (m_cache->Values[i].Parameter - m_cache->Values[i - 1].Parameter) * t;
            along = m_cache->Values[i - 1].Distance +
                    (m_cache->Values[i].Distance - m_cache->Values[i - 1].Distance) * t;
        }
    }
    auto found = world.TransformPoint(Evaluate(parameter)),
         tangent = world.TransformDirection(EvaluateTangent(parameter)).Normalized();
    return {parameter, (position - found).Length(), along, found, tangent};
}
std::vector<Transform> Spline::SampleTransforms(unsigned count, bool evenly, const Mat4 &world) const {
    if (count > 65536)
        throw std::length_error("Spline placement limit");
    std::vector<Transform> result;
    result.reserve(count);
    float length = GetLength();
    for (unsigned i = 0; i < count; ++i) {
        float t = count <= 1 ? 0 : float(i) / (m_closed ? count : count - 1);
        result.push_back(EvaluateTransform(evenly ? GetParameterAtDistance(t * length) : t, world));
    }
    return result;
}
bool Spline::PlaceEntities(const std::vector<Entity> &entities, float spacing, float start,
                           const Mat4 &world) const {
    if (entities.size() > 65536 || !std::isfinite(spacing) || spacing < 0 || !std::isfinite(start))
        return false;
    if (m_points.empty())
        return entities.empty();
    std::vector<Transform> previous, next;
    std::set<std::pair<Scene *, Entity::Id>> unique;
    std::vector<unsigned> order(entities.size());
    std::iota(order.begin(), order.end(), 0);
    for (unsigned i = 0; i < entities.size(); ++i) {
        if (!entities[i] || entities[i].GetUUID().IsRoot() ||
            !unique.emplace(entities[i].GetScene(), entities[i].GetId()).second)
            return false;
        previous.push_back(entities[i].GetComponent<Transform>());
        next.push_back(EvaluateDistance(start + spacing * i, world));
    }
    std::vector<unsigned> depths(entities.size());
    for (unsigned index = 0; index < entities.size(); ++index)
        for (auto parent = entities[index].GetParent(); parent; parent = parent.GetParent())
            ++depths[index];
    std::stable_sort(order.begin(), order.end(),
                     [&](unsigned a, unsigned b) { return depths[a] < depths[b]; });
    for (auto index : order)
        if (!entities[index].SetWorldTransform(next[index])) {
            for (auto restore : order)
                entities[restore].GetComponent<Transform>() = previous[restore];
            return false;
        }
    return true;
}
} // namespace Bazzalt
