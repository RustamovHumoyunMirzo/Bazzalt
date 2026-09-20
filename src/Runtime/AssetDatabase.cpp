#include "Runtime/AssetDatabase.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <functional>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

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

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool CopyAsset(const AssetImportContext& context, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(context.OutputPath.parent_path(), code);
    if (code) { error = code.message(); return false; }
    std::filesystem::copy_file(context.SourcePath, context.OutputPath,
        std::filesystem::copy_options::overwrite_existing, code);
    if (code) { error = code.message(); return false; }
    return true;
}

bool CollectGltfUris(const std::filesystem::path& source,
                     std::vector<std::filesystem::path>& uris, std::string& error) {
    if (LowerExtension(source) != ".gltf") return true;
    std::ifstream stream(source, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (!stream && json.empty()) { error = "Could not read glTF JSON"; return false; }
    try {
        ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(json.data(), json.size()));
        std::function<bool(ryml::ConstNodeRef)> visit = [&](ryml::ConstNodeRef node) {
            for (const ryml::ConstNodeRef child : node.children()) {
                if (child.has_key() && Key(child) == "uri" && child.has_val()) {
                    const std::string uri = Text(child);
                    if (uri.rfind("data:", 0) == 0) continue;
                    const std::filesystem::path relative(uri);
                    if (relative.is_absolute() || relative.empty()) {
                        error = "glTF contains a non-local resource URI: " + uri; return false;
                    }
                    const auto normalized = relative.lexically_normal();
                    if (*normalized.begin() == "..") {
                        error = "glTF resource escapes its asset directory: " + uri; return false;
                    }
                    uris.push_back(normalized);
                }
                if (!visit(child)) return false;
            }
            return true;
        };
        return visit(tree.rootref());
    } catch (const std::exception& exception) {
        error = std::string("Invalid glTF JSON: ") + exception.what(); return false;
    }
}

bool RunTool(const std::filesystem::path& executable,
             const std::vector<std::filesystem::path>& arguments, std::string& error) {
    if (!std::filesystem::exists(executable)) {
        error = "Filament tool was not found: " + executable.string(); return false;
    }
#ifdef _WIN32
    auto quote = [](const std::wstring& value) {
        std::wstring result = L"\"";
        std::size_t slashes = 0;
        for (const wchar_t character : value) {
            if (character == L'\\') { ++slashes; continue; }
            if (character == L'\"') result.append(slashes * 2 + 1, L'\\');
            else result.append(slashes, L'\\');
            slashes = 0; result.push_back(character);
        }
        result.append(slashes * 2, L'\\'); result.push_back(L'\"'); return result;
    };
    std::wstring command = quote(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote(argument.wstring());
    std::vector<wchar_t> writable(command.begin(), command.end()); writable.push_back(L'\0');
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        error = "Could not start " + executable.string(); return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    if (exitCode != 0) { error = executable.filename().string() + " failed with exit code " +
        std::to_string(exitCode); return false; }
    return true;
#else
    std::vector<std::string> storage{executable.string()};
    for (const auto& argument : arguments) storage.push_back(argument.string());
    std::vector<char*> argv; for (auto& value : storage) argv.push_back(value.data());
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child == 0) { execv(argv[0], argv.data()); _exit(127); }
    if (child < 0) { error = "Could not start " + executable.string(); return false; }
    int status = 0; if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = executable.filename().string() + " failed"; return false;
    }
    return true;
#endif
}

class GltfImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.glTF"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path& source) const override {
        const auto extension = LowerExtension(source); return extension == ".gltf" || extension == ".glb";
    }
    std::string ComputeSourceHash(const std::filesystem::path& source) const override {
        std::vector<std::filesystem::path> uris; std::string error;
        if (!CollectGltfUris(source, uris, error)) return {};
        std::uint64_t hash = 14695981039346656037ULL;
        const auto append = [&hash](std::string_view value) {
            for (const unsigned char byte : value) { hash ^= byte; hash *= 1099511628211ULL; }
        };
        append(HashFile(source));
        for (const auto& uri : uris) { append(uri.generic_string()); append(HashFile(source.parent_path() / uri)); }
        std::ostringstream text; text << std::hex << std::setfill('0') << std::setw(16) << hash;
        return text.str();
    }
    bool Import(const AssetImportContext& context, std::string& error) override {
        if (!CopyAsset(context, error)) return false;
        std::vector<std::filesystem::path> uris;
        if (!CollectGltfUris(context.SourcePath, uris, error)) return false;
        for (const auto& uri : uris) {
            const auto source = context.SourcePath.parent_path() / uri;
            if (!std::filesystem::is_regular_file(source)) {
                error = "Missing glTF resource: " + source.string(); return false;
            }
            std::error_code code;
            const auto destination = context.OutputPath.parent_path() / uri;
            std::filesystem::create_directories(destination.parent_path(), code);
            if (!code) std::filesystem::copy_file(source, destination,
                std::filesystem::copy_options::overwrite_existing, code);
            if (code) { error = "Could not cache glTF resource " + uri.string() + ": " + code.message(); return false; }
        }
        return true;
    }
};

class TextureImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.Texture"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path& source) const override {
        const auto extension = LowerExtension(source);
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
    }
    bool Import(const AssetImportContext& context, std::string& error) override {
        return CopyAsset(context, error);
    }
};

class MaterialImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.FilamentMaterial"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path& source) const override {
        const auto extension = LowerExtension(source); return extension == ".mat" || extension == ".filamat";
    }
    std::string GetCacheExtension(const std::filesystem::path&) const override { return ".filamat"; }
    bool Import(const AssetImportContext& context, std::string& error) override {
        std::error_code code; std::filesystem::create_directories(context.OutputPath.parent_path(), code);
        if (code) { error = code.message(); return false; }
        if (LowerExtension(context.SourcePath) == ".filamat") return CopyAsset(context, error);
        return RunTool(BAZZALT_MATC_EXECUTABLE,
            {"--platform", "desktop", "--api", "all", "--output", context.OutputPath, context.SourcePath}, error);
    }
};

class FilameshImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.Filamesh"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path& source) const override {
        const auto extension = LowerExtension(source);
        return extension == ".filamesh" || extension == ".obj" || extension == ".fbx";
    }
    std::string GetCacheExtension(const std::filesystem::path&) const override { return ".filamesh"; }
    bool Import(const AssetImportContext& context, std::string& error) override {
        std::error_code code; std::filesystem::create_directories(context.OutputPath.parent_path(), code);
        if (code) { error = code.message(); return false; }
        if (LowerExtension(context.SourcePath) == ".filamesh") return CopyAsset(context, error);
        return RunTool(BAZZALT_FILAMESH_EXECUTABLE,
            {"--interleaved", "--compress", context.SourcePath, context.OutputPath}, error);
    }
};

class RawImporter final : public AssetImporter {
public:
    std::string GetName() const override { return "Bazzalt.Raw"; }
    std::uint32_t GetVersion() const override { return 1; }
    bool Supports(const std::filesystem::path&) const override { return true; }
    bool Import(const AssetImportContext& context, std::string& error) override {
        return CopyAsset(context, error);
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

std::string AssetImporter::ComputeSourceHash(const std::filesystem::path& source) const {
    return HashFile(source);
}

AssetDatabase::AssetDatabase() {
    RegisterImporter(std::make_unique<RawImporter>());
    RegisterImporter(std::make_unique<GltfImporter>());
    RegisterImporter(std::make_unique<TextureImporter>());
    RegisterImporter(std::make_unique<MaterialImporter>());
    RegisterImporter(std::make_unique<FilameshImporter>());
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
    const std::string sourceHash = importer->ComputeSourceHash(normalized);
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
        record.Id.ToString() / (sourceHash + importer.GetCacheExtension(record.SourcePath));
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
    // Raw metadata predating a specialized importer must upgrade automatically.
    if (!requested.empty() && requested != "Bazzalt.Raw")
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
