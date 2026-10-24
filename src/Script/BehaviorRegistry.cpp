#include "Runtime/BehaviorAccess.h"
#include "Bazzalt/Detail/SubsystemAccess.h"
#include <map>
namespace Bazzalt {
namespace {std::map<std::pair<std::string,std::string>,Behavior*> Instances;}
Behavior* Scripts::Find(Entity entity,const std::string& name){
    Detail::RequireSubsystemService(Detail::SubsystemService::Scene,"Scripts.Find");
    if(!entity||entity.GetScene()!=SceneManager::GetActiveScene())return nullptr;
    auto found=Instances.find({entity.GetUUID().ToString(),name});return found==Instances.end()?nullptr:found->second;
}
void Runtime::BehaviorAccess::Register(const std::string& entity,const std::string& name,Behavior* behavior){
    if(behavior&&!Instances.emplace(std::make_pair(entity,name),behavior).second)throw std::logic_error("Duplicate native script instance");
}
void Runtime::BehaviorAccess::Remove(Behavior* behavior){std::erase_if(Instances,[&](const auto& entry){return entry.second==behavior;});}
}
