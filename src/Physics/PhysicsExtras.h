#pragma once
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include "Bazzalt/Graphics.h"
#include "Runtime/GraphicsAccess.h"
namespace Bazzalt::Runtime {
using JointKey=std::pair<UUID,unsigned>;
struct VehicleDriverInput {
    float Throttle=0,Steering=0,Brake=0,Handbrake=0;
    int Gear=1;
    float Clutch=1;
};
struct VehicleEntry : VehicleDriverInput {
    JPH::Ref<JPH::VehicleConstraint> Native;
    JPH::BodyID Body;
    std::string Signature;
    std::vector<UUID> Wheels;
};
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
    Vec3 ReferenceCenter{};
    Vec3 RestScale{1};
    bool CastShadows=true;
    bool Closed=false;
};
}
