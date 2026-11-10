#pragma once
#include "Bazzalt/Export.h"
#include <memory>
#include "Bazzalt/Entity.h"
#include "Bazzalt/Components/Transform.h"

namespace Bazzalt {
struct SplinePoint {
    Vec3 Position{}, InTangent{}, OutTangent{};
    Vec3 Up{0,1,0}, Scale{1};
    float Roll=0;
};
struct SplineQuery {
    float Parameter=0, Distance=0, DistanceAlongSpline=0;
    Vec3 Position{}, Tangent{};
};
// Reusable gameplay path. Positions and tangent offsets use the path's space.
class BAZZALT_API Spline {
public:
    Spline();
    explicit Spline(std::vector<SplinePoint> points,bool closed=false);
    const std::vector<SplinePoint>& GetPoints() const { return m_points; }
    std::size_t GetPointCount() const { return m_points.size(); }
    void SetPoints(std::vector<SplinePoint> points);
    void AddPoint(const SplinePoint& point);
    void SetPoint(std::size_t index,const SplinePoint& point);
    void RemovePoint(std::size_t index);
    void Clear();
    void SetClosed(bool closed);
    bool IsClosed() const { return m_closed; }
    Vec3 Evaluate(float parameter) const;
    Vec3 EvaluateTangent(float parameter) const;
    Transform EvaluateTransform(float parameter,const Mat4& world=Mat4{}) const;
    float GetLength() const;
    float GetParameterAtDistance(float distance) const;
    Transform EvaluateDistance(float distance,const Mat4& world=Mat4{}) const;
    SplineQuery FindClosestPoint(Vec3 position,const Mat4& world=Mat4{}) const;
    std::vector<Vec3> Sample(unsigned subdivisions=16) const;
    std::vector<Transform> SampleTransforms(unsigned count,bool evenlySpaced=true,const Mat4& world=Mat4{}) const;
    bool PlaceEntities(const std::vector<Entity>& entities,float spacing=1,float startDistance=0,const Mat4& world=Mat4{}) const;
private:
    static void Validate(const SplinePoint& point);
    unsigned Segment(float& parameter) const;
    void BuildArc() const;
    std::vector<SplinePoint> m_points;
    bool m_closed=false;
    struct ArcCache;
    std::shared_ptr<ArcCache> m_cache;
};
}
