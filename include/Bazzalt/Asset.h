#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Bazzalt/UUID.h"

namespace Bazzalt {

enum class AssetState : std::uint8_t {
    Unknown,
    Ready,
    NeedsImport,
    Missing,
    Failed
};

struct AssetInfo {
    UUID Id{};
    std::filesystem::path SourcePath;
    std::filesystem::path MetaPath;
    std::filesystem::path CachePath;
    std::string Importer;
    std::uint32_t ImporterVersion = 0;
    AssetState State = AssetState::Unknown;
};

} // namespace Bazzalt
