#pragma once
#include "Bazzalt/Asset.h"
#include <vector>
namespace Bazzalt {
// Found is about identity resolution, not successful import or GPU residency.
enum class AssetReferenceState : std::uint8_t { Unknown, Missing, Found, Builtin };
enum class AssetDependencyScanState : std::uint8_t { Unknown, Complete, Partial, Failed };
struct AssetDependency {
    UUID Source{}, Target{};
    std::filesystem::path TargetPath;
    std::string Field;
    AssetReferenceState State=AssetReferenceState::Unknown;
};
struct AssetDependencyReport {
    UUID Asset{};
    AssetDependencyScanState State=AssetDependencyScanState::Unknown;
    std::string LastError;
    std::vector<AssetDependency> References;
};
} // namespace Bazzalt
