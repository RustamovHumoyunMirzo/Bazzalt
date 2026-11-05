#include "Bazzalt/DeveloperConsole.h"
#include "Runtime/NativeScriptRuntime.h"
#include "Runtime/LuaRuntime.h"
#include "Runtime/Engine.h"
#include "Bazzalt/Script.h"
#include <cassert>
#include <fstream>
#include <iterator>
using namespace Bazzalt;
using namespace Bazzalt::Runtime;
int main(int argc,char** argv){
 assert(argc==2);Engine engine;auto entity=engine.GetScene().CreateEntity("Reload");
 auto root=std::filesystem::temp_directory_path()/std::filesystem::u8path("bazzalt-reload-"+UUID::Generate().ToString());std::filesystem::create_directories(root);
 struct Cleanup{std::filesystem::path Path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(Path,ec);}} cleanup{root};
 auto log=root/"log.txt";std::string error;
 ScriptBinding binding{std::filesystem::u8path(argv[1]),entity.GetUUID().ToString(),"LifecycleProbe",{{"LogPath",log.string()}}};
 NativeScriptRuntime old;assert(old.Configure({binding},error));assert(old.Start(error));auto* original=Scripts::Find(entity,"LifecycleProbe");assert(original);
 auto added=engine.GetScene().CreateEntity("Added");auto addedBinding=binding;addedBinding.Entity=added.GetUUID().ToString();
 auto replacement=binding;replacement.Properties["LogPath"]=(root/"wrong.txt").string();NativeScriptRuntime next;assert(next.Configure({replacement,addedBinding},error));assert(next.Start(error,false,true));assert(Scripts::Find(entity,"LifecycleProbe")==original);assert(next.PreserveFrom(old,error));
 old.DiscardForReload();next.ActivateReload();assert(Scripts::Find(entity,"LifecycleProbe")!=original);next.Stop();
 std::ifstream stream(log);std::string contents((std::istreambuf_iterator<char>(stream)),{});stream.close();assert(contents=="create:"+binding.Entity+"\nreload:"+binding.Entity+"\ncreate:"+addedBinding.Entity+"\ndestroy:"+addedBinding.Entity+"\ndestroy:"+binding.Entity+"\n");assert(!std::filesystem::exists(root/"wrong.txt"));
 const auto source=root/"Walk.lua",bytecode=root/"Walk.blua";
 auto write=[&](const char* text){std::ofstream(source)<<text;assert(LuaRuntime::Import(source,bytecode,error));};
 write(R"lua(local T={Speed=1,State={Ticks=0,Large=9223372036854775806},Direction=Bazzalt.Vec3.new(1,2,3)}
 function T:OnCreate()
 Bazzalt.DeveloperConsole.SetEnabled(true)
 local d=Bazzalt.CVarDefinition.new();d.Name="reload.value";d.DefaultValue=3.0;Bazzalt.CVar.Register(d);Bazzalt.CVar.SetValue("reload.value",7.0)
 local c=Bazzalt.ConsoleCommand.new();c.Name="reload.command";Bazzalt.DeveloperConsole.RegisterCommand(c,function() return "old" end)
 self.Speed=7;self.Replay=Bazzalt.PhysicsReplay.ForScene(self.Entity:GetScene());self.Checkpoint=Bazzalt.PhysicsState.new() end
 function T:OnUpdate(dt) self.State.Ticks=self.State.Ticks+1 end
 function T:OnDestroy() error('reload must not destroy old gameplay') end
 return T)lua");
 ScriptBinding luaBinding{bytecode,binding.Entity,"Walk",{{"Speed","1"}}};LuaRuntime lua;assert(lua.Configure({luaBinding},error));assert(lua.Start(error));lua.Update(.1f);
 write(R"lua(local T={Speed=1,Added=12,State={Ticks=0,Large=9223372036854775806},Direction=Bazzalt.Vec3.new(0,0,0)}
 function T:OnCreate() error('reload must not recreate gameplay') end
 function T:OnSaveReloadState() return {Version=2,Speed=self.Speed,State=self.State,Direction=self.Direction} end
 function T:OnReload()
 local d=Bazzalt.CVarDefinition.new();d.Name="reload.value";d.DefaultValue=1.0;Bazzalt.CVar.Register(d);assert(Bazzalt.CVar.GetValue("reload.value")==7.0)
 local c=Bazzalt.ConsoleCommand.new();c.Name="reload.command";Bazzalt.DeveloperConsole.RegisterCommand(c,function() return "new" end)
 assert(self.Replay:GetStatus().Mode==Bazzalt.PhysicsReplayMode.Idle and not self.Checkpoint:IsValid());assert(self.Speed==7 and self.State.Ticks==1 and self.State.Large==9223372036854775806 and self.Added==12 and self.Direction.Y==2);local p=self.Entity:GetWorldTransform();p.Position.X=42;self.Entity:SetWorldTransform(p) end
 function T:OnUpdate(dt) local p=self.Entity:GetWorldTransform();p.Position.X=p.Position.X+2;self.Entity:SetWorldTransform(p) end
 return T)lua");
 LuaRuntime nextLua;assert(nextLua.Configure({luaBinding},error));assert(nextLua.Start(error,true));assert(nextLua.PreserveFrom(lua,error));lua.DiscardForReload();nextLua.ActivateReload();assert(DeveloperConsole::Execute("reload.command").Output=="new");assert(entity.GetWorldTransform().Position.X==42);nextLua.Update(.1f);assert(entity.GetWorldTransform().Position.X==44);
 write("return {Speed='incompatible'}");LuaRuntime failed;assert(failed.Configure({luaBinding},error));assert(failed.Start(error,true));assert(!failed.PreserveFrom(nextLua,error));nextLua.Update(.1f);assert(entity.GetWorldTransform().Position.X==46);failed.DiscardForReload();

 write(R"lua(local T={Speed="changed"}
 function T:OnRestoreReloadState(state) assert(state.Version==2);self.Speed=tostring(state.Speed) end
 function T:OnReload() assert(self.Speed=="7.0" or self.Speed=="7");local p=self.Entity:GetWorldTransform();p.Position.X=50;self.Entity:SetWorldTransform(p) end
 return T)lua");
 LuaRuntime migrated;assert(migrated.Configure({luaBinding},error));assert(migrated.Start(error,true));assert(migrated.PreserveFrom(nextLua,error));nextLua.DiscardForReload();migrated.ActivateReload();assert(entity.GetWorldTransform().Position.X==50);
 migrated.Stop();return 0;
}
