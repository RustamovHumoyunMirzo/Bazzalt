#pragma once
#include "Bazzalt/Animation.h"
#include "Bazzalt/Component.h"
namespace Bazzalt {
// Put constraints on helper entities. Multiple helpers can target the same Animator.
struct IKConstraint : Component {
 UUID AnimatorEntity{},Target{},PoleTarget{};IKSolver Solver=IKSolver::TwoBone;
 unsigned RootBone=0,TipBone=0,Iterations=16;Vec3 TargetOffset{},Pole{0,0,1};float Weight=1,Tolerance=.001f;
 bool GroundRaycast=false;float RayDistance=2,GroundOffset=.05f;std::uint32_t LayerMask=0xffffffffu;
 bool IsValid()const{return unsigned(Solver)<=2&&Iterations>0&&Iterations<=128&&std::isfinite(Weight)&&Weight>=0&&Weight<=1&&std::isfinite(Tolerance)&&Tolerance>0&&std::isfinite(RayDistance)&&RayDistance>0&&RayDistance<=10000&&std::isfinite(GroundOffset)&&std::isfinite(TargetOffset.X)&&std::isfinite(TargetOffset.Y)&&std::isfinite(TargetOffset.Z)&&std::isfinite(Pole.X)&&std::isfinite(Pole.Y)&&std::isfinite(Pole.Z);}
};
}
