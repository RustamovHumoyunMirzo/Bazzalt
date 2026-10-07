#pragma once
#include <sol/sol.hpp>
#include <map>
#include <iostream>
#include "Bazzalt/Script.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Material.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Components/GaussianBlur.h"
#include "Bazzalt/Components/Vignette.h"
#include "Bazzalt/Components/SceneQueryBounds.h"
#include "Bazzalt/Components/ModelInstance.h"
#include "Bazzalt/Components/ModelNode.h"
#include "Bazzalt/Components/ScriptComponent.h"
namespace Bazzalt::Runtime {
void BindLuaMath(sol::table api);
void BindLuaComponents(sol::table api);
void BindLuaGUI(sol::table api);
void BindLuaServices(sol::table api);
}
