#pragma once
#include "Bazzalt/SaveGame.h"
namespace Bazzalt::Runtime {
struct SaveAccess {
    BAZZALT_API static void Configure(UUID project,bool editor);
    BAZZALT_API static void Pump();
    BAZZALT_API static void Reset();
    BAZZALT_API static void SetTestDirectory(const std::filesystem::path& path);
};
}
