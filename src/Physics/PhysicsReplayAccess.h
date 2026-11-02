#pragma once
#include "Bazzalt/PhysicsReplay.h"
namespace Bazzalt::Runtime {
struct PhysicsReplayAccess {
    static PhysicsState Make(std::shared_ptr<const void> data,std::uint64_t tick,std::uint64_t bytes,std::uint64_t hash){PhysicsState result;result.m_data=std::move(data);result.m_tick=tick;result.m_bytes=bytes;result.m_hash=hash;return result;}
    static const std::shared_ptr<const void>& Data(const PhysicsState& state){return state.m_data;}
};
struct PhysicsReplayModuleApi {
    PhysicsDeterminismSettings (*GetDeterminism)(Scene&);
    void (*SetDeterminism)(Scene&,const PhysicsDeterminismSettings&);
    bool (*CrossPlatform)();
    PhysicsState (*Capture)(Scene&);
    void (*Restore)(Scene&,const PhysicsState&);
    void (*Control)(Scene&,int,const PhysicsReplaySettings&,std::uint64_t,bool);
    PhysicsReplayStatus (*Status)(Scene&);
    PhysicsState (*Frame)(Scene&,std::uint64_t);
};
}
