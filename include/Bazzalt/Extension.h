#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <functional>
#include <string>
#include <variant>
#include <vector>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace Bazzalt {
using ExtensionValue=std::variant<std::monostate,bool,std::int64_t,double,std::string,Vec2,Vec3,Vec4,UUID>;
using ExtensionArguments=std::vector<ExtensionValue>;
using ExtensionFunction=std::function<ExtensionValue(const ExtensionArguments&)>;
// Register ordinary C++ functions; Lua conversion is provided by the engine.
class BAZZALT_API ExtensionRegistry final {
public:
    void Export(const std::string& module,const std::string& name,ExtensionFunction function);
    template<class R,class... A> void Bind(const std::string& module,const std::string& name,R(*function)(A...)) {
        Export(module,name,[function](const ExtensionArguments& args)->ExtensionValue {
            if(args.size()!=sizeof...(A))throw std::invalid_argument("Extension function argument count mismatch");
            return Invoke(function,args,std::index_sequence_for<A...>{});
        });
    }
private:
    template<class T> static auto Convert(const ExtensionValue& value){
        using V=std::remove_cvref_t<T>;
        if constexpr(std::is_floating_point_v<V>){if(auto* number=std::get_if<std::int64_t>(&value))return static_cast<V>(*number);return static_cast<V>(std::get<double>(value));}
        else if constexpr(std::is_integral_v<V>&&!std::is_same_v<V,bool>){auto n=std::get<std::int64_t>(value);if(!std::in_range<V>(n))throw std::out_of_range("Extension integer argument overflow");return static_cast<V>(n);}
        else return std::get<V>(value);
    }
    template<class R,class... A,std::size_t... I> static ExtensionValue Invoke(R(*fn)(A...),const ExtensionArguments& args,std::index_sequence<I...>){
        if constexpr(std::is_void_v<R>){fn(Convert<A>(args[I])...);return {};}
        else if constexpr(std::is_integral_v<R>&&!std::is_same_v<R,bool>)return std::int64_t(fn(Convert<A>(args[I])...));
        else if constexpr(std::is_floating_point_v<R>)return double(fn(Convert<A>(args[I])...));
        else return fn(Convert<A>(args[I])...);
    }
};
class BAZZALT_API Extensions final {
public:
    Extensions()=delete;
    static UUID GetAsset(const std::string& extension,const std::string& asset);
    static bool HasModule(const std::string& name);
    static std::vector<std::string> GetModules();
    static ExtensionValue Call(const std::string& module,const std::string& function,const ExtensionArguments& arguments={});
};
inline constexpr std::uint32_t ExtensionAbiVersion=1;
struct ExtensionModuleApi {std::uint32_t AbiVersion;void(*Register)(ExtensionRegistry&);};
}
#ifdef _WIN32
#define BAZZALT_EXTENSION_EXPORT __declspec(dllexport)
#else
#define BAZZALT_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif
#define BAZZALT_EXTENSION(RegisterFunction) extern "C" BAZZALT_EXTENSION_EXPORT const ::Bazzalt::ExtensionModuleApi* BazzaltGetExtensionModuleV1(){static const ::Bazzalt::ExtensionModuleApi api{::Bazzalt::ExtensionAbiVersion,RegisterFunction};return &api;}
