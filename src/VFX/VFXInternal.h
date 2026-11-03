#pragma once
#include "Bazzalt/VFX.h"
#include "Bazzalt/Systems/ParticleSystem.h"
#include "Bazzalt/Graphics.h"
namespace Bazzalt::Runtime {
struct VFXProgram {
    float Rate=20,Duration=5,Delay=0,LifeMin=1,LifeMax=2,SpeedMin=1,SpeedMax=2,SizeMin=.1f,SizeMax=.2f,Cone=20,Drag=0,Turbulence=0,Frequency=1,AngularSpeed=0,EndSize=0;
    unsigned Burst=0,Capacity=4096,Columns=1,Rows=1;bool Loop=true;
    Vec3 Box{},Velocity{},Gravity{0,-9.81f,0};float Radius=0;
    Vec4 StartColor{1,1,1,1},EndColor{1,1,1,0};UUID Material{},Texture{};
    std::vector<UUID> Dependencies;
};
struct VFXAccess {
    static void Render(Scene&,const GraphicsEvent&);
    static void Update(Scene&,float delta);
    static bool ValidateSource(const std::string&,std::string& error);
    static const std::shared_ptr<const VFXProgram>& Program(const VFXGraph& graph){return graph.m_program;}
};
}
