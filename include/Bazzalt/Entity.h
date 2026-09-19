#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

#include "Bazzalt/Component.h"
#include "Bazzalt/Math.h"
#include "Bazzalt/UUID.h"

namespace Bazzalt {

class Scene;

class Entity final {
public:
    using Id = std::uint32_t;

    Entity() = default;

    template<typename Component, typename... Args>
    Component& AddComponent(Args&&... args) const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        return m_registry->template emplace<Component>(
            m_handle, std::forward<Args>(args)...);
    }

    template<typename Component, typename... Args>
    Component& AddOrReplaceComponent(Args&&... args) const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        return m_registry->template emplace_or_replace<Component>(
            m_handle, std::forward<Args>(args)...);
    }

    template<typename Component>
    [[nodiscard]] bool HasComponent() const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        return IsValid() && m_registry->template all_of<Component>(m_handle);
    }

    template<typename Component>
    Component& GetComponent() const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        return m_registry->template get<Component>(m_handle);
    }

    template<typename Component>
    Component* TryGetComponent() const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        return IsValid() ? m_registry->template try_get<Component>(m_handle) : nullptr;
    }

    template<typename Component>
    void RemoveComponent() const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        m_registry->template remove<Component>(m_handle);
    }

    [[nodiscard]] bool IsValid() const {
        return m_registry != nullptr && m_registry->valid(m_handle);
    }

    [[nodiscard]] explicit operator bool() const { return IsValid(); }
    [[nodiscard]] Id GetId() const { return static_cast<Id>(m_handle); }
    [[nodiscard]] UUID GetUUID() const;
    [[nodiscard]] Scene* GetScene() const { return m_scene; }

    [[nodiscard]] Entity GetParent() const;
    [[nodiscard]] bool HasParent() const;
    [[nodiscard]] std::vector<Entity> GetChildren() const;
    [[nodiscard]] bool IsAncestorOf(Entity entity) const;
    [[nodiscard]] Mat4 GetWorldMatrix() const;
    bool SetParent(Entity parent) const;
    bool AddChild(Entity child) const;
    bool RemoveParent() const;

    friend bool operator==(Entity left, Entity right) {
        return left.m_handle == right.m_handle && left.m_registry == right.m_registry;
    }

    friend bool operator!=(Entity left, Entity right) { return !(left == right); }

private:
    friend class Scene;

    Entity(entt::entity handle, entt::registry& registry, Scene& scene)
        : m_handle(handle), m_registry(&registry), m_scene(&scene) {}

    entt::entity m_handle{entt::null};
    entt::registry* m_registry = nullptr;
    Scene* m_scene = nullptr;
};

} // namespace Bazzalt
