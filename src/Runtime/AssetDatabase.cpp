#include "Runtime/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <ryml.hpp>

namespace Bazzalt::Runtime {
namespace {

std::string Text(ryml::ConstNodeRef node) {
    const auto value = node.val();
    return std::string(value.str, value.len);
}

std::string Key(ryml::ConstNodeRef node) {
    const auto value = node.key();
    return std::string(value.str, value.len);
}

std::string QuoteYaml(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output << static_cast<char>(character); break;
        }
    }
    output << '"';
    return output.str();
}

std::string HashFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::uint64_t hash = 14695981039346656037ULL;
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        for (std::streamsize index = 0; index < stream.gcount(); ++index) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            hash *= 1099511628211ULL;
        }
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << hash;
    return text.str();
}

class RawImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.Raw"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path&) const override { return true; }
    bool Import(const AssetImportContext& context, std::string& error) override {
        std::error_code code;
        std::filesystem::create_directories(context.OutputPath.parent_path(), code);
        if (code) { error = code.message(); return false; }
        std::filesystem::copy_file(context.SourcePath, context.OutputPath,
                                   std::filesystem::copy_options::overwrite_existing, code);
        if (code) { error = code.message(); return false; }
        return true;
    }
};

bool ReadUnsigned(ryml::ConstNodeRef node, std::uint32_t& value) {
    const std::string text = Text(node);
    try {
        const unsigned long parsed = std::stoul(text);
        value = static_cast<std::uint32_t>(parsed);
        return parsed <= std::numeric_limits<std::uint32_t>::max();
    } catch (...) { return false; }
}

} // namespace

AssetDatabase::AssetDatabase() {
    RegisterImporter(std::make_unique<RawImporter>());
}

void AssetDatabase::RegisterImporter(std::unique_ptr<AssetImporter> importer) {
    if (!importer) return;
    for (auto& existing : m_importers) {
        if (existing->GetName() == importer->GetName()) { existing = std::move(importer); return; }
    }
    // Raw is the fallback and must remain last.
    m_importers.insert(m_importers.end() - (m_importers.empty() ? 0 : 1), std::move(importer));
}

bool AssetDatabase::Open(std::filesystem::path projectDirectory, std::filesystem::path assetDirectory) {
    m_projectDirectory = std::filesystem::absolute(std::move(projectDirectory)).lexically_normal();
    m_assetDirectory = assetDirectory.is_absolute()
        ? std::move(assetDirectory) : m_projectDirectory / assetDirectory;
    m_assetDirectory = m_assetDirectory.lexically_normal();
    m_cacheDirectory = m_projectDirectory / ".bazzalt" / "Cache";
    return Refresh();
}

bool AssetDatabase::Refresh() {
    m_lastError.clear();
    m_byId.clear();
    m_byPath.clear();
    std::error_code code;
    std::filesystem::create_directories(m_assetDirectory, code);
    if (code) { m_lastError = "Could not create asset directory: " + code.message(); return false; }
    for (std::filesystem::recursive_directory_iterator iterator(m_assetDirectory, code), end;
         iterator != end && !code; iterator.increment(code)) {
        if (!iterator->is_regular_file()) continue;
        if (iterator->path().extension() == ".meta") continue;
        if (!RegisterSource(iterator->path())) return false;
    }
    if (code) { m_lastError = "Could not scan assets: " + code.message(); return false; }
    return true;
}

bool AssetDatabase::RegisterSource(const std::filesystem::path& source) {
    const std::filesystem::path normalized = NormalizeSource(source);
    const std::filesystem::path metaPath = std::filesystem::path(normalized.string() + ".meta");
    Metadata metadata;
    const bool hadMeta = std::filesystem::exists(metaPath);
    bool metadataDirty = !hadMeta;
    if (hadMeta && !LoadMetadata(metaPath, metadata)) return false;
    if (!hadMeta) metadata.Id = UUID::Generate();

    if (const auto duplicate = m_byId.find(metadata.Id);
        duplicate != m_byId.end() && duplicate->second.SourcePath != normalized) {
        metadata.Id = UUID::Generate();
        metadata.SourceHash.clear();
        metadataDirty = true;
    }
    AssetImporter* importer = SelectImporter(normalized, metadata.Importer);
    if (importer == nullptr) {
        m_lastError = "No importer supports asset: " + normalized.string();
        return false;
    }
    const std::string sourceHash = HashFile(normalized);
    if (sourceHash.empty()) { m_lastError = "Could not hash asset: " + normalized.string(); return false; }

    AssetInfo record;
    record.Id = metadata.Id;
    record.SourcePath = normalized;
    record.MetaPath = metaPath;
    record.Importer = importer->GetName();
    record.ImporterVersion = importer->GetVersion();
    record.State = AssetState::NeedsImport;
    if (metadata.SourceHash == sourceHash && metadata.Importer == importer->GetName() &&
        metadata.ImporterVersion == importer->GetVersion() && !metadata.CachePath.empty()) {
        record.CachePath = m_projectDirectory / metadata.CachePath;
        if (std::filesystem::exists(record.CachePath)) record.State = AssetState::Ready;
    }
    if (record.State != AssetState::Ready) {
        if (!ImportAsset(record, metadata, *importer, sourceHash)) return false;
        metadataDirty = true;
    }
    if (metadataDirty && !SaveMetadata(metaPath, metadata)) return false;
    m_byPath.emplace(normalized.generic_string(), record.Id);
    m_byId.emplace(record.Id, std::move(record));
    return true;
}

