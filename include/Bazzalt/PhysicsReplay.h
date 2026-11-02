#pragma once
#include "Bazzalt/Physics.h"
namespace Bazzalt {
namespace Runtime {struct PhysicsReplayAccess;}
enum class PhysicsReplayMode : std::uint8_t { Idle, Recording, Playback };
struct PhysicsDeterminismSettings {
    bool Enabled=false,SingleThreaded=true;
    float FixedDeltaTime=1.f/60.f;
    [[nodiscard]] bool IsValid()const{return std::isfinite(FixedDeltaTime)&&FixedDeltaTime>0&&FixedDeltaTime<=1;}
};
struct PhysicsReplaySettings {
    std::uint32_t MaxFrames=3600;
    std::uint64_t MaxMemoryBytes=64ull*1024*1024;
    [[nodiscard]] bool IsValid()const{return MaxFrames>=2&&MaxFrames<=100000&&MaxMemoryBytes>=1024&&MaxMemoryBytes<=512ull*1024*1024;}
};
// Immutable, trusted solver checkpoint. Never accepts serialized native bytes.
class PhysicsState final {
public:
    [[nodiscard]] bool IsValid()const{return bool(m_data);}
    [[nodiscard]] std::uint64_t GetTick()const{return m_tick;}
    [[nodiscard]] std::uint64_t GetByteSize()const{return m_bytes;}
    [[nodiscard]] std::string GetHash()const{static constexpr char digits[]="0123456789abcdef";std::string text(16,'0');auto hash=m_hash;for(int i=15;i>=0;--i){text[std::size_t(i)]=digits[hash&15];hash>>=4;}return text;}
private:
    friend struct Runtime::PhysicsReplayAccess;
    std::shared_ptr<const void> m_data;
    std::uint64_t m_tick=0,m_bytes=0,m_hash=0;
};
struct PhysicsReplayStatus {
    PhysicsReplayMode Mode=PhysicsReplayMode::Idle;
    std::uint64_t Tick=0,FrameCount=0,Cursor=0,MemoryBytes=0;
    bool LimitReached=false,AtEnd=false;
};
// Scene-scoped physics rollback and bounded state playback. Does not rewind scripts.
class BAZZALT_API PhysicsReplay final {
public:
    explicit PhysicsReplay(Scene& scene);
    [[nodiscard]] static PhysicsReplay ForScene(Scene& scene){return PhysicsReplay(scene);}
    [[nodiscard]] PhysicsDeterminismSettings GetDeterminismSettings()const;
    void SetDeterminismSettings(const PhysicsDeterminismSettings& settings);
    [[nodiscard]] bool IsSolverCrossPlatformDeterministic()const;
    [[nodiscard]] PhysicsState CaptureState()const;
    void RestoreState(const PhysicsState& state);
    void BeginRecording(const PhysicsReplaySettings& settings={});
    void EndRecording();
    void BeginPlayback(bool loop=false);
    void StopPlayback();
    void SeekFrame(std::uint64_t index);
    bool AdvancePlayback();
    void Clear();
    [[nodiscard]] PhysicsReplayStatus GetStatus()const;
    [[nodiscard]] PhysicsState GetFrame(std::uint64_t index)const;
private:
    Scene& GetScene()const;
    Scene* m_scene;
    PhysicsScene m_physics;
};
}
