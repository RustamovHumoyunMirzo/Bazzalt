#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include "Bazzalt/Texture.h"
namespace Bazzalt::Runtime {
std::optional<TextureDescriptor> GetTextureDescriptor(UUID id);
bool ReadTextureDescriptor(const std::filesystem::path& path,TextureDescriptor& value,std::string& error);
bool SaveTextureDescriptor(const std::filesystem::path& path,const TextureDescriptor& value,std::string& error);
void ResetTextureLibrary();
void ResetRuntimeTextures();
}
