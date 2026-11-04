#pragma once
#include "Bazzalt/Component.h"
#include "Bazzalt/Animation.h"
namespace Bazzalt {
struct Animator : Component {
 UUID Model{},Controller{};std::string DefaultClip;
 float Speed=1;bool PlayOnAwake=true,Loop=true,UseUnscaledTime=false,AnimatePhysics=false;
 RootMotionMode RootMotion=RootMotionMode::Disabled;unsigned RootBone=0;
 bool IsValid()const{return std::isfinite(Speed)&&Speed>=0&&Speed<=100&&unsigned(RootMotion)<=3&&RootBone<4096&&DefaultClip.size()<=256;}
};
}
