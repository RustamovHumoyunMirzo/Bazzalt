#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/Entity.h"
#include <vector>
#include <memory>
#include <string>
namespace Bazzalt {
class Scene;
enum class LightBakeStatus { Baking, Complete, Applied, Cancelled, Failed };
struct LightBakeSettings {
    unsigned Samples=256;
    unsigned Bounces=1;
    float MaxDistance=1000;
    float RayBias=.002f;
    Vec3 AmbientRadiance{};
    Vec3 DefaultAlbedo{.5f};
    unsigned MaxTriangles=1000000;
    unsigned LightmapSamples=4;
    unsigned ReflectionSamples=32;
    unsigned MaxBakeTexels=1048576;
    [[nodiscard]] BAZZALT_API bool IsValid() const;
};
/// Copyable asynchronous result. Creation and Apply must run on the scene's owner thread.
class BAZZALT_API LightBakeHandle final {
public:
    LightBakeHandle()=default;
    LightBakeStatus GetStatus() const;
    float GetProgress() const;
    std::string GetError() const;
    void Cancel() const;
    /// Transactional; rejects a different or changed scene. Does not save files.
    bool Apply(Scene& scene) const;
private:
    struct State;
    std::shared_ptr<State> m_state;
    friend class Lighting;
};
class BAZZALT_API Lighting final {
public:
    /// Resource budget, not a shader loop or a silently truncated selection.
    static constexpr unsigned MaxProbes=65536;
    Lighting()=delete;
    /// Snapshots enabled triangle geometry and lights before scheduling detached CPU work.
    static LightBakeHandle BeginBake(Scene& scene,const LightBakeSettings& settings={});
    static void Clear(Scene& scene);
    /// Call after directly editing baked vector payloads. Ordinary component fields are detected automatically.
    static void Invalidate(Scene& scene);
    static std::vector<Entity> CreateProbeGrid(Scene& scene,Vec3 minimum,Vec3 maximum,unsigned x=2,unsigned y=2,unsigned z=2,float radius=10);
    /// Interpolated diffuse irradiance. No influence returns zero; normal must be nonzero.
    static Vec3 Sample(const Scene& scene,Vec3 position,Vec3 normal);
    static Vec3 SampleReflection(const Scene& scene,Vec3 position,Vec3 direction,float roughness=0);
};
}
