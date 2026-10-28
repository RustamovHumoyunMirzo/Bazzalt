#pragma once
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include "Bazzalt/Graphics.h"
#include "Runtime/GraphicsAccess.h"
namespace Bazzalt::Runtime {
using JointKey=std::pair<UUID,unsigned>;
struct JointEntry {
    Joint Settings;
    std::string Signature,Configuration;
    JPH::Ref<JPH::TwoBodyConstraint> Native;
    JPH::BodyID A,B;
    bool Broken=false;
};
struct SoftSurface {
    GraphicsMesh Mesh;
    std::vector<GraphicsVertex> Vertices;
    std::vector<std::uint32_t> Indices;
    std::vector<unsigned> Particles,SmoothingGroups;
    UUID Source{};
    unsigned Node=0xffffffffu,Slot=0;
};
struct SoftEntry {
    JPH::BodyID Id;
    std::string Signature,Configuration;
    std::vector<SoftSurface> Surfaces;
    std::vector<std::uint32_t> Indices;
    void DestroyMeshes(){for(auto& s:Surfaces)s.Mesh.Destroy();}
    float InverseMass=1;
    Vec3 Wind{};
    UUID Material{};
    Transform Pose;
    bool CastShadows=true;
};
}