bool AssetDatabase::LoadMetadata(const std::filesystem::path& path, Metadata& metadata) {
    std::ifstream stream(path, std::ios::binary);
    std::string yaml((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!stream && yaml.empty()) { m_lastError = "Could not read meta file: " + path.string(); return false; }
    try {
        ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
        const ryml::ConstNodeRef root = tree.rootref();
        std::uint32_t version = 0;
        std::string uuid;
        if (!root.has_child("FormatVersion") || !ReadUnsigned(root["FormatVersion"], version) ||
            version == 0 || version > CurrentMetaVersion || !root.has_child("UUID") ||
            !UUID::TryParse(Text(root["UUID"]), metadata.Id) || metadata.Id.IsRoot() ||
            !root.has_child("Importer") || !root.has_child("ImporterVersion")) {
            m_lastError = "Invalid asset metadata: " + path.string(); return false;
        }
        metadata.Importer = Text(root["Importer"]);
        if (!ReadUnsigned(root["ImporterVersion"], metadata.ImporterVersion)) return false;
        if (root.has_child("SourceHash")) metadata.SourceHash = Text(root["SourceHash"]);
        if (root.has_child("CachePath")) metadata.CachePath = Text(root["CachePath"]);
        if (root.has_child("Settings")) {
            for (const ryml::ConstNodeRef child : root["Settings"].children())
                metadata.Settings[Key(child)] = Text(child);
        }
        return true;
    } catch (const std::exception& error) {
        m_lastError = "Could not parse meta YAML " + path.string() + ": " + error.what(); return false;
    }
}

bool AssetDatabase::SaveMetadata(const std::filesystem::path& path, const Metadata& metadata) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { m_lastError = "Could not write meta file: " + path.string(); return false; }
    stream << "FormatVersion: " << CurrentMetaVersion << '\n'
           << "UUID: " << QuoteYaml(metadata.Id.ToString()) << '\n'
           << "Importer: " << QuoteYaml(metadata.Importer) << '\n'
           << "ImporterVersion: " << metadata.ImporterVersion << '\n'
           << "SourceHash: " << QuoteYaml(metadata.SourceHash) << '\n'
           << "CachePath: " << QuoteYaml(metadata.CachePath.generic_string()) << '\n'
           << "Settings:\n";
    for (const auto& [key, value] : metadata.Settings)
        stream << "  " << QuoteYaml(key) << ": " << QuoteYaml(value) << '\n';
    return static_cast<bool>(stream);
}

bool AssetDatabase::ImportAsset(AssetInfo& record, Metadata& metadata, AssetImporter& importer,
                                const std::string& sourceHash) {
    const std::filesystem::path relativeCache = std::filesystem::path(".bazzalt") / "Cache" /
        record.Id.ToString() / (sourceHash + record.SourcePath.extension().string());
    record.CachePath = m_projectDirectory / relativeCache;
    std::string error;
    if (!importer.Import({record.SourcePath, record.CachePath, metadata.Settings}, error)) {
        record.State = AssetState::Failed;
        m_lastError = "Import failed for " + record.SourcePath.string() + ": " + error;
        return false;
    }
    metadata.Importer = importer.GetName();
    metadata.ImporterVersion = importer.GetVersion();
    metadata.SourceHash = sourceHash;
    metadata.CachePath = relativeCache;
    record.Importer = metadata.Importer;
    record.ImporterVersion = metadata.ImporterVersion;
    record.State = AssetState::Ready;
    return true;
}

AssetImporter* AssetDatabase::SelectImporter(const std::filesystem::path& source,
                                             const std::string& requested) const {
    if (!requested.empty())
        for (const auto& importer : m_importers)
            if (importer->GetName() == requested && importer->Supports(source)) return importer.get();
    for (const auto& importer : m_importers) if (importer->Supports(source)) return importer.get();
    return nullptr;
}

std::filesystem::path AssetDatabase::NormalizeSource(const std::filesystem::path& path) const {
    return std::filesystem::absolute(path).lexically_normal();
}

std::optional<AssetInfo> AssetDatabase::Find(UUID id) const {
    const auto found = m_byId.find(id);
    return found == m_byId.end() ? std::nullopt : std::optional<AssetInfo>(found->second);
}

std::optional<AssetInfo> AssetDatabase::Find(const std::filesystem::path& sourcePath) const {
    const auto found = m_byPath.find(NormalizeSource(sourcePath).generic_string());
    return found == m_byPath.end() ? std::nullopt : Find(found->second);
}

} // namespace Bazzalt::Runtime
