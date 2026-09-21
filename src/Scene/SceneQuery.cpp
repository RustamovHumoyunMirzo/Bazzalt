#include "Bazzalt/Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/SceneQueryBounds.h"

namespace Bazzalt {
namespace {

bool IsFinite(Vec3 value) {
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

bool Accept(Entity entity, const SceneQueryBounds& bounds, const SceneQueryOptions& options) {
    return (options.IncludeDisabled || bounds.Enabled) &&
           (bounds.LayerMask & options.LayerMask) != 0 &&
           (!options.Filter || options.Filter(entity));
}

bool IntersectBox(Vec3 origin, Vec3 direction, Vec3 minimum, Vec3 maximum,
                  float& distance, Vec3& normal, bool& inside) {
    float nearDistance = -std::numeric_limits<float>::infinity();
    float farDistance = std::numeric_limits<float>::infinity();
    Vec3 nearNormal{}, farNormal{};
    inside = origin.X >= minimum.X && origin.X <= maximum.X &&
             origin.Y >= minimum.Y && origin.Y <= maximum.Y &&
             origin.Z >= minimum.Z && origin.Z <= maximum.Z;
    for (int axis = 0; axis < 3; ++axis) {
        const float o = axis == 0 ? origin.X : axis == 1 ? origin.Y : origin.Z;
        const float d = axis == 0 ? direction.X : axis == 1 ? direction.Y : direction.Z;
        const float lo = axis == 0 ? minimum.X : axis == 1 ? minimum.Y : minimum.Z;
        const float hi = axis == 0 ? maximum.X : axis == 1 ? maximum.Y : maximum.Z;
        if (std::fabs(d) <= Epsilon) { if (o < lo || o > hi) return false; continue; }
        float first = (lo - o) / d, second = (hi - o) / d;
        Vec3 firstNormal{}, secondNormal{};
        (axis == 0 ? firstNormal.X : axis == 1 ? firstNormal.Y : firstNormal.Z) = -1.0f;
        (axis == 0 ? secondNormal.X : axis == 1 ? secondNormal.Y : secondNormal.Z) = 1.0f;
        if (first > second) { std::swap(first, second); std::swap(firstNormal, secondNormal); }
        if (first > nearDistance) { nearDistance = first; nearNormal = firstNormal; }
        if (second < farDistance) { farDistance = second; farNormal = secondNormal; }
        if (nearDistance > farDistance) return false;
    }
    if (farDistance < 0.0f) return false;
    distance = inside ? farDistance : nearDistance;
    normal = inside ? farNormal : nearNormal;
    return distance >= 0.0f;
}

bool IntersectSphere(Vec3 origin, Vec3 direction, Vec3 center, float radius,
                     float& distance, Vec3& normal, bool& inside) {
    const Vec3 offset = origin - center;
    const float a = Vec3::Dot(direction, direction);
    const float b = 2.0f * Vec3::Dot(offset, direction);
    const float c = Vec3::Dot(offset, offset) - radius * radius;
    if (a <= Epsilon) return false;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f) return false;
    inside = c <= 0.0f;
    const float root = std::sqrt(discriminant);
    const float first = (-b - root) / (2.0f * a);
    const float second = (-b + root) / (2.0f * a);
    distance = first >= 0.0f ? first : second;
    if (distance < 0.0f) return false;
    normal = (origin + direction * distance - center).Normalized();
    return true;
}

std::pair<Vec3, Vec3> WorldAabb(const Mat4& world, const SceneQueryBounds& bounds) {
    Vec3 localExtents = bounds.Shape == SceneQueryShape::Sphere
        ? Vec3{std::max(0.0f, bounds.Radius)}
        : Vec3{std::fabs(bounds.Extents.X), std::fabs(bounds.Extents.Y), std::fabs(bounds.Extents.Z)};
    Vec3 minimum{std::numeric_limits<float>::infinity()};
    Vec3 maximum{-std::numeric_limits<float>::infinity()};
    for (int x : {-1, 1}) for (int y : {-1, 1}) for (int z : {-1, 1}) {
        const Vec3 point = world.TransformPoint(bounds.Center + Vec3{
            localExtents.X * static_cast<float>(x), localExtents.Y * static_cast<float>(y),
            localExtents.Z * static_cast<float>(z)});
        minimum.X=std::min(minimum.X,point.X);minimum.Y=std::min(minimum.Y,point.Y);minimum.Z=std::min(minimum.Z,point.Z);
        maximum.X=std::max(maximum.X,point.X);maximum.Y=std::max(maximum.Y,point.Y);maximum.Z=std::max(maximum.Z,point.Z);
    }
    return {minimum, maximum};
}

} // namespace

SceneRay Scene::ScreenPointToRay(Entity cameraEntity, Vec2 screenPoint,
                                 Vec2 presentationSize) const {
    if (!Owns(cameraEntity) || !cameraEntity.HasComponent<Camera>())
        throw std::invalid_argument("Picking camera must belong to the scene and have Camera");
    if (!(presentationSize.X > 0.0f && presentationSize.Y > 0.0f) ||
        !std::isfinite(presentationSize.X) || !std::isfinite(presentationSize.Y))
        throw std::invalid_argument("Presentation size must be finite and positive");
    const auto& camera = cameraEntity.GetComponent<Camera>();
    const float width = camera.Viewport.Width * presentationSize.X;
    const float height = camera.Viewport.Height * presentationSize.Y;
    const float left = camera.Viewport.X * presentationSize.X;
    const float top = presentationSize.Y - (camera.Viewport.Y * presentationSize.Y + height);
    if (width <= 0.0f || height <= 0.0f || screenPoint.X < left || screenPoint.X > left + width ||
        screenPoint.Y < top || screenPoint.Y > top + height)
        throw std::out_of_range("Screen point is outside the camera viewport");
    const float ndcX = ((screenPoint.X - left) / width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((screenPoint.Y - top) / height) * 2.0f;
    const Mat4 world = GetWorldMatrix(cameraEntity);
    const Vec3 position = world.TransformPoint({});
    const Vec3 forward = world.TransformDirection({0,0,-1}).Normalized();
    const Vec3 right = world.TransformDirection({1,0,0}).Normalized();
    const Vec3 up = world.TransformDirection({0,1,0}).Normalized();
    const float aspect = camera.AspectMode == CameraAspectMode::Fixed
        ? std::max(Epsilon, camera.AspectRatio) : width / height;
    if (camera.Projection == CameraProjection::Orthographic) {
        const float halfHeight = std::max(Epsilon, camera.OrthographicSize * 0.5f);
        return {position + right * (ndcX * halfHeight * aspect) + up * (ndcY * halfHeight), forward};
    }
    const float tangent = std::tan(Clamp(camera.VerticalFieldOfView,
        ToRadians(1.0f), ToRadians(179.0f)) * 0.5f);
    return {position, (forward + right * (ndcX * tangent * aspect) + up * (ndcY * tangent)).Normalized()};
}

SceneQueryHit Scene::Pick(Entity camera, Vec2 screenPoint, Vec2 presentationSize,
                          const SceneQueryOptions& options) {
    return Raycast(ScreenPointToRay(camera, screenPoint, presentationSize), options);
}

std::vector<SceneQueryHit> Scene::PickAll(Entity camera, Vec2 screenPoint,
    Vec2 presentationSize, const SceneQueryOptions& options) {
    return RaycastAll(ScreenPointToRay(camera, screenPoint, presentationSize), options);
}

SceneQueryHit Scene::Raycast(const SceneRay& ray, const SceneQueryOptions& options) {
    auto hits = RaycastAll(ray, options);
    return hits.empty() ? SceneQueryHit{} : hits.front();
}

std::vector<SceneQueryHit> Scene::RaycastAll(const SceneRay& ray,
                                             const SceneQueryOptions& options) {
    std::vector<SceneQueryHit> hits;
    if (!IsFinite(ray.Origin) || !IsFinite(ray.Direction) || ray.Direction.LengthSquared() <= Epsilon ||
        !(options.MaxDistance >= 0.0f)) return hits;
    const Vec3 direction = ray.Direction.Normalized();
    auto view = m_registry.view<Transform, SceneQueryBounds>();
    for (const auto handle : view) {
        Entity entity = GetEntity(static_cast<Entity::Id>(handle));
        const auto& bounds = view.get<SceneQueryBounds>(handle);
        if (!Accept(entity, bounds, options)) continue;
        const Mat4 world = GetWorldMatrix(entity); Mat4 inverse;
        if (!world.TryInverse(inverse)) continue;
        const Vec3 localOrigin = inverse.TransformPoint(ray.Origin);
        const Vec3 localDirection = inverse.TransformDirection(direction);
        float parameter = 0.0f; Vec3 localNormal{}; bool inside = false; bool hit = false;
        if (bounds.Shape == SceneQueryShape::Sphere) {
            hit = IntersectSphere(localOrigin, localDirection, bounds.Center,
                std::max(0.0f, bounds.Radius), parameter, localNormal, inside);
        } else {
            const Vec3 extents{std::fabs(bounds.Extents.X), std::fabs(bounds.Extents.Y), std::fabs(bounds.Extents.Z)};
            hit = IntersectBox(localOrigin, localDirection, bounds.Center-extents,
                               bounds.Center+extents, parameter, localNormal, inside);
        }
        if (!hit) continue;
        const Vec3 point = ray.Origin + direction * parameter;
        const float distance = (point - ray.Origin).Length();
        if (distance > options.MaxDistance) continue;
        const Vec3 normal = inverse.Transposed().TransformDirection(localNormal).Normalized();
        hits.push_back({entity, point, normal, distance, inside});
    }
    std::sort(hits.begin(), hits.end(), [](const SceneQueryHit& left, const SceneQueryHit& right) {
        return left.Distance != right.Distance ? left.Distance < right.Distance
                                               : left.Target.GetUUID() < right.Target.GetUUID();
    });
    return hits;
}

std::vector<Entity> Scene::OverlapSphere(Vec3 center, float radius,
                                         const SceneQueryOptions& options) {
    std::vector<Entity> result;
    if (!IsFinite(center) || !std::isfinite(radius) || radius < 0.0f) return result;
    auto view=m_registry.view<Transform,SceneQueryBounds>();
    for(const auto handle:view){Entity entity=GetEntity(static_cast<Entity::Id>(handle));const auto&bounds=view.get<SceneQueryBounds>(handle);if(!Accept(entity,bounds,options))continue;const auto box=WorldAabb(GetWorldMatrix(entity),bounds);const Vec3 closest{Clamp(center.X,box.first.X,box.second.X),Clamp(center.Y,box.first.Y,box.second.Y),Clamp(center.Z,box.first.Z,box.second.Z)};if((closest-center).LengthSquared()<=radius*radius)result.push_back(entity);}
    std::sort(result.begin(),result.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});return result;
}

std::vector<Entity> Scene::OverlapBox(Vec3 center, Vec3 extents,
                                      const SceneQueryOptions& options) {
    std::vector<Entity> result;
    if(!IsFinite(center)||!IsFinite(extents))return result;extents={std::fabs(extents.X),std::fabs(extents.Y),std::fabs(extents.Z)};const Vec3 minimum=center-extents,maximum=center+extents;auto view=m_registry.view<Transform,SceneQueryBounds>();for(const auto handle:view){Entity entity=GetEntity(static_cast<Entity::Id>(handle));const auto&bounds=view.get<SceneQueryBounds>(handle);if(!Accept(entity,bounds,options))continue;const auto box=WorldAabb(GetWorldMatrix(entity),bounds);if(box.first.X<=maximum.X&&box.second.X>=minimum.X&&box.first.Y<=maximum.Y&&box.second.Y>=minimum.Y&&box.first.Z<=maximum.Z&&box.second.Z>=minimum.Z)result.push_back(entity);}std::sort(result.begin(),result.end(),[](Entity a,Entity b){return a.GetUUID()<b.GetUUID();});return result;
}

} // namespace Bazzalt
