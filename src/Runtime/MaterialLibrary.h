#pragma once
#include "Bazzalt/Material.h"
namespace Bazzalt::Runtime {
struct MaterialShaderPackage { const std::uint8_t* Data=nullptr;std::size_t Size=0; };
MaterialShaderPackage GetBuiltinMaterialPackage(UUID shader);
Detail::MaterialServices* GetMaterialServices();
void ResetMaterialLibrary();
void ResetRuntimeMaterials();
UUID GetMaterialCameraTexture(UUID material,const std::string& parameter);
}
