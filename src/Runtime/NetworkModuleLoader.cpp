#include "Network/NetworkModuleApi.h"
#include <filesystem>
#include <stdexcept>
#include "Runtime/ModuleLoader.h"
namespace Bazzalt::Runtime {
namespace {
const NetworkModuleApi* Load(){
    const auto* api = LoadSiblingModuleApi<NetworkModuleApi>("bazzalt_network",
        "BazzaltGetNetworkModuleApi", NetworkModuleVersion);
    if(!api||api->Version!=NetworkModuleVersion||api->Size!=sizeof(NetworkModuleApi)||!api->Create||!api->Destroy||!api->Update||!api->State||!api->Send||!api->Poll||!api->Token||!api->Address||!api->Error||!api->Peers||!api->Statistics||!api->Connect||!api->Disconnect)throw std::runtime_error("Incompatible networking module");
    return api;
}
}
const NetworkModuleApi& NetworkModule(){static const auto* api=Load();return *api;}
}
