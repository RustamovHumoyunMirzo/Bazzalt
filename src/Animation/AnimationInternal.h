#pragma once
#include "Bazzalt/Animation.h"
namespace Bazzalt::Runtime {
// Only for immutable, previously validated rig/clip assets held by an Animator.
std::vector<BoneTransform> SampleAnimationClip(const AnimationClip&,const Skeleton&,float time,bool loop);
}
