#include <cassert>
#include <filesystem>
#include <fstream>
#include "Bazzalt/Texture.h"
#include "Bazzalt/MaterialBuilder.h"
#include "Bazzalt/Components/Camera.h"
#include "Bazzalt/GUI.h"
#include "Runtime/TextureLibrary.h"
#include "Runtime/Engine.h"
using namespace Bazzalt;
int main(){
    TextureDescriptor d;assert(d.IsValid()&&d.GetMipLevelCount()==1);d.Mipmaps=true;assert(d.GetMipLevelCount()==10);d.Samples=3;assert(!d.IsValid());d.Samples=4;d.Width=64;d.Height=32;d.ColorFormat=TextureColorFormat::RGBA16F;assert(d.IsValid());
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-texture-"+UUID::Generate().ToString())/std::filesystem::u8path("Документы");std::filesystem::create_directories(folder/"Assets");
    struct Cleanup {std::filesystem::path Folder;~Cleanup(){std::error_code error;std::filesystem::remove_all(Folder.parent_path(),error);}} cleanup{folder};
    std::string error;auto source=folder/"Assets"/std::filesystem::u8path("Экран.btexture");assert(Runtime::SaveTextureDescriptor(source,d,error));TextureDescriptor loaded;assert(Runtime::ReadTextureDescriptor(source,loaded,error)&&loaded.Width==64&&loaded.Samples==4&&loaded.Mipmaps);
    auto project=folder/"Texture.bproject";{std::ofstream f(project);f<<"FormatVersion: 1\nProjectUUID: \""<<UUID::Generate().ToString()<<"\"\nName: Textures\nAssetDirectory: Assets\nStartupScene: \"\"\nProperties:\n";}
    Runtime::Engine engine;assert(engine.LoadProject(project,false));auto asset=AssetManager::GetAsset(source);assert(asset&&asset->Importer=="Bazzalt.RenderTexture");auto texture=Texture::Load(asset->Id);assert(texture.IsValid()&&texture.IsRenderTarget()&&!texture.IsRuntime());
    auto material=MaterialBuilder(Shader::Builtin(ShaderPreset::Unlit)).SetTexture("baseColorTexture",texture).Build();assert(material.GetTexture("baseColorTexture")==asset->Id);
    GuiImage image;image.SetTexture(texture);assert(image.Texture==asset->Id&&!image.Camera);
    auto camera=engine.GetScene().CreateEntity("Camera");camera.AddComponent<Camera>().SetRenderTarget(texture);assert(camera.GetComponent<Camera>().GetRenderTarget().GetAssetUUID()==asset->Id);
    auto sceneFile=folder/"Assets"/"Test.bscene";assert(engine.SaveScene(sceneFile));auto scene=engine.LoadSceneAsset(sceneFile);assert(scene&&scene->GetEntity(camera.GetUUID()).GetComponent<Camera>().RenderTarget==asset->Id);
    assert(!texture.Destroy());texture.Resize(128,64);assert(texture.GetDescriptor().Width==128);assert(Runtime::ReadTextureDescriptor(source,loaded,error)&&loaded.Width==64);texture.ResetDescriptor();assert(texture.GetDescriptor().Width==64);
    d.Width=256;assert(Runtime::SaveTextureDescriptor(source,d,error));assert(engine.RefreshTextureAsset(source));assert(Texture::Load(asset->Id).GetDescriptor().Width==256);
    auto transient=Texture::Create(d);assert(transient.IsRuntime());transient.Resize(32,32);assert(transient.GetDescriptor().Width==32);assert(transient.Destroy()&&!transient.IsValid());
    bool rejected=false;try{d.Width=0;auto invalid=Texture::Create(d);(void)invalid;}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    {std::ofstream f(source);f<<"FormatVersion: 1\nWidth: -1\n";}assert(!Runtime::ReadTextureDescriptor(source,loaded,error));
}
