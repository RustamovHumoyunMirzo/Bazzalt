#pragma once
#ifdef __ANDROID__
#include "Bazzalt/Export.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Bazzalt::Runtime::AndroidPlatform {
BAZZALT_API std::filesystem::path ContentDirectory();
BAZZALT_API std::filesystem::path NativeLibrary(const std::string& name);
BAZZALT_API void SetAssetDirectory(const std::filesystem::path& path);
BAZZALT_API std::filesystem::path NativeModule(const std::filesystem::path& path);
BAZZALT_API std::vector<unsigned char> ProtectKey(const std::string& id, const std::vector<unsigned char>& seed);
BAZZALT_API std::optional<std::vector<unsigned char>> ReadSave(const std::filesystem::path& path);
BAZZALT_API void WriteSave(const std::filesystem::path& path, const std::vector<unsigned char>& bytes);
BAZZALT_API bool RemoveSave(const std::filesystem::path& path);
BAZZALT_API std::vector<std::string> SaveFiles(const std::filesystem::path& directory);
BAZZALT_API void ShowIntro(bool enabled, int size, bool allowSkip);
BAZZALT_API bool IntroSkipped();
BAZZALT_API void FinishIntro();
}
#endif
