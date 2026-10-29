#pragma once
#include "Bazzalt/GlobalEvent.h"
#include <filesystem>
namespace Bazzalt::Runtime {
struct GlobalEventsAccess {
    static bool Read(const std::filesystem::path&,EventDefinition&,std::string& error);
    static void DisconnectOwner(const void* owner)noexcept;
    static void BindThread();
    static const void* SetClosingOwner(const void* owner)noexcept;
};
}
