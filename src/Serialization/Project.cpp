#include "Bazzalt/Project.h"

#include <fstream>
#include <iomanip>

namespace Bazzalt {

bool ProjectSerializer::Save(const ProjectMetadata& project, const std::filesystem::path& path) {
    m_lastError.clear();
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { m_lastError = "Could not open project file for writing: " + path.string(); return false; }
    stream << "BAZZALT_PROJECT " << ProjectMetadata::CurrentFormatVersion << '\n'
           << "UUID " << std::quoted(project.ProjectUUID.ToString()) << '\n'
           << "NAME " << std::quoted(project.Name) << '\n'
           << "ASSET_DIRECTORY " << std::quoted(project.AssetDirectory.generic_string()) << '\n'
           << "STARTUP_SCENE " << std::quoted(project.StartupScene.generic_string()) << '\n'
           << "PROPERTY_COUNT " << project.Properties.size() << '\n';
    for (const auto& [key, value] : project.Properties)
        stream << "PROPERTY " << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    stream << "END_PROJECT\n";
    if (!stream) { m_lastError = "Failed while writing project file: " + path.string(); return false; }
    return true;
}

bool ProjectSerializer::Load(ProjectMetadata& project, const std::filesystem::path& path) {
    m_lastError.clear();
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { m_lastError = "Could not open project file: " + path.string(); return false; }
    std::string token, uuidText, name, assets, startup;
    std::uint32_t version = 0;
    std::size_t propertyCount = 0;
    UUID uuid;
    if (!(stream >> token >> version) || token != "BAZZALT_PROJECT" || version == 0 || version > ProjectMetadata::CurrentFormatVersion ||
        !(stream >> token >> std::quoted(uuidText)) || token != "UUID" || !UUID::TryParse(uuidText, uuid) || uuid.IsRoot() ||
        !(stream >> token >> std::quoted(name)) || token != "NAME" ||
        !(stream >> token >> std::quoted(assets)) || token != "ASSET_DIRECTORY" ||
        !(stream >> token >> std::quoted(startup)) || token != "STARTUP_SCENE" ||
        !(stream >> token >> propertyCount) || token != "PROPERTY_COUNT") {
        m_lastError = "Invalid or unsupported project file"; return false;
    }
    std::map<std::string, std::string> properties;
    for (std::size_t index = 0; index < propertyCount; ++index) {
        std::string key, value;
        if (!(stream >> token >> std::quoted(key) >> std::quoted(value)) || token != "PROPERTY") {
            m_lastError = "Invalid project property"; return false;
        }
        properties[std::move(key)] = std::move(value);
    }
    if (!(stream >> token) || token != "END_PROJECT") { m_lastError = "Missing END_PROJECT"; return false; }
    project.ProjectUUID = uuid;
    project.Name = std::move(name);
    project.AssetDirectory = std::filesystem::path(assets);
    project.StartupScene = std::filesystem::path(startup);
    project.Properties = std::move(properties);
    return true;
}

} // namespace Bazzalt
