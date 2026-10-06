#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bazzalt/Asset.h"
#include "Bazzalt/Serialization.h"

namespace Bazzalt::Runtime {

struct AssetImportContext {
    std::filesystem::path SourcePath;
    std::filesystem::path OutputPath;
    PropertyMap Settings;
};

class AssetImporter {
public:
    virtual ~AssetImporter() = default;
    [[nodiscard]] virtual std::string GetName() const = 0;
    [[nodiscard]] virtual std::uint32_t GetVersion() const = 0;
    [[nodiscard]] virtual bool Supports(const std::filesystem::path& source) const = 0;
    [[nodiscard]] virtual std::string GetCacheExtension(
        const std::filesystem::path& source) const { const auto value=source.extension().u8string();return {reinterpret_cast<const char*>(value.data()),value.size()}; }
    [[nodiscard]] virtual std::string ComputeSourceHash(
        const std::filesystem::path& source) const;
    virtual bool Import(const AssetImportContext& context, std::string& error) = 0;
};

class AssetDatabase final {
public:
    static constexpr std::uint32_t CurrentMetaVersion = 1;

    AssetDatabase();
    void RegisterImporter(std::unique_ptr<AssetImporter> importer);
    bool Open(std::filesystem::path projectDirectory, std::filesystem::path assetDirectory);
    bool Refresh();
    bool RefreshSource(const std::filesystem::path& source);
    bool SetEnvironmentImportSettings(UUID id,const PropertyMap& settings);
    PropertyMap GetImportSettings(UUID id);

    [[nodiscard]] std::optional<AssetInfo> Find(UUID id) const;
    [[nodiscard]] std::optional<AssetInfo> Find(const std::filesystem::path& sourcePath) const;
    [[nodiscard]] const std::string& GetLastError() const { return m_lastError; }

private:
    struct Metadata {
        UUID Id{};
        std::string Importer;
        std::uint32_t ImporterVersion = 0;
        std::string SourceHash;
        std::filesystem::path CachePath;
        PropertyMap Settings;
    };

    bool RegisterSource(const std::filesystem::path& source);
    bool LoadMetadata(const std::filesystem::path& path, Metadata& metadata);
    bool SaveMetadata(const std::filesystem::path& path, const Metadata& metadata);
    bool ImportAsset(AssetInfo& record, Metadata& metadata, AssetImporter& importer,
                     const std::string& sourceHash);
    [[nodiscard]] AssetImporter* SelectImporter(const std::filesystem::path& source,
                                                const std::string& requested = {}) const;
    [[nodiscard]] std::filesystem::path NormalizeSource(const std::filesystem::path& path) const;

    std::filesystem::path m_projectDirectory;
    std::filesystem::path m_assetDirectory;
    std::filesystem::path m_cacheDirectory;
    std::vector<std::unique_ptr<AssetImporter>> m_importers;
    std::unordered_map<UUID, AssetInfo> m_byId;
    std::unordered_map<std::string, UUID> m_byPath;
    std::string m_lastError;
};

} // namespace Bazzalt::Runtime
