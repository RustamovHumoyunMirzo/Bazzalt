#include "Prefab/PrefabAccess.h"
#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Scene.h"
#include "Runtime/Engine.h"
#include "Runtime/ApplicationAccess.h"
#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <ryml.hpp>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
using namespace Bazzalt::Runtime;
namespace Bazzalt {
namespace {
constexpr std::size_t MaxBytes=16u*1024u*1024u,MaxEntities=10000;
constexpr auto LinkType="Bazzalt.PrefabInstance";
std::vector<Engine*> Engines;
std::map<Engine*,std::map<std::string,std::filesystem::path>> ScriptModules;
ComponentSerializationRegistry& Registry(){if(Engines.empty())throw std::logic_error("Prefab operations require a loaded project");return Engines.back()->GetSceneSerializer().GetComponents();}
std::string Quote(const std::string& value){std::string out="\"";for(unsigned char c:value){if(c=='\\'||c=='"'){out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)throw std::invalid_argument("Unsupported control character in prefab text");else out+=char(c);}return out+'"';}
std::string Text(ryml::ConstNodeRef node){auto value=node.val();return {value.str,value.len};}
UUID RootId(const PreparedScene& document){UUID root;for(const auto& entity:document.Entities)if(!entity.Parent){if(root)throw std::runtime_error("A prefab must have exactly one root");root=entity.Id;}if(!root)throw std::runtime_error("Prefab has no root");return root;}
void Validate(const PreparedScene& document){
 if(document.Entities.empty()||document.Entities.size()>MaxEntities)throw std::runtime_error("Prefab must contain 1..10000 entities");RootId(document);std::map<UUID,UUID> parents;std::map<UUID,unsigned> colors;
 for(const auto& entity:document.Entities){if(!entity.Id||!parents.emplace(entity.Id,entity.Parent).second)throw std::runtime_error("Duplicate or invalid prefab entity");std::set<std::string> types;for(const auto& c:entity.Components){if(c.Type==LinkType)throw std::runtime_error("Prefab sources cannot contain nested instance links");if(c.Type.empty()||!c.Version||!types.insert(c.Type).second)throw std::runtime_error("Invalid prefab component");}}
 for(auto [id,parent]:parents)if(parent&&!parents.contains(parent))throw std::runtime_error("Prefab references a missing parent");
 for(auto [start,_]:parents){auto id=start;std::vector<UUID> chain;while(id&&colors[id]==0){colors[id]=1;chain.push_back(id);id=parents.at(id);}if(id&&colors[id]==1)throw std::runtime_error("Prefab hierarchy cycle");for(auto value:chain)colors[value]=2;}
}
std::string Encode(const PreparedScene& document){
 Validate(document);std::ostringstream out;out<<"PrefabVersion: 1\nFormatVersion: 1\nSceneUUID: "<<Quote(document.Id.ToString())<<"\nEntities:\n";
 for(const auto& e:document.Entities){out<<"  - UUID: "<<Quote(e.Id.ToString())<<"\n    Parent: "<<Quote(e.Parent.ToString())<<"\n    Name: "<<Quote(e.Name)<<"\n    Components:"<<(e.Components.empty()?" []\n":"\n");for(const auto& c:e.Components){out<<"      - Type: "<<Quote(c.Type)<<"\n        Version: "<<c.Version<<"\n        Properties:"<<(c.Properties.empty()?" {}\n":"\n");for(const auto& [key,value]:c.Properties)out<<"          "<<Quote(key)<<": "<<Quote(value)<<'\n';}}
 auto text=out.str();if(text.size()>MaxBytes)throw std::length_error("Prefab exceeds 16 MiB");return text;
}
PreparedScene Decode(const std::string& text){if(text.size()>MaxBytes)throw std::length_error("Prefab exceeds 16 MiB");auto tree=ryml::parse_in_arena(ryml::csubstr(text.data(),text.size()));auto root=tree.rootref();if(!root.is_map()||!root.has_child("PrefabVersion")||Text(root["PrefabVersion"])!="1")throw std::runtime_error("Unsupported prefab version");PreparedScene document;std::string error;if(!PrepareSceneData(text,document,error))throw std::runtime_error(error);Validate(document);return document;}
void Remap(SerializedComponent& component,const std::map<UUID,UUID>& map,ComponentSerializationRegistry& registry,bool strict=false){
 for(auto& [key,value]:component.Properties){bool reference=registry.IsEntityReference(component.Type,key);
  if(component.Type=="Bazzalt.ScriptComponents"&&key.ends_with(".Value")){auto type=component.Properties.find(key.substr(0,key.size()-5)+"Type");if(type!=component.Properties.end())reference=type->second=="Entity"||type->second=="Bazzalt::Entity"||type->second=="EntityReference"||type->second=="Bazzalt::EntityReference";}
  if(!reference)continue;UUID id;if(!UUID::TryParse(value,id))throw std::runtime_error("Invalid serialized entity reference: "+component.Type+"."+key);if(!id)continue;auto found=map.find(id);if(found!=map.end())value=found->second.ToString();else if(strict)throw std::runtime_error("Prefab references an entity outside its hierarchy: "+component.Type+"."+key);
 }
}
bool HasNested(Entity root){std::vector<Entity> pending=root.GetChildren();for(std::size_t i=0;i<pending.size();++i){if(pending[i].HasComponent<PrefabLink>())return true;auto children=pending[i].GetChildren();pending.insert(pending.end(),children.begin(),children.end());}return false;}
PreparedScene Capture(Entity root,ComponentSerializationRegistry& registry,std::map<UUID,UUID>& ids,bool strict=false){
 if(!root||!root.GetUUID())throw std::invalid_argument("Select a non-root entity");auto* scene=root.GetScene();std::map<UUID,UUID> reverse;for(auto [local,world]:ids)reverse[world]=local;
 PreparedScene document;document.Id=UUID::Generate();std::vector<Entity> pending{root},ordered;
 for(std::size_t i=0;i<pending.size();++i){if(pending.size()>MaxEntities)throw std::length_error("Prefab hierarchy exceeds 10000 entities");auto entity=pending[i];if(strict&&entity!=root&&entity.HasComponent<PrefabLink>())throw std::runtime_error("Unpack nested prefab instances before creating or applying this prefab");ordered.push_back(entity);if(!reverse.contains(entity.GetUUID())){auto local=UUID::Generate();reverse[entity.GetUUID()]=local;ids[local]=entity.GetUUID();}auto children=scene->GetChildren(entity);pending.insert(pending.end(),children.begin(),children.end());}
 for(auto entity:ordered){PreparedEntity record;record.Id=reverse.at(entity.GetUUID());record.Parent=entity==root?UUID{}:reverse.at(entity.GetParent().GetUUID());record.Name=entity.GetComponent<Name>().Value;
  for(const auto& descriptor:registry.GetDescriptors()){if(descriptor.Type==LinkType)continue;SerializedComponent component{descriptor.Type,descriptor.Version,{}};if(descriptor.Serialize(entity,component.Properties)){Remap(component,reverse,registry,strict);record.Components.push_back(std::move(component));}}
  if(auto* unknown=entity.TryGetComponent<UnresolvedComponents>())for(auto component:unknown->Values)if(!registry.Find(component.Type)){Remap(component,reverse,registry,strict);record.Components.push_back(std::move(component));}
  std::sort(record.Components.begin(),record.Components.end(),[](const auto& a,const auto& b){return a.Type<b.Type;});document.Entities.push_back(std::move(record));
 }
 return document;
}
void Deserialize(Entity entity,const PreparedEntity& record,const std::map<UUID,UUID>& ids,ComponentSerializationRegistry& registry){
 for(auto component:record.Components){Remap(component,ids,registry);if(auto* descriptor=registry.Find(component.Type)){if(!descriptor->Deserialize(entity,component.Properties,component.Version))throw std::runtime_error("Could not deserialize prefab component: "+component.Type);}else{auto* u=entity.TryGetComponent<UnresolvedComponents>();if(!u)u=&entity.AddComponent<UnresolvedComponents>();u->Values.push_back(std::move(component));}}
}
Entity Spawn(const PreparedScene& document,Scene& scene,Entity parent,std::map<UUID,UUID>& ids,ComponentSerializationRegistry& registry){
 Validate(document);if(parent&&parent.GetScene()!=&scene)throw std::invalid_argument("Prefab parent belongs to another scene");std::set<UUID> used;for(const auto& record:document.Entities){auto& id=ids[record.Id];if(!id)id=UUID::Generate();if(scene.GetEntity(id)||!used.insert(id).second)throw std::runtime_error("Prefab instance UUID collision");}
 std::vector<const PreparedEntity*> ordered;std::map<UUID,std::vector<const PreparedEntity*>> children;
 for(const auto& record:document.Entities)if(!record.Parent)ordered.push_back(&record);else children[record.Parent].push_back(&record);
 for(std::size_t i=0;i<ordered.size();++i)for(auto child:children[ordered[i]->Id])ordered.push_back(child);
 std::vector<UUID> created;try{
  for(auto pointer:ordered){const auto& record=*pointer;auto entity=scene.CreateEntity(ids.at(record.Id),record.Name);created.push_back(entity.GetUUID());}
  for(auto pointer:ordered){const auto& record=*pointer;auto entity=scene.GetEntity(ids.at(record.Id));Entity p=record.Parent?scene.GetEntity(ids.at(record.Parent)):parent;if(p&&!scene.SetParent(entity,p,false))throw std::runtime_error("Could not parent prefab entity");}
  for(auto record:ordered)Deserialize(scene.GetEntity(ids.at(record->Id)),*record,ids,registry);
  return scene.GetEntity(ids.at(RootId(document)));
 }catch(...){for(auto id:created)if(auto entity=scene.GetEntity(id))scene.DestroyEntity(entity);throw;}
}
bool Equal(const SerializedComponent& a,const SerializedComponent& b){return a.Type==b.Type&&a.Version==b.Version&&a.Properties==b.Properties;}
using Components=std::map<std::string,SerializedComponent>;
Components Fields(const PreparedEntity& e){Components result;for(auto c:e.Components)result.emplace(c.Type,std::move(c));return result;}
bool Equal(const PreparedEntity& a,const PreparedEntity& b,bool root=false){if(a.Name!=b.Name||(!root&&a.Parent!=b.Parent))return false;auto left=Fields(a),right=Fields(b);if(root){left.erase("Bazzalt.Transform");right.erase("Bazzalt.Transform");}if(left.size()!=right.size())return false;for(auto& [type,value]:left)if(!right.contains(type)||!Equal(value,right.at(type)))return false;return true;}
using Records=std::map<UUID,PreparedEntity>;
Records Index(const PreparedScene& d){Records out;for(auto entity:d.Entities)out.emplace(entity.Id,std::move(entity));return out;}
PreparedScene Merge(const PreparedScene& baseline,const PreparedScene& local,const PreparedScene& source){
 auto base=Index(baseline),ours=Index(local),theirs=Index(source);const auto root=RootId(source);PreparedScene result=source;result.Entities.clear();
 // Merge into a separate map: never erase a map while iterating it.
 Records merged;
 for(auto& [id,record]:ours){auto old=base.find(id),fresh=theirs.find(id);if(old==base.end()){merged[id]=record;continue;}if(fresh==theirs.end()){if(!Equal(record,old->second,id==root))merged[id]=record;continue;}
  auto value=record;if(value.Name==old->second.Name)value.Name=fresh->second.Name;if(value.Parent==old->second.Parent)value.Parent=fresh->second.Parent;
  auto b=Fields(old->second),l=Fields(record),s=Fields(fresh->second);std::set<std::string> types;for(auto* map:{&b,&l,&s})for(auto& [type,_]:*map)types.insert(type);value.Components.clear();
  for(const auto& type:types){auto bi=b.find(type),li=l.find(type),si=s.find(type);const bool had=bi!=b.end(),has=li!=l.end(),now=si!=s.end();if(id==root&&type=="Bazzalt.Transform"){if(has)value.Components.push_back(li->second);continue;}
   if(had==has&&(!had||Equal(bi->second,li->second))){if(now)value.Components.push_back(si->second);continue;}if(!has)continue;if(!had||!now||li->second.Version!=bi->second.Version||si->second.Version!=bi->second.Version){value.Components.push_back(li->second);continue;}
   auto component=li->second;std::set<std::string> keys;for(auto* props:{&bi->second.Properties,&li->second.Properties,&si->second.Properties})for(auto& [key,_]:*props)keys.insert(key);
   for(const auto& key:keys){auto bp=bi->second.Properties.find(key),lp=li->second.Properties.find(key),sp=si->second.Properties.find(key);bool before=bp!=bi->second.Properties.end(),present=lp!=li->second.Properties.end();if(before==present&&(!before||bp->second==lp->second)){if(sp==si->second.Properties.end())component.Properties.erase(key);else component.Properties[key]=sp->second;}}
   value.Components.push_back(std::move(component));
  }merged[id]=std::move(value);
 }
 for(const auto& record:source.Entities)if(!base.contains(record.Id)&&!ours.contains(record.Id)&&(!record.Parent||merged.contains(record.Parent)))merged[record.Id]=record;
 // Preserve an overridden orphan's removed ancestors so hierarchy remains valid.
 bool changed=true;while(changed){changed=false;for(auto& [id,record]:merged)if(record.Parent&&!merged.contains(record.Parent)){auto parent=ours.find(record.Parent);if(parent!=ours.end()){merged[parent->first]=parent->second;changed=true;}else record.Parent=root;}}
 for(auto& [_,record]:merged)result.Entities.push_back(std::move(record));Validate(result);return result;
}
Entity Replace(Entity root,const PreparedScene& replacement,const PreparedScene& baseline,std::map<UUID,UUID> ids,UUID asset,ComponentSerializationRegistry& registry){
 auto* scene=root.GetScene();auto parent=root.GetParent();const auto placement=root.GetComponent<Transform>();auto previousBaseline=root.GetComponent<PrefabLink>().Baseline;auto previousIds=root.GetComponent<PrefabLink>().Entities;auto current=Capture(root,registry,previousIds);
 // Validate deserializers in isolation before changing the live hierarchy.
 {Scene staging;auto stagedIds=std::map<UUID,UUID>{};Spawn(replacement,staging,{},stagedIds,registry);}
 for(const auto& record:replacement.Entities)if(auto found=ids.find(record.Id);found!=ids.end())if(auto existing=scene->GetEntity(found->second);existing&&existing!=root&&!scene->IsAncestor(root,existing)){if(record.Id==RootId(replacement))throw std::runtime_error("Prefab root UUID collides outside its instance");ids[record.Id]=UUID::Generate();}
 scene->DestroyEntity(root);
 try{auto value=Spawn(replacement,*scene,parent,ids,registry);value.AddOrReplaceComponent<Transform>(placement);auto& link=value.AddComponent<PrefabLink>();link.Asset=asset;std::set<UUID> retained;for(const auto& record:replacement.Entities)retained.insert(record.Id);for(const auto& record:baseline.Entities)retained.insert(record.Id);std::erase_if(ids,[&](const auto& pair){return !retained.contains(pair.first);});link.Entities=std::move(ids);link.Baseline=Encode(baseline);return value;}
 catch(...){try{auto restored=Spawn(current,*scene,parent,previousIds,registry);restored.AddOrReplaceComponent<Transform>(placement);auto& link=restored.AddComponent<PrefabLink>();link.Asset=asset;link.Entities=std::move(previousIds);link.Baseline=previousBaseline;}catch(...){throw std::runtime_error("Prefab restore failed after a component deserializer failure");}throw;}
}
}
void PrefabAccess::Bind(Engine* engine){if(engine&&std::find(Engines.begin(),Engines.end(),engine)==Engines.end())Engines.push_back(engine);}
void PrefabAccess::Unbind(Engine* engine){ScriptModules.erase(engine);std::erase(Engines,engine);}
void PrefabAccess::SetScriptModules(const std::map<std::string,std::filesystem::path>& modules){if(Engines.empty())return;ScriptModules[Engines.back()]=modules;}
std::filesystem::path PrefabAccess::ScriptModule(const std::string& type,const std::filesystem::path& fallback){if(fallback.extension()!=".cpp"||Engines.empty())return fallback;auto& modules=ScriptModules[Engines.back()];auto found=modules.find(type);return found==modules.end()?fallback:found->second;}
bool PrefabAccess::Read(const std::filesystem::path& path,PreparedScene& result,std::string& error){try{std::error_code code;auto length=std::filesystem::file_size(path,code);if(code||length>MaxBytes)throw std::runtime_error("Could not read prefab or size exceeds 16 MiB");std::ifstream file(path,std::ios::binary);std::string text(static_cast<std::size_t>(length),'\0');file.read(text.data(),text.size());if(!file||file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Prefab changed while reading");result=Decode(text);return true;}catch(const std::exception& e){error=e.what();return false;}}
bool PrefabAccess::Save(Entity root,const std::filesystem::path& path,SceneSerializer& serializer,std::string& error,bool overwrite){
 std::filesystem::path temporary;try{if(!root)throw std::invalid_argument("Select an entity");if(path.extension()!=".bprefab")throw std::invalid_argument("Prefab assets use .bprefab");if(std::filesystem::exists(path)&&!overwrite)throw std::runtime_error("Prefab already exists; use Apply to update it");std::map<UUID,UUID> ids;if(auto* link=root.TryGetComponent<PrefabLink>())ids=link->Entities;auto document=Capture(root,serializer.GetComponents(),ids,true);if(overwrite){PreparedScene old;if(!Read(path,old,error))return false;document.Id=old.Id;}
 auto text=Encode(document);temporary=path;temporary+=".tmp-"+UUID::Generate().ToString();{std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file.write(text.data(),text.size());file.flush();if(!file)throw std::runtime_error("Could not write prefab asset");}
#if defined(_WIN32)
 if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0)))throw std::runtime_error("Could not commit prefab asset");
#else
 if(!overwrite){std::filesystem::create_hard_link(temporary,path);std::filesystem::remove(temporary);}else std::filesystem::rename(temporary,path);
#endif
 return true;}catch(const std::exception& e){if(!temporary.empty()){std::error_code ignored;std::filesystem::remove(temporary,ignored);}error=e.what();return false;}
}
bool PrefabAccess::Link(Entity root,UUID asset,SceneSerializer& serializer,std::string& error){try{auto info=AssetManager::GetAsset(asset);if(!info)throw std::runtime_error("Missing prefab asset");PreparedScene document;if(!Read(info->CachePath,document,error))return false;std::map<UUID,UUID> ids;if(auto* old=root.TryGetComponent<PrefabLink>())ids=old->Entities;auto captured=Capture(root,serializer.GetComponents(),ids);auto localRecords=captured.Entities;if(localRecords.size()!=document.Entities.size())throw std::runtime_error("Prefab hierarchy changed during authoring");
 // Capture uses stable breadth-first order; the saved document has that same order.
 std::map<UUID,UUID> linked;for(std::size_t i=0;i<localRecords.size();++i)linked[document.Entities[i].Id]=ids.at(localRecords[i].Id);auto& link=root.AddOrReplaceComponent<PrefabLink>();link.Asset=asset;link.Entities=std::move(linked);link.Baseline=Encode(document);return true;}catch(const std::exception& e){error=e.what();return false;}}
namespace {
void CookDocument(PreparedScene& document,const std::map<std::string,UUID>& modules,const std::filesystem::path& project){
 for(auto& entity:document.Entities)for(auto& component:entity.Components)if(component.Type=="Bazzalt.ScriptComponents")for(auto& [key,value]:component.Properties)if(key.ends_with(".Source")){
   auto type=component.Properties.find(key.substr(0,key.size()-6)+"TypeName");if(type==component.Properties.end())throw std::runtime_error("Prefab script is missing its type name");UUID sourceId;auto asset=UUID::TryParse(value,sourceId)?AssetManager::GetAsset(sourceId):AssetManager::GetAsset(std::filesystem::u8path(value).is_absolute()?std::filesystem::u8path(value):project/std::filesystem::u8path(value));if(!asset||asset->State!=AssetState::Ready)throw std::runtime_error("Prefab script asset is missing or failed import");
   if(asset->SourcePath.extension()==".cpp"){auto module=modules.find(type->second);if(module==modules.end())throw std::runtime_error("Prefab script module was not compiled: "+type->second);value=module->second.ToString();}else value=asset->Id.ToString();
 }
}
}
void PrefabAccess::CookAsset(const std::filesystem::path& source,const std::filesystem::path& destination,const std::map<std::string,UUID>& modules,const std::filesystem::path& project){PreparedScene document;std::string error;if(!Read(source,document,error))throw std::runtime_error(error);CookDocument(document,modules,project);auto text=Encode(document);std::ofstream out(destination,std::ios::binary|std::ios::trunc);out.write(text.data(),text.size());out.flush();if(!out)throw std::runtime_error("Could not write cooked prefab");}
void PrefabAccess::CookLinks(Scene& scene,const std::map<std::string,UUID>& modules,const std::filesystem::path& project){for(auto handle:scene.GetRegistry().view<PrefabLink>()){auto& link=scene.GetRegistry().get<PrefabLink>(handle);auto document=Decode(link.Baseline);CookDocument(document,modules,project);link.Baseline=Encode(document);}}
void PrefabAccess::Register(ComponentSerializationRegistry& registry){registry.Register<PrefabLink>(LinkType,1,[](const PrefabLink& v,PropertyMap& p){p["Asset"]=v.Asset.ToString();p["Baseline"]=v.Baseline;for(auto [local,world]:v.Entities)p["Entity."+local.ToString()]=world.ToString();},[](PrefabLink& v,const PropertyMap& p,std::uint32_t version){try{if(version!=1||!UUID::TryParse(p.at("Asset"),v.Asset)||!v.Asset)return false;v.Baseline=p.at("Baseline");auto document=Decode(v.Baseline);std::set<UUID> targets;for(auto& [key,value]:p)if(key.starts_with("Entity.")){UUID local,world;if(!UUID::TryParse(key.substr(7),local)||!local||!UUID::TryParse(value,world)||!world||!targets.insert(world).second||!v.Entities.emplace(local,world).second)return false;}return v.Entities.size()<=MaxEntities*2&&v.Entities.contains(RootId(document));}catch(...){return false;}});}
std::optional<Prefab> Prefab::Load(UUID asset){auto info=AssetManager::GetAsset(asset);if(!info||info->State!=AssetState::Ready||info->SourcePath.extension()!=".bprefab")return {};auto data=std::make_shared<PrefabDefinition>();std::string error;if(!PrefabAccess::Read(info->CachePath,data->Document,error))return {};data->Asset=asset;Prefab value;value.m_data=std::move(data);return value;}
UUID Prefab::GetUUID()const{return m_data?m_data->Asset:UUID{};}
std::string Prefab::GetName()const{if(!m_data)return {};auto id=RootId(m_data->Document);for(const auto& e:m_data->Document.Entities)if(e.Id==id)return e.Name;return {};}
std::size_t Prefab::GetEntityCount()const{return m_data?m_data->Document.Entities.size():0;}
Entity Prefab::Instantiate(Scene& scene,Entity parent)const{if(!m_data)return {};std::map<UUID,UUID> ids;auto value=Spawn(m_data->Document,scene,parent,ids,Registry());try{auto& link=value.AddComponent<PrefabLink>();link.Asset=m_data->Asset;link.Entities=std::move(ids);link.Baseline=Encode(m_data->Document);if(!Engines.empty())Engines.back()->AdoptPrefabScripts(value);return value;}catch(...){scene.DestroyEntity(value);throw;}}
Entity Scene::InstantiatePrefab(UUID asset,Entity parent){auto prefab=Prefab::Load(asset);return prefab?prefab->Instantiate(*this,parent):Entity{};}
Entity Prefab::GetInstanceRoot(Entity entity){while(entity&&entity.GetUUID()){if(entity.HasComponent<PrefabLink>())return entity;entity=entity.GetParent();}return {};}
UUID Prefab::GetSource(Entity entity){auto root=GetInstanceRoot(entity);return root?root.GetComponent<PrefabLink>().Asset:UUID{};}
bool Prefab::HasOverrides(Entity entity){auto root=GetInstanceRoot(entity);if(!root)return false;if(HasNested(root))return true;auto& link=root.GetComponent<PrefabLink>();auto ids=link.Entities;auto current=Capture(root,Registry(),ids);auto baseline=Decode(link.Baseline);auto a=Index(current),b=Index(baseline);if(a.size()!=b.size())return true;auto id=RootId(baseline);for(const auto& [key,value]:a)if(!b.contains(key)||!Equal(value,b.at(key),key==id))return true;return false;}
Entity Prefab::Refresh(Entity entity){if(!Engines.empty()&&(Engines.back()->IsScriptsRunning()||ApplicationAccess::AreRuntimeServicesReady()))throw std::logic_error("Prefab source refresh is an authoring operation and is unavailable during Play");auto root=GetInstanceRoot(entity);if(!root)return {};if(HasNested(root))throw std::runtime_error("Unpack nested prefab instances before updating or reverting this prefab");auto link=root.GetComponent<PrefabLink>();auto source=Load(link.Asset);if(!source)throw std::runtime_error("Prefab source is missing or failed import; unpack to keep local objects");auto ids=link.Entities;auto local=Capture(root,Registry(),ids);auto merged=Merge(Decode(link.Baseline),local,source->m_data->Document);return Replace(root,merged,source->m_data->Document,std::move(ids),link.Asset,Registry());}
Entity Prefab::Revert(Entity entity){if(!Engines.empty()&&(Engines.back()->IsScriptsRunning()||ApplicationAccess::AreRuntimeServicesReady()))throw std::logic_error("Prefab revert is unavailable during Play");auto root=GetInstanceRoot(entity);if(!root)return {};if(HasNested(root))throw std::runtime_error("Unpack nested prefab instances before updating or reverting this prefab");auto link=root.GetComponent<PrefabLink>();auto source=Load(link.Asset);if(!source)throw std::runtime_error("Prefab source is missing or failed import");return Replace(root,source->m_data->Document,source->m_data->Document,link.Entities,link.Asset,Registry());}
bool Prefab::Unpack(Entity entity){auto root=GetInstanceRoot(entity);if(!root)return false;root.RemoveComponent<PrefabLink>();return true;}
}
