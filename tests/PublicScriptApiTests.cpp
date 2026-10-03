#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/SceneManager.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/Components/Light.h"
#include "Bazzalt/Components/PrimitiveObject.h"
#include "Bazzalt/Time.h"
#include <cassert>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
int main(int argc,char** argv) {
    assert(argc==2);
    const auto module=std::filesystem::u8path(argv[1]);
    {
        Bazzalt::Runtime::Engine engine;
        auto& scene=engine.GetScene();
        assert(Bazzalt::SceneManager::GetActiveScene()==&scene);
        auto parent=scene.CreateEntity("Parent");
        parent.GetComponent<Bazzalt::Transform>().Position.X=10.0f;
        auto cube=scene.CreateEntity("Cube");
        cube.SetParent(parent,false);
        cube.AddComponent<Bazzalt::PrimitiveObject>();
        cube.AddComponent<Bazzalt::Camera>();
        cube.AddComponent<Bazzalt::Light>();
        Bazzalt::Runtime::NativeScriptRuntime scripts;
        Bazzalt::Runtime::ScriptBinding binding;
        binding.Module=module; binding.TypeName="PublicApiProbe"; binding.Entity=cube.GetUUID().ToString();
        binding.Properties["Cube"]=binding.Entity;
        std::string error;
        assert(scripts.Configure({binding},error));
        assert(scripts.Start(error));
        assert(cube.GetComponent<Bazzalt::Transform>().Position.X==3.0f);
        assert(!cube.IsComponentEnabled<Bazzalt::Light>());
        assert(Bazzalt::Time::GetTimeScale()==0.5f);
        scripts.Update(2.0f);
        assert(std::abs(cube.GetWorldTransform().Position.X-15.0f)<0.001f);
        scripts.Stop();
        // Storage and systems still own DLL code; Stop must not unload it yet.
#ifdef _WIN32
        assert(GetModuleHandleW(module.filename().c_str())!=nullptr);
#endif
        scene.DestroyEntity(cube);
    }
#ifdef _WIN32
    assert(GetModuleHandleW(module.filename().c_str())==nullptr);
#endif
}
