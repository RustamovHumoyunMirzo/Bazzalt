#pragma once
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include <optional>
#include <string>
#include <type_traits>

namespace Bazzalt::Runtime {
// Process-local comparison keys, never serialized. Avoid locale/stream work on
// every physics tick and canonicalize signed zero from matrix calculations.
template<class T> void AppendPhysicsKey(std::string& key,const T& value) {
    if constexpr(std::is_enum_v<T>) AppendPhysicsKey(key,static_cast<std::underlying_type_t<T>>(value));
    else if constexpr(std::is_arithmetic_v<T>) {
        auto scalar=value;if constexpr(std::is_floating_point_v<T>)if(scalar==0)scalar=0;
        key.append(reinterpret_cast<const char*>(&scalar),sizeof(scalar));
    } else if constexpr(std::is_same_v<T,UUID>) {AppendPhysicsKey(key,value.GetHigh());AppendPhysicsKey(key,value.GetLow());}
    else if constexpr(std::is_same_v<T,std::optional<Vec3>>) {AppendPhysicsKey(key,value.has_value());if(value)AppendPhysicsKey(key,*value);}
    else if constexpr(std::is_same_v<T,Mat4>) {for(float element:value.Values)AppendPhysicsKey(key,element);}
    else {AppendPhysicsKey(key,value.X);AppendPhysicsKey(key,value.Y);AppendPhysicsKey(key,value.Z);if constexpr(std::is_same_v<T,Quaternion>)AppendPhysicsKey(key,value.W);}
}
template<class T> std::string PhysicsKey(const T& value){std::string key;AppendPhysicsKey(key,value);return key;}
}
