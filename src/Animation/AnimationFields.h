#pragma once
#include "Bazzalt/Components/Animator.h"
#include "Bazzalt/Components/IKConstraint.h"
namespace Bazzalt::Runtime {
template<class T,class F>void VisitAnimator(T& v,F f){f("Model",v.Model);f("Controller",v.Controller);f("DefaultClip",v.DefaultClip);f("Speed",v.Speed);f("PlayOnAwake",v.PlayOnAwake);f("Loop",v.Loop);f("UseUnscaledTime",v.UseUnscaledTime);f("AnimatePhysics",v.AnimatePhysics);f("RootMotion",v.RootMotion);f("RootBone",v.RootBone);}
template<class T,class F>void VisitIKConstraint(T& v,F f){f("AnimatorEntity",v.AnimatorEntity);f("Target",v.Target);f("PoleTarget",v.PoleTarget);f("Solver",v.Solver);f("RootBone",v.RootBone);f("TipBone",v.TipBone);f("Iterations",v.Iterations);f("TargetOffset",v.TargetOffset);f("Pole",v.Pole);f("Weight",v.Weight);f("Tolerance",v.Tolerance);f("GroundRaycast",v.GroundRaycast);f("RayDistance",v.RayDistance);f("GroundOffset",v.GroundOffset);f("LayerMask",v.LayerMask);}
}
