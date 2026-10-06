#include "Runtime/LuaRuntime.h"
#include "Script/LuaBindings.h"
#include "Script/LuaSystems.h"
#include "Bazzalt/EntityReference.h"
#include <fstream>
#include <iostream>
#include <set>
#include <cstdlib>
#include <sstream>
namespace Bazzalt::Runtime {
static_assert(LUA_VERSION_RELEASE_NUM==50409,"Update importer version and bytecode tests when changing Lua");
namespace {
constexpr std::size_t MaxScriptBytes=4*1024*1024,MaxVmBytes=32*1024*1024;
struct Memory { std::size_t Used=0; };
void* Allocate(void* context,void* pointer,std::size_t oldSize,std::size_t size){
    auto& memory=*static_cast<Memory*>(context);if(!pointer)oldSize=0;
    if(!size){std::free(pointer);memory.Used-=oldSize;return nullptr;}
    if(size>MaxVmBytes || memory.Used-oldSize>MaxVmBytes-size)return nullptr;
    auto* result=std::realloc(pointer,size);if(result)memory.Used=memory.Used-oldSize+size;return result;
}
void Budget(lua_State* state,lua_Debug*) { luaL_error(state,"Lua callback exceeded its instruction budget"); }
std::string Read(const std::filesystem::path& path){
    std::error_code error;auto size=std::filesystem::file_size(path,error);
    if(error||size>MaxScriptBytes)throw std::runtime_error("Lua asset is missing or exceeds 4 MiB");
    std::ifstream stream(path,std::ios::binary);std::string result((std::istreambuf_iterator<char>(stream)),{});
    if(!stream.good()&&!stream.eof())throw std::runtime_error("Could not read Lua asset");return result;
}
struct VM {
    Memory MemoryState;
    lua_State* State=lua_newstate(Allocate,&MemoryState);
    VM(){if(!State)throw std::runtime_error("Could not allocate Lua VM");}
    ~VM(){lua_close(State);}
};
void SetProperty(sol::table object,const std::string& name,const std::string& value){
    // Editor properties are scalars or explicitly declared vector/entity values.
    sol::object current=object[name];
    const auto assetId=[&](){UUID id;if(!UUID::TryParse(value,id))throw std::invalid_argument("Invalid Lua reference property");return id;};
    const auto numbers=[&](std::size_t count){std::vector<float> result;std::istringstream input(value);std::string item;while(std::getline(input,item,',')){std::size_t used=0;float number=std::stof(item,&used);if(used!=item.size()||!std::isfinite(number))throw std::invalid_argument("Invalid Lua vector property");result.push_back(number);}if(result.size()!=count)throw std::invalid_argument("Wrong Lua vector size");return result;};
    if(current.is<UUID>()){UUID id;if(!UUID::TryParse(value,id))throw std::invalid_argument("Invalid Lua UUID property");object[name]=id;}
    else if(current.is<Entity>()){UUID id;if(!UUID::TryParse(value,id))throw std::invalid_argument("Invalid Lua entity property");auto* scene=SceneManager::GetActiveScene();object[name]=scene?scene->GetEntity(id):Entity{};}
    else if(current.is<EntityReference>())object[name]=EntityReference(assetId());
    else if(current.is<Material>())object[name]=Material::Load(assetId());
    else if(current.is<Shader>())object[name]=Shader::Load(assetId());
    else if(current.get_type()==sol::type::boolean){if(value!="true"&&value!="false")throw std::invalid_argument("Invalid Lua boolean property");object[name]=(value=="true");}
    else if(current.get_type()==sol::type::number){std::size_t used=0;double number=std::stod(value,&used);if(used!=value.size()||!std::isfinite(number))throw std::invalid_argument("Invalid Lua number property");object[name]=number;}
    else if(current.is<Vec2>()){auto n=numbers(2);object[name]=Vec2{n[0],n[1]};}
    else if(current.is<Vec3>()){auto n=numbers(3);object[name]=Vec3{n[0],n[1],n[2]};}
    else if(current.is<Vec4>()){auto n=numbers(4);object[name]=Vec4{n[0],n[1],n[2],n[3]};}
    else if(current.is<Quaternion>()){auto n=numbers(4);object[name]=Quaternion{n[0],n[1],n[2],n[3]};}
    else object[name]=value;
}
}
struct LuaRuntime::Impl {
    struct Instance {
        std::unique_ptr<VM> Vm;
        sol::table Object;
        Entity Owner;
        std::string Name;
        bool Failed=false;
        bool Destroyed=false;
        void Call(const char* name,float dt=0,bool timed=false){
            const bool destroying=std::string_view(name)=="OnDestroy";
            if(!Object.valid()||Destroyed||(Failed&&!destroying))return;
            if(destroying)Destroyed=true;
            else if(!Owner.IsValid()){Call("OnDestroy");return;}
            if(timed)if(auto* scripts=Owner.TryGetComponent<ScriptComponents>()){
                if(!scripts->IsEnabled())return;
                for(const auto& script:scripts->Values)if(script.TypeName==Name&&!script.Enabled)return;
            }
            sol::object member=Object[name];if(!member.valid()||member==sol::nil)return;
            if(member.get_type()!=sol::type::function)throw std::runtime_error(std::string(name)+" must be a function");
            lua_sethook(Vm->State,Budget,LUA_MASKCOUNT,1000000);
            sol::protected_function function=member;
            sol::protected_function_result result=timed?function(Object,dt):function(Object);
            lua_sethook(Vm->State,nullptr,0,0);
            if(!result.valid()){sol::error error=result;throw std::runtime_error(Name+": "+error.what());}
        }
    };
    std::vector<ScriptBinding> Bindings;
    std::vector<std::unique_ptr<Instance>> Instances;
};
LuaRuntime::LuaRuntime():m_impl(std::make_unique<Impl>()){}
LuaRuntime::~LuaRuntime(){Stop();}
bool LuaRuntime::Configure(std::vector<ScriptBinding> bindings,std::string& error){
    if(!m_impl->Instances.empty()){error="Cannot configure Lua while playing";return false;}
    std::set<std::pair<std::string,std::string>> unique;
    std::map<std::string,std::filesystem::path> types;
    for(const auto& binding:bindings){
        if(binding.TypeName.empty()||!unique.emplace(binding.Entity,binding.TypeName).second){error="Duplicate or unnamed Lua component";return false;}
        const auto path=binding.Module.lexically_normal();auto found=types.find(binding.TypeName);
        if(found!=types.end()&&found->second!=path){error="Ambiguous Lua component type: "+binding.TypeName;return false;}types.emplace(binding.TypeName,path);
    }
    m_impl->Bindings=std::move(bindings);error.clear();return true;
}
bool LuaRuntime::Start(std::string& error){
    Stop();error.clear();
    try{for(const auto& binding:m_impl->Bindings){
        UUID id;if(!UUID::TryParse(binding.Entity,id))throw std::runtime_error("Invalid Lua entity UUID");
        auto* scene=SceneManager::GetActiveScene();auto owner=scene?scene->GetEntity(id):Entity{};
        if(!owner)throw std::runtime_error("Lua attachment entity is missing");
        auto instance=std::make_unique<Impl::Instance>();instance->Vm=std::make_unique<VM>();instance->Owner=owner;instance->Name=binding.TypeName;
        sol::state_view lua(instance->Vm->State);
        lua.open_libraries(sol::lib::base,sol::lib::math,sol::lib::string,sol::lib::table,sol::lib::utf8);
        for(const char* key:{"dofile","loadfile","load","collectgarbage"})lua[key]=sol::nil;
        auto api=lua.create_named_table("Bazzalt");BindLuaMath(api);BindLuaComponents(api);BindLuaServices(api);
        m_impl->Instances.push_back(std::move(instance));auto& active=*m_impl->Instances.back();
        const auto bytes=Read(binding.Module);
        lua_sethook(lua.lua_state(),Budget,LUA_MASKCOUNT,1000000);
        auto result=lua.safe_script(bytes,sol::script_pass_on_error, "@"+binding.TypeName,sol::load_mode::binary);
        lua_sethook(lua.lua_state(),nullptr,0,0);
        if(!result.valid()){sol::error failure=result;throw std::runtime_error(failure.what());}
        if(result.get_type()!=sol::type::table)throw std::runtime_error("Lua behavior must return a table");
        active.Object=result.get<sol::table>();active.Object["Entity"]=owner;
        for(const auto& [key,value]:binding.Properties)SetProperty(active.Object,key,value);
        active.Call("OnCreate");
    }}catch(const std::exception& failure){error=failure.what();Stop();return false;}
    return true;
}
void LuaRuntime::Update(float dt){for(auto& instance:m_impl->Instances)try{instance->Call("OnUpdate",dt,true);}catch(const std::exception& error){instance->Failed=true;std::cerr<<"[Lua] "<<error.what()<<'\n';}}
void LuaRuntime::FixedUpdate(float dt){for(auto& instance:m_impl->Instances)try{instance->Call("OnFixedUpdate",dt,true);}catch(const std::exception& error){instance->Failed=true;std::cerr<<"[Lua] "<<error.what()<<'\n';}}
void LuaRuntime::Stop() noexcept {
    if(!m_impl->Instances.empty())if(auto* scene=SceneManager::GetActiveScene())try{scene->RemoveSystem<LuaSystems>();}catch(const std::exception& error){std::cerr<<"[Lua] "<<error.what()<<'\n';}
    for(auto it=m_impl->Instances.rbegin();it!=m_impl->Instances.rend();++it)try{(*it)->Call("OnDestroy");}catch(const std::exception& error){std::cerr<<"[Lua] "<<error.what()<<'\n';}
    if(!m_impl->Instances.empty())if(auto* scene=SceneManager::GetActiveScene())try{scene->RemoveSystem<LuaSystems>();}catch(const std::exception& error){std::cerr<<"[Lua] "<<error.what()<<'\n';}
    m_impl->Instances.clear();
}
bool LuaRuntime::Import(const std::filesystem::path& source,const std::filesystem::path& output,std::string& error){
    try{VM vm;auto bytes=Read(source);if(luaL_loadbufferx(vm.State,bytes.data(),bytes.size(),"@LuaAsset","t")!=LUA_OK){error=lua_tostring(vm.State,-1);return false;}
        std::string bytecode;auto writer=[](lua_State*,const void* data,std::size_t size,void* context)->int{auto& out=*static_cast<std::string*>(context);if(size>MaxScriptBytes||out.size()>MaxScriptBytes-size)return 1;out.append(static_cast<const char*>(data),size);return 0;};
        if(lua_dump(vm.State,writer,&bytecode,0)!=0)throw std::runtime_error("Lua bytecode exceeds limit");
        std::ofstream file(output,std::ios::binary|std::ios::trunc);file.write(bytecode.data(),static_cast<std::streamsize>(bytecode.size()));file.close();if(!file)throw std::runtime_error("Cannot write Lua bytecode asset");error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
}
