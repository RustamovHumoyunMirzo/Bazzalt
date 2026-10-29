#include "Bazzalt/Scene.h"
#include "Physics/PhysicsModuleApi.h"
#include <chrono>
#include <cstdio>
using namespace Bazzalt;
template<class F> double Measure(F function){auto start=std::chrono::steady_clock::now();function();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
int main(){
    float checksum=0;auto q=Quaternion::FromEuler({.2f,.4f,.1f});
    auto math=Measure([&]{for(int i=0;i<400000;++i){auto m=Mat4::Transform({float(i%100),2,3},q,{1,2,3});checksum+=m(0,0)+m(0,3);}});
    Scene scene;Entity parent=scene.GetRootEntity();for(int i=0;i<100;++i){auto e=scene.CreateEntity();e.SetParent(parent,false);e.GetComponent<Transform>().Position={.01f,.02f,.03f};parent=e;}
    auto hierarchy=Measure([&]{for(int i=0;i<4000;++i)checksum+=parent.GetWorldMatrix()(0,3);});
    Scene world;for(int i=0;i<200;++i){auto e=world.CreateEntity();e.GetComponent<Transform>().Position={float(i%20)*3,0,float(i/20)*3};e.AddComponent<ShapeCollider>();}
    world.GetPhysics().Synchronize();auto sync=Measure([&]{for(int i=0;i<100;++i)world.GetPhysics().Synchronize();});
    std::printf("transform_400k_ms=%.3f hierarchy_400k_ms=%.3f physics_200x100_ms=%.3f checksum=%.0f\n",math,hierarchy,sync,checksum);
}
