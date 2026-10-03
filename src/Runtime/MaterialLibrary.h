#pragma once
#include "Bazzalt/Material.h"
namespace Bazzalt::Runtime {
Detail::MaterialServices* GetMaterialServices();
void ResetMaterialLibrary();
void ResetRuntimeMaterials();
}
