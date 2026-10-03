#pragma once
#include "Bazzalt/Export.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "Bazzalt/UUID.h"

namespace Bazzalt {

namespace Runtime { class Engine; }

struct ProjectMetadata {
    static constexpr std::uint32_t CurrentFormatVersion = 1;

    UUID ProjectUUID = UUID::Generate();
    std::string Name = "Untitled";
    std::filesystem::path AssetDirectory = "Assets";
    std::filesystem::path StartupScene;
    // Namespaced keys (for example "renderer.pipeline") let future subsystems
    // add metadata without changing the core project format.
    std::map<std::string, std::string> Properties;
};

class BAZZALT_API ProjectSerializer final {
private:
    friend class Runtime::Engine;
    bool Save(const ProjectMetadata& project, const std::filesystem::path& path);
    bool Load(ProjectMetadata& project, const std::filesystem::path& path);
    [[nodiscard]] const std::string& GetLastError() const { return m_lastError; }

private:
    std::string m_lastError;
};

} // namespace Bazzalt
