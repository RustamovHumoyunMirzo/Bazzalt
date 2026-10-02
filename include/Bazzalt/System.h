#pragma once

#include <type_traits>

#include <entt/entity/registry.hpp>

#include "Bazzalt/Component.h"

namespace Bazzalt {

class Scene;

class System {
public:
    virtual ~System() = default;

    [[nodiscard]] bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) { m_enabled = enabled; }

protected:
    virtual void OnCreate(Scene&) {}
    virtual void OnUpdate(Scene&, float deltaTime) = 0;
    virtual void OnFixedUpdate(Scene&, float fixedDeltaTime) { (void)fixedDeltaTime; }
    virtual void OnDestroy(Scene&) {}

private:
    friend class Scene;
    bool m_enabled = true;
};

template<typename... Components>
class ComponentSystem : public System {
    static_assert(sizeof...(Components) > 0,
                  "ComponentSystem requires at least one component type");
    static_assert((std::is_base_of_v<Component, Components> && ...),
                  "Every component type must derive from Bazzalt::Component");

protected:
    static auto GetView(entt::registry& registry) {
        return registry.template view<Components...>();
    }

    static auto GetView(const entt::registry& registry) {
        return registry.template view<Components...>();
    }

    template<typename View, typename EntityType>
    static bool AreComponentsEnabled(const View& view, EntityType entity) {
        return (view.template get<Components>(entity).IsEnabled() && ...);
    }
};

} // namespace Bazzalt
