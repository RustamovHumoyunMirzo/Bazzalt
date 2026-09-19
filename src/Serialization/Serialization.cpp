#include "Bazzalt/Serialization.h"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <ryml.hpp>
#include "Bazzalt/Components/Hierarchy.h"
#include "Bazzalt/Components/Name.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt { namespace {
struct EntityRecord { UUID Id; UUID Parent; std::string Name; std::vector<SerializedComponent> Components; };
std::string Text(ryml::ConstNodeRef n) { auto v=n.val(); return {v.str,v.len}; }
std::string Key(ryml::ConstNodeRef n) { auto v=n.key(); return {v.str,v.len}; }
std::string Quote(const std::string& s) { std::ostringstream o; o<<'"'; for(unsigned char c:s){ switch(c){case '\\':o<<"\\\\";break;case '"':o<<"\\\"";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:o<<static_cast<char>(c);} } o<<'"'; return o.str(); }
bool UInt(ryml::ConstNodeRef n, std::uint32_t& v) { auto s=Text(n); auto r=std::from_chars(s.data(),s.data()+s.size(),v); return r.ec==std::errc{}&&r.ptr==s.data()+s.size(); }
std::string Float(float v){std::ostringstream s;s<<std::setprecision(std::numeric_limits<float>::max_digits10)<<v;return s.str();}
bool Float(const PropertyMap& p,const char* k,float& v){auto i=p.find(k);if(i==p.end())return false;auto b=i->second.data(),e=b+i->second.size();auto r=std::from_chars(b,e,v);return r.ec==std::errc{}&&r.ptr==e;}
} // namespace

void ComponentSerializationRegistry::RegisterDescriptor(Descriptor d){for(auto& e:m_descriptors)if(e.Type==d.Type){e=std::move(d);return;}m_descriptors.emplace_back(std::move(d));}
const ComponentSerializationRegistry::Descriptor* ComponentSerializationRegistry::Find(const std::string& t)const{for(const auto& d:m_descriptors)if(d.Type==t)return &d;return nullptr;}

SceneSerializer::SceneSerializer(){m_components.Register<Transform>("Bazzalt.Transform",1,
[](const Transform& v,PropertyMap& o){o["Position.X"]=Float(v.Position.X);o["Position.Y"]=Float(v.Position.Y);o["Position.Z"]=Float(v.Position.Z);o["Rotation.X"]=Float(v.Rotation.X);o["Rotation.Y"]=Float(v.Rotation.Y);o["Rotation.Z"]=Float(v.Rotation.Z);o["Rotation.W"]=Float(v.Rotation.W);o["Scale.X"]=Float(v.Scale.X);o["Scale.Y"]=Float(v.Scale.Y);o["Scale.Z"]=Float(v.Scale.Z);},
[](Transform& v,const PropertyMap& i,std::uint32_t n){return n==1&&Float(i,"Position.X",v.Position.X)&&Float(i,"Position.Y",v.Position.Y)&&Float(i,"Position.Z",v.Position.Z)&&Float(i,"Rotation.X",v.Rotation.X)&&Float(i,"Rotation.Y",v.Rotation.Y)&&Float(i,"Rotation.Z",v.Rotation.Z)&&Float(i,"Rotation.W",v.Rotation.W)&&Float(i,"Scale.X",v.Scale.X)&&Float(i,"Scale.Y",v.Scale.Y)&&Float(i,"Scale.Z",v.Scale.Z);});}

