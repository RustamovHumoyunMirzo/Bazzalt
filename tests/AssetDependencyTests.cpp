#include "Runtime/AssetDatabase.h"
#include "Runtime/Engine.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/ScriptComponent.h"
#include <algorithm>
#include <cassert>
#include <fstream>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
namespace {
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;assert(out.good());}
void Meta(const std::filesystem::path& path,UUID id,const std::string& importer="Bazzalt.Raw"){
 auto meta=path;meta+=".meta";Write(meta,"FormatVersion: 1\nUUID: \""+id.ToString()+"\"\nImporter: \""+importer+"\"\nImporterVersion: 0\nSourceHash: \"\"\nCachePath: \"\"\nSettings: {}\n");
}
class DeclaredImporter:public AssetImporter {
public:
 explicit DeclaredImporter(int* calls=nullptr):Calls(calls){}
 int* Calls;
 std::string GetName()const override{return "Test.Dependencies";}
 std::uint32_t GetVersion()const override{return 1;}
 bool Supports(const std::filesystem::path& path)const override{return path.extension()==".deps";}
 bool Import(const AssetImportContext& ctx,std::string&)override{std::filesystem::create_directories(ctx.OutputPath.parent_path());std::filesystem::copy_file(ctx.SourcePath,ctx.OutputPath,std::filesystem::copy_options::overwrite_existing);return true;}
 bool CollectDependencies(const AssetInfo& asset,AssetDependencyReport& report)const override{
  if(Calls)++*Calls;report.State=AssetDependencyScanState::Complete;std::ifstream in(asset.SourcePath);std::string value;
  while(std::getline(in,value)){UUID id;if(UUID::TryParse(value,id))report.References.push_back({asset.Id,id,{},"Declared"});else report.References.push_back({asset.Id,{},std::filesystem::u8path(value),"File"});}return true;
 }
};
}
int main(){
 const auto root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"bazzalt-dependencies-\u8d44\u6e90")/UUID::Generate().ToString();
 const auto assets=root/"Assets";std::filesystem::create_directories(assets);
 const auto a=UUID::Generate(),b=UUID::Generate(),c=UUID::Generate(),missing=UUID::Generate(),scene=UUID::Generate();
 Write(assets/"a.matinst","{\"shader\":\""+b.ToString()+"\",\"properties\":{\"albedo\":\""+c.ToString()+"\"}}");Meta(assets/"a.matinst",a,"Bazzalt.FilamentMaterial");
 Write(assets/"b.matinst","{\"shader\":\""+a.ToString()+"\",\"properties\":{\"missing\":\""+missing.ToString()+"\"}}");Meta(assets/"b.matinst",b,"Bazzalt.FilamentMaterial");
 Write(assets/"texture.bin","pixels");Meta(assets/"texture.bin",c);
 Write(assets/"scene.bscene","Environment:\n  Source: \""+missing.ToString()+"\"\nEntities:\n  - UUID: \""+c.ToString()+"\"\n    Parent: \""+c.ToString()+"\"\n    Components:\n      - Type: Bazzalt.Mesh\n        Properties:\n          MaterialAsset: \""+a.ToString()+"\"\n          Materials: \""+a.ToString()+","+b.ToString()+"\"\n      - Type: Bazzalt.CustomEntityLink\n        Properties:\n          MeshAsset: \""+c.ToString()+"\"\n");Meta(assets/"scene.bscene",scene);
 AssetDatabase db;assert(db.Open(root,"Assets"));
 assert(db.GetDependencyReport(a).State==AssetDependencyScanState::Complete);
 assert(db.GetDependencyReport(scene).State==AssetDependencyScanState::Partial);
 auto all=db.GetDependencies(a,true);assert(all.size()==3);assert(std::find(all.begin(),all.end(),a)==all.end());
 assert(db.GetDependencies(a).size()==2);assert(db.GetReferencers(c,true).size()==3);
 assert(db.GetDependencyReport(missing).State==AssetDependencyScanState::Unknown);
 auto refs=db.GetDependencyReport(b).References;assert(std::any_of(refs.begin(),refs.end(),[](auto& ref){return ref.State==AssetReferenceState::Missing;}));
 auto sceneRefs=db.GetDependencyReport(scene).References;assert(std::none_of(sceneRefs.begin(),sceneRefs.end(),[&](auto& ref){return ref.Target==c;}));
 std::filesystem::rename(assets/"texture.bin",assets/"renamed.bin");std::filesystem::rename(assets/"texture.bin.meta",assets/"renamed.bin.meta");assert(db.RefreshSource(assets/"renamed.bin"));assert(!db.Find(assets/"texture.bin"));
 refs=db.GetDependencyReport(a).References;assert(std::any_of(refs.begin(),refs.end(),[&](auto& ref){return ref.Target==c&&ref.TargetPath.filename()=="renamed.bin"&&ref.State==AssetReferenceState::Found;}));
 std::filesystem::remove(assets/"renamed.bin");assert(db.RefreshSource(assets/"renamed.bin"));assert(!db.Find(c));
 refs=db.GetDependencyReport(a).References;assert(std::any_of(refs.begin(),refs.end(),[&](auto& ref){return ref.Target==c&&ref.State==AssetReferenceState::Missing;}));assert(db.GetReferencers(c,true).size()==3);
 Write(assets/"renamed.bin","restored");assert(db.RefreshSource(assets/"renamed.bin"));assert(db.Find(c));
 int scans=0;db.RegisterImporter(std::make_unique<DeclaredImporter>(&scans));const auto declared=UUID::Generate();Write(assets/"custom.deps",a.ToString()+"\nfuture.bin\n");Meta(assets/"custom.deps",declared,"Test.Dependencies");assert(db.RefreshSource(assets/"custom.deps"));
 assert(db.RefreshSource(assets/"custom.deps"));assert(scans==1);
 assert(db.GetDependencyReport(declared).References.size()==2);Write(assets/"future.bin","hello");assert(db.RefreshSource(assets/"future.bin"));const auto future=db.Find(assets/"future.bin")->Id;assert(db.GetReferencers(future)==std::vector<UUID>{declared});
 Write(assets/"a.matinst","{}");assert(db.RefreshSource(assets/"a.matinst"));assert(db.GetDependencies(a).empty());assert(db.GetReferencers(c).empty());
 Write(assets/"a.matinst","shader: [");assert(db.RefreshSource(assets/"a.matinst"));assert(db.GetDependencyReport(a).State==AssetDependencyScanState::Failed);
 Write(assets/"a.matinst","shader: \""+b.ToString()+"\"\n");assert(db.RefreshSource(assets/"a.matinst"));
 Write(assets/"unsafe.deps","../escape.bin\n");assert(db.RefreshSource(assets/"unsafe.deps"));assert(db.GetDependencyReport(db.Find(assets/"unsafe.deps")->Id).State==AssetDependencyScanState::Failed);
 std::filesystem::remove(assets/"unsafe.deps");assert(db.Refresh());
 Write(assets/"model.gltf",R"({"asset":{"version":"2.0"},"buffers":[{"uri":"future.bin","byteLength":5}],"scenes":[{}],"scene":0})");assert(db.RefreshSource(assets/"model.gltf"));const auto model=db.Find(assets/"model.gltf")->Id;
 assert(db.GetDependencies(model)==std::vector<UUID>{future});assert(db.GetDependencyReport(model).State==AssetDependencyScanState::Complete);
 const auto preset=UUID{0xba22a17000000002ULL,1};Write(assets/"preset.matinst","shader: \""+preset.ToString()+"\"\n");assert(db.RefreshSource(assets/"preset.matinst"));assert(db.GetDependencyReport(db.Find(assets/"preset.matinst")->Id).References[0].State==AssetReferenceState::Builtin);
 const auto cooked=root/"Cooked";assert(db.ExportCooked(cooked));AssetDatabase shipped;assert(shipped.Open(cooked,"Assets"));assert(shipped.GetDependencies(a,true)==db.GetDependencies(a,true));assert(shipped.GetReferencers(b,true)==db.GetReferencers(b,true));
 Write(cooked/".bazzalt/AssetDependencies.yaml","FormatVersion: 99\nAssets: []\n");assert(!shipped.Open(cooked,"Assets"));
 // Drop the test-only importer asset before opening with the production importer set.
 std::filesystem::remove(assets/"custom.deps");std::filesystem::remove(assets/"custom.deps.meta");
 Write(root/"Game.bproject","FormatVersion: 1\nProjectUUID: \""+UUID::Generate().ToString()+"\"\nName: Dependencies\nAssetDirectory: Assets\nStartupScene: \"\"\n");
 {
  Engine engine;assert(engine.LoadProject(root/"Game.bproject",false));assert(!AssetManager::GetDependencies(a,true).empty());assert(AssetManager::GetDependencyReport(a).Asset==a);
  std::filesystem::remove(assets/"scene.bscene");std::filesystem::remove(assets/"scene.bscene.meta");
  Write(assets/"Script.lua","return {}\n");assert(engine.RefreshAssets());const auto lua=AssetManager::GetAsset(assets/"Script.lua")->Id;
  auto entity=engine.GetScene().CreateEntity("Scripted");ScriptAttachment attachment;attachment.Source="Assets/Script.lua";attachment.TypeName="TestScript";attachment.Lua=true;entity.AddComponent<ScriptComponents>().Values.push_back(attachment);
  assert(engine.SaveScene(assets/"Main.bscene"));assert(engine.RefreshAssets());const auto main=AssetManager::GetAsset(assets/"Main.bscene")->Id;
  Write(root/"Game.bproject","FormatVersion: 1\nProjectUUID: \""+engine.GetProject().ProjectUUID.ToString()+"\"\nName: Dependencies\nAssetDirectory: Assets\nStartupScene: Assets/Main.bscene\n");assert(engine.LoadProject(root/"Game.bproject",false));
  const auto output=root/"GameOutput";assert(engine.ExportCookedProject(output,{}));AssetDatabase finalGraph;assert(finalGraph.Open(output,"Assets"));assert(finalGraph.GetDependencies(main)==std::vector<UUID>{lua});
 }

 std::filesystem::remove_all(root);
}

