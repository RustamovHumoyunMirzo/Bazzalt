#pragma once
#include "Bazzalt/Entity.h"
#include "Bazzalt/Components/ParticleEmitter.h"
#include <memory>
#include <string>
#include <vector>
namespace Bazzalt {
namespace Runtime {struct VFXProgram;struct VFXAccess;}
class BAZZALT_API VFXGraph final {
public:
    static VFXGraph Load(UUID asset);
    static VFXGraph FromSource(const std::string& source);
    bool IsValid() const;
    UUID GetAssetUUID() const;
    std::string GetError() const;
    std::vector<UUID> GetDependencies() const;
private:
    UUID m_asset{};std::string m_error;
    std::shared_ptr<const Runtime::VFXProgram> m_program;
    friend struct Runtime::VFXAccess;
};
enum class ParticleEventType { Born, Died, Completed, Collision };
struct ParticleEvent {ParticleEventType Type{};unsigned ParticleId=0;Vec3 Position{};UUID Collider{};Vec3 WorldPoint{},Normal{};};
struct Particle {unsigned Id=0;Vec3 Position{},Velocity{};float Age=0,Lifetime=1,Size=1,Rotation=0;Vec4 Color{1,1,1,1};};
struct ParticleStatistics {unsigned Alive=0,Capacity=0;std::uint64_t Spawned=0,Dropped=0;double Time=0;bool Playing=false,Paused=false;ParticleSimulationBackend Backend=ParticleSimulationBackend::CPU;std::string BackendMessage;unsigned TrailPoints=0,CollisionQueries=0,CollisionBudgetMisses=0,EmittedLights=0;};
// Engine-thread API. Entity must belong to a live scene with ParticleSystem.
class BAZZALT_API VFX final {
public:
    VFX()=delete;
    static void Play(Entity entity);
    static void Restart(Entity entity);
    static void Pause(Entity entity);
    static void Resume(Entity entity);
    static void Stop(Entity entity,bool clear=true);
    static void Clear(Entity entity);
    static void Emit(Entity entity,unsigned count);
    static void SetGraph(Entity entity,VFXGraph graph);
    static void Simulate(Entity entity,float seconds,bool restart=false);
    static ParticleStatistics GetStatistics(Entity entity);
    static std::vector<Particle> GetParticles(Entity entity);
    static std::vector<ParticleEvent> GetEvents(Entity entity);
};
}