bool SceneSerializer::Save(const Scene& scene,const std::filesystem::path& path){m_lastError.clear();std::ofstream s(path,std::ios::binary|std::ios::trunc);if(!s){m_lastError="Could not open scene for writing: "+path.string();return false;}s<<"FormatVersion: "<<CurrentFormatVersion<<"\nSceneUUID: "<<Quote(scene.GetUUID().ToString())<<"\nEntities:\n";auto view=scene.GetRegistry().view<Identity,Name,Hierarchy>();for(auto h:view){Entity e=const_cast<Scene&>(scene).GetEntity(static_cast<Entity::Id>(h));if(e.GetUUID().IsRoot())continue;std::vector<SerializedComponent> cs;for(const auto& d:m_components.GetDescriptors()){SerializedComponent c{d.Type,d.Version,{}};if(d.Serialize(e,c.Properties))cs.push_back(std::move(c));}if(auto* u=e.TryGetComponent<UnresolvedComponents>())for(const auto& c:u->Values)if(!m_components.Find(c.Type))cs.push_back(c);s<<"  - UUID: "<<Quote(e.GetUUID().ToString())<<"\n    Parent: "<<Quote(e.GetComponent<Hierarchy>().Parent.ToString())<<"\n    Name: "<<Quote(e.GetComponent<Name>().Value)<<"\n    Components:\n";for(const auto& c:cs){s<<"      - Type: "<<Quote(c.Type)<<"\n        Version: "<<c.Version<<"\n        Properties:\n";for(const auto& [k,v]:c.Properties)s<<"          "<<Quote(k)<<": "<<Quote(v)<<'\n';}}if(!s){m_lastError="Failed writing scene: "+path.string();return false;}return true;}

bool SceneSerializer::Load(Scene& scene,const std::filesystem::path& path){m_lastError.clear();std::ifstream f(path,std::ios::binary);std::string y((std::istreambuf_iterator<char>(f)),{});if(y.empty()){m_lastError="Could not read scene: "+path.string();return false;}std::vector<EntityRecord> rs;UUID sid;std::unordered_set<UUID> ids;try{auto tree=ryml::parse_in_arena(ryml::csubstr(y.data(),y.size()));auto root=tree.rootref();std::uint32_t ver=0;if(!root.has_child("FormatVersion")||!UInt(root["FormatVersion"],ver)||ver==0||ver>CurrentFormatVersion||!root.has_child("SceneUUID")||!UUID::TryParse(Text(root["SceneUUID"]),sid)||sid.IsRoot()||!root.has_child("Entities")){m_lastError="Invalid or unsupported scene YAML";return false;}for(auto en:root["Entities"].children()){EntityRecord r;if(!en.has_child("UUID")||!UUID::TryParse(Text(en["UUID"]),r.Id)||r.Id.IsRoot()||!ids.insert(r.Id).second||!en.has_child("Parent")||!UUID::TryParse(Text(en["Parent"]),r.Parent)||!en.has_child("Name")){m_lastError="Invalid entity in scene YAML";return false;}r.Name=Text(en["Name"]);if(en.has_child("Components"))for(auto cn:en["Components"].children()){SerializedComponent c;if(!cn.has_child("Type")||!cn.has_child("Version")||!UInt(cn["Version"],c.Version)){m_lastError="Invalid component in scene YAML";return false;}c.Type=Text(cn["Type"]);if(cn.has_child("Properties"))for(auto p:cn["Properties"].children())c.Properties[Key(p)]=Text(p);r.Components.push_back(std::move(c));}rs.push_back(std::move(r));}}catch(const std::exception& e){m_lastError="Could not parse scene YAML: "+std::string(e.what());return false;}for(const auto&r:rs)if(!r.Parent.IsRoot()&&!ids.count(r.Parent)){m_lastError="Entity references missing parent";return false;}scene.Clear();scene.m_uuid=sid;for(const auto&r:rs)scene.CreateEntity(r.Id,r.Name);for(const auto&r:rs){Entity e=scene.GetEntity(r.Id);if(!r.Parent.IsRoot()&&!scene.SetParent(e,scene.GetEntity(r.Parent))){m_lastError="Invalid hierarchy cycle";return false;}for(const auto&c:r.Components){if(auto*d=m_components.Find(c.Type)){if(!d->Deserialize(e,c.Properties,c.Version)){m_lastError="Could not deserialize component: "+c.Type;return false;}}else{auto*u=e.TryGetComponent<UnresolvedComponents>();if(!u)u=&e.AddComponent<UnresolvedComponents>();u->Values.push_back(c);}}}return true;}
} // namespace Bazzalt
