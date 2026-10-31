// Private editor automation VM. No gameplay bindings, native modules or OS libraries.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <lua.hpp>
#include <map>
#include <string>
#include <vector>
#include <cstdlib>
#include <stdexcept>
#include <cmath>
namespace py=pybind11;
namespace Bazzalt::EditorBridge {
class EditorLua {
    struct Memory {std::size_t Bytes=0;} MemoryState;
    bool Closing=false;lua_State* L=nullptr;py::object Dispatch;std::map<std::string,std::string> Modules;int Object=LUA_NOREF;
    static void* Allocate(void* context,void* data,size_t oldSize,size_t size){auto& m=*static_cast<Memory*>(context);if(!data)oldSize=0;if(!size){std::free(data);m.Bytes-=oldSize;return nullptr;}if(size>32*1024*1024||m.Bytes-oldSize>32*1024*1024-size)return nullptr;auto p=std::realloc(data,size);if(p)m.Bytes=m.Bytes-oldSize+size;return p;}
    static void Budget(lua_State* l,lua_Debug*){luaL_error(l,"Editor Lua instruction budget exceeded");}
    void Check(int status){if(status!=LUA_OK){std::string error=lua_tostring(L,-1)?lua_tostring(L,-1):"Editor Lua failure";lua_pop(L,1);throw std::runtime_error(error);}}
    static EditorLua* Self(lua_State* l){return static_cast<EditorLua*>(lua_touserdata(l,lua_upvalueindex(1)));}
    static py::object Read(lua_State* l,int index,int depth=0){
        if(depth>16)throw std::runtime_error("Editor Lua value nesting limit exceeded");index=lua_absindex(l,index);
        switch(lua_type(l,index)){
        case LUA_TNIL:return py::none();case LUA_TBOOLEAN:return py::bool_(lua_toboolean(l,index)!=0);
        case LUA_TNUMBER:if(lua_isinteger(l,index))return py::int_(lua_tointeger(l,index));else {double n=lua_tonumber(l,index);if(!std::isfinite(n))throw std::runtime_error("Nonfinite editor argument");return py::float_(n);}
        case LUA_TSTRING:{size_t size;const char* s=lua_tolstring(l,index,&size);if(size>1024*1024)throw std::runtime_error("Editor text exceeds limit");return py::str(s,size);}
        case LUA_TTABLE:{py::dict result;std::size_t count=0;bool array=true;lua_pushnil(l);while(lua_next(l,index)){if(++count>4096)throw std::runtime_error("Editor table exceeds limit");if(!lua_isinteger(l,-2)||lua_tointeger(l,-2)<1)array=false;auto key=Read(l,-2,depth+1);result[key]=Read(l,-1,depth+1);lua_pop(l,1);}if(array&&count){py::list values;for(std::size_t i=1;i<=count;++i){auto key=py::int_(i);if(!result.contains(key)){array=false;break;}values.append(result[key]);}if(array)return values;}return result;}
        default:throw std::runtime_error("Editor commands accept data, not functions or userdata");}
    }
    static std::size_t Cost(py::handle value,int depth=0){
        if(depth>16)throw std::runtime_error("Editor result nesting limit exceeded");std::size_t cost=128;
        if(py::isinstance<py::str>(value))cost+=py::cast<std::string>(value).size();
        else if(py::isinstance<py::dict>(value))for(auto item:py::reinterpret_borrow<py::dict>(value)){cost+=Cost(item.first,depth+1)+Cost(item.second,depth+1);if(cost>8*1024*1024)break;}
        else if(py::isinstance<py::list>(value)||py::isinstance<py::tuple>(value))for(auto item:py::reinterpret_borrow<py::sequence>(value)){cost+=Cost(item,depth+1);if(cost>8*1024*1024)break;}
        if(cost>8*1024*1024)throw std::runtime_error("Editor result exceeds transfer budget");return cost;
    }
    static void Push(lua_State* l,py::handle value,int depth=0){
        if(depth>16)throw std::runtime_error("Editor result nesting limit exceeded");
        if(value.is_none())lua_pushnil(l);else if(py::isinstance<py::bool_>(value))lua_pushboolean(l,py::cast<bool>(value));
        else if(py::isinstance<py::int_>(value))lua_pushinteger(l,py::cast<lua_Integer>(value));else if(py::isinstance<py::float_>(value))lua_pushnumber(l,py::cast<double>(value));
        else if(py::isinstance<py::str>(value)){auto s=py::cast<std::string>(value);if(s.size()>1024*1024)throw std::runtime_error("Editor result exceeds limit");lua_pushlstring(l,s.data(),s.size());}
        else if(py::isinstance<py::dict>(value)){auto dict=py::reinterpret_borrow<py::dict>(value);if(dict.size()>4096)throw std::runtime_error("Editor result table exceeds limit");lua_newtable(l);for(auto item:dict){Push(l,item.first,depth+1);Push(l,item.second,depth+1);lua_settable(l,-3);}}
        else if(py::isinstance<py::list>(value)||py::isinstance<py::tuple>(value)){auto seq=py::reinterpret_borrow<py::sequence>(value);if(seq.size()>4096)throw std::runtime_error("Editor result list exceeds limit");lua_newtable(l);for(py::ssize_t i=0;i<seq.size();++i){Push(l,seq[i],depth+1);lua_rawseti(l,-2,i+1);}}
        else throw std::runtime_error("Unsupported editor result");
    }
    static int Command(lua_State* l){{std::string error;try{auto* self=Self(l);if(self->Closing)throw std::runtime_error("Editor VM is closing");if(lua_gettop(l)>16||self->MemoryState.Bytes>16*1024*1024)throw std::runtime_error("Editor command exceeds transfer budget");std::string name=lua_tostring(l,lua_upvalueindex(2));py::list arguments;for(int i=1;i<=lua_gettop(l);++i)arguments.append(Read(l,i));auto result=self->Dispatch(name,arguments);(void)Cost(result);Push(l,result);return 1;}catch(const std::exception& e){error=e.what();}lua_pushnil(l);lua_pushlstring(l,error.data(),error.size());}return 2;}
    static int Require(lua_State* l){{std::string error;try{auto* self=Self(l);auto name=std::string(lua_tostring(l,1)?lua_tostring(l,1):"");auto found=self->Modules.find(name);if(found==self->Modules.end())throw std::runtime_error("Module is not in the stored editor bundle: "+name);lua_getfield(l,LUA_REGISTRYINDEX,"EditorModules");lua_getfield(l,-1,name.c_str());if(!lua_isnil(l,-1))return 1;lua_pop(l,1);lua_pushboolean(l,1);lua_setfield(l,-2,name.c_str());self->Check(luaL_loadbufferx(l,found->second.data(),found->second.size(),name.c_str(),"t"));self->Check(lua_pcall(l,0,1,0));if(lua_isnil(l,-1)){lua_pop(l,1);lua_pushboolean(l,1);}lua_pushvalue(l,-1);lua_setfield(l,-3,name.c_str());return 1;}catch(const std::exception& e){error=e.what();}lua_pushnil(l);lua_pushlstring(l,error.data(),error.size());}return 2;}
public:
    EditorLua(const std::string& source,const py::dict& modules,py::object dispatch,const std::vector<std::string>& methods):Dispatch(std::move(dispatch)){
        std::size_t bytes=source.size();if(bytes>4*1024*1024||modules.size()>32||methods.size()>128)throw std::runtime_error("Editor script bundle exceeds limits");for(auto item:modules){auto name=py::cast<std::string>(item.first),text=py::cast<std::string>(item.second);bytes+=text.size();if(bytes>8*1024*1024)throw std::runtime_error("Editor bundle exceeds 8 MiB");Modules.emplace(name,text);}
        L=lua_newstate(Allocate,&MemoryState);if(!L)throw std::runtime_error("Cannot allocate editor Lua VM");
        try{
            for(auto lib:std::vector<std::pair<const char*,lua_CFunction>>{{"_G",luaopen_base},{LUA_MATHLIBNAME,luaopen_math},{LUA_STRLIBNAME,luaopen_string},{LUA_TABLIBNAME,luaopen_table},{LUA_UTF8LIBNAME,luaopen_utf8}}){luaL_requiref(L,lib.first,lib.second,1);lua_pop(L,1);}
            for(const char* name:{"load","loadfile","dofile","collectgarbage","pcall","xpcall"}){lua_pushnil(L);lua_setglobal(L,name);}
            lua_newtable(L);lua_setfield(L,LUA_REGISTRYINDEX,"EditorModules");lua_pushlightuserdata(L,this);lua_pushcclosure(L,Require,1);lua_setglobal(L,"require");
            lua_newtable(L);for(const auto& name:methods){lua_pushlightuserdata(L,this);lua_pushlstring(L,name.data(),name.size());lua_pushcclosure(L,Command,2);lua_setfield(L,-2,name.c_str());}lua_setglobal(L,"Editor");
            const char* wrappers=R"lua(
                local function wrap(raw)
                    return function(...) local value,why=raw(...); if why then error(why,2) end; return value end
                end
                for name,callback in pairs(Editor) do Editor[name]=wrap(callback) end
                require=wrap(require)
                local raw=setmetatable
                setmetatable=function(value,meta)
                    if meta and (rawget(meta,"__gc")~=nil or rawget(meta,"__close")~=nil) then error("Editor Lua finalizers are not available",2) end
                    return raw(value,meta)
                end
            )lua";
            Check(luaL_loadstring(L,wrappers));Check(lua_pcall(L,0,0,0));
            lua_sethook(L,Budget,LUA_MASKCOUNT,1000000);Check(luaL_loadbufferx(L,source.data(),source.size(),"@EditorTool","t"));Check(lua_pcall(L,0,1,0));lua_sethook(L,nullptr,0,0);
            if(!lua_istable(L,-1)&&!lua_isfunction(L,-1))throw std::runtime_error("Editor script must return a callback table or function");Object=luaL_ref(L,LUA_REGISTRYINDEX);
        }catch(...){lua_close(L);L=nullptr;throw;}
    }
    ~EditorLua(){if(L){Closing=true;lua_sethook(L,Budget,LUA_MASKCOUNT,1000000);lua_close(L);}}
    bool Call(const std::string& name,py::object argument){
        const int top=lua_gettop(L);try{lua_rawgeti(L,LUA_REGISTRYINDEX,Object);bool table=lua_istable(L,-1);if(table){lua_pushlstring(L,name.data(),name.size());lua_rawget(L,-2);if(lua_isnil(L,-1)){lua_settop(L,top);return false;}lua_pushvalue(L,-2);}else if(name!="OnRun"){lua_settop(L,top);return false;}
            Push(L,argument);lua_sethook(L,Budget,LUA_MASKCOUNT,1000000);Check(lua_pcall(L,table?2:1,1,0));lua_sethook(L,nullptr,0,0);bool handled=lua_toboolean(L,-1);lua_settop(L,top);return handled;
        }catch(...){lua_sethook(L,nullptr,0,0);lua_settop(L,top);throw;}
    }
};
void BindEditorLua(py::module_& m){py::class_<EditorLua>(m,"EditorLua").def(py::init<const std::string&,const py::dict&,py::object,const std::vector<std::string>&>()).def("call",&EditorLua::Call,py::arg("function"),py::arg("argument")=py::none());}
}
