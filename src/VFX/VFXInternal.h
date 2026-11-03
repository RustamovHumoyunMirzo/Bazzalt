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
    bool Trails=false,TrailDieWithParticle=false,Collisions=false,KillOnCollision=false,SceneGravity=false,TransferImpulse=false,OutputLit=false,OutputShadows=false,Lights=false,LightShadows=false,MeshOutput=false;
    float TrailLifetime=.5f,TrailWidth=.05f,TrailDistance=.03f,TrailRatio=1,Restitution=.5f,Friction=.2f,CollisionRadius=.05f,GravityScale=1,ParticleMass=.1f,PlaneDistance=0,LightIntensity=200,LightRange=3,LightRatio=1;
    Vec3 PlaneNormal{0,1,0};unsigned TrailSegments=16,CollisionMode=0,CollisionLayers=0xffffffffu,MaxLights=8,MeshPrimitive=0;int MeshNode=-1;UUID Mesh{};
    std::vector<UUID> Dependencies;
};
struct ParticleLight {UUID Owner;unsigned Id;Vec3 Position,Color;float Intensity,Range;bool Shadows;};
struct ParticleGpuUniforms {Vec3 Gravity;float Drag=0,EndSize=0,AngularSpeed=0,SizeScale=1;Vec4 StartColor,EndColor,Tint;unsigned Columns=1,Rows=1;bool Mesh=false;};
struct VFXAccess {
    static void Render(Scene&,const GraphicsEvent&);
    static void Update(Scene&,float delta);
    static void SetGPUAvailable(Scene&,bool);
    static std::vector<ParticleLight> GetLights(Scene&);
    static bool ValidateSource(const std::string&,std::string& error);
    static const std::shared_ptr<const VFXProgram>& Program(const VFXGraph& graph){return graph.m_program;}
};
}
