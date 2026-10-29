#pragma once
#include "Bazzalt/Export.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"
#include "Bazzalt/Entity.h"
#include <functional>
#include <string>
#include <variant>
#include <vector>
namespace Bazzalt {
enum class EventArgumentType : std::uint8_t { Boolean, Integer, Number, String, Vec2, Vec3, Vec4, Quaternion, UUID };
using EventValue=std::variant<bool,std::int64_t,double,std::string,Vec2,Vec3,Vec4,Quaternion,UUID>;
struct EventArgument {std::string Name;EventArgumentType Type=EventArgumentType::Number;};
struct EventDefinition {std::string Name;std::vector<EventArgument> Arguments;};
struct BAZZALT_API EventInvocation {
    UUID Event{};
    std::string Name;
    std::vector<EventArgument> Arguments;
    std::vector<EventValue> Values;
    [[nodiscard]] const EventValue& GetValue(const std::string& name)const;
    template<class T> [[nodiscard]] const T& Get(const std::string& name)const{return std::get<T>(GetValue(name));}
};
// Project-authored typed event channel. All operations run on the engine thread.
class BAZZALT_API GlobalEvent final {
public:
    using Subscription=std::uint64_t;
    using Callback=std::function<void(const EventInvocation&)>;
    GlobalEvent()=default;
    [[nodiscard]] static GlobalEvent Load(UUID asset);
    [[nodiscard]] UUID GetAssetUUID()const{return m_asset;}
    [[nodiscard]] bool IsValid()const;
    [[nodiscard]] EventDefinition GetDefinition()const;
    // Positional values must match the schema exactly. Nested fires are queued FIFO.
    void Fire(std::vector<EventValue> arguments={})const;
    [[nodiscard]] Subscription Subscribe(Callback callback,Entity owner={})const;
    bool Unsubscribe(Subscription subscription)const;
private:
    UUID m_asset{};
};
}
