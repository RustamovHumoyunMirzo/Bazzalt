#pragma once
#include "Bazzalt/Network.h"
namespace Bazzalt::Runtime {
inline constexpr std::uint32_t NetworkModuleVersion=1;
struct NetworkModuleApi {
    std::uint32_t Version,Size;
    void* (*Create)(NetworkRole,const std::string&,const NetworkSettings&,std::uint64_t,const ByteBuffer&);
    void (*Destroy)(void*);
    void (*Update)(void*,double);
    NetworkState (*State)(void*);
    std::string (*Address)(void*);
    std::string (*Error)(void*);
    std::vector<NetworkPeerId> (*Peers)(void*);
    NetworkStatistics (*Statistics)(void*,NetworkPeerId);
    ByteBuffer (*Token)(void*,NetworkPeerId,const std::vector<std::string>&,int);
    bool (*Connect)(void*,NetworkPeerId,const ByteBuffer&);
    bool (*Send)(void*,NetworkPeerId,std::uint16_t,const ByteBuffer&,NetworkChannel);
    std::optional<NetworkEvent> (*Poll)(void*);
    bool (*Disconnect)(void*,NetworkPeerId);
};
using GetNetworkModuleApi=const NetworkModuleApi* (*)(std::uint32_t,std::uint32_t);
const NetworkModuleApi& NetworkModule();
struct NetworkAccess {
    BAZZALT_API static void Configure(UUID project);
    BAZZALT_API static void Pump();
    BAZZALT_API static void Reset();
};
}
