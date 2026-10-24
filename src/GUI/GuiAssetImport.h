#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "Bazzalt/UUID.h"
#include "GUI/FontFace.h"
namespace Bazzalt::Runtime {
bool ValidateGuiAssetFile(const std::filesystem::path& path,const std::string& kind,std::string& error);
std::vector<UUID> GetStylesheetReferences(const std::filesystem::path& path);
FontPreviewImage GetFontPreview(UUID asset,std::uint32_t face,const std::string& text,float size,std::uint32_t width);
}
