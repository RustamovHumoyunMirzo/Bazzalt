#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
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
struct Hierarchy;
struct Identity;
struct Name;
struct Transform;

class Entity final {
public:
    using Id = std::uint32_t;

    Entity() = default;

    template<typename Component, typename... Args>
    Component& AddComponent(Args&&... args) const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        RequireValid("AddComponent");
        if (m_registry->template all_of<Component>(m_handle))
            throw std::logic_error("Entity already has the requested component");
        return m_registry->template emplace<Component>(
            m_handle, std::forward<Args>(args)...);
    }

    template<typename Component, typename... Args>
    Component& AddOrReplaceComponent(Args&&... args) const {
        static_assert(std::is_base_of_v<Bazzalt::Component, Component>,
                      "Component must derive from Bazzalt::Component");
        static_assert(!std::is_same_v<Component, Identity> &&
                      !std::is_same_v<Component, Hierarchy>,
                      "Identity and Hierarchy are managed by Scene");
        RequireValid("AddOrReplaceComponent");
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
        RequireValid("GetComponent");
        if (!m_registry->template all_of<Component>(m_handle))
            throw std::logic_error("Entity does not have the requested component");
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
        static_assert(!std::is_same_v<Component, Identity> &&
                      !std::is_same_v<Component, Hierarchy> &&
                      !std::is_same_v<Component, Name> &&
                      !std::is_same_v<Component, Transform>,
                      "Core entity components cannot be removed");
        RequireValid("RemoveComponent");
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
    [[nodiscard]] Transform GetWorldTransform() const;
    bool SetWorldTransform(const Transform& transform) const;
    bool SetParent(Entity parent, bool worldPositionStays = true) const;
    bool AddChild(Entity child, bool worldPositionStays = true) const;
    bool RemoveParent(bool worldPositionStays = true) const;

    friend bool operator==(Entity left, Entity right) {
        return left.m_handle == right.m_handle && left.m_registry == right.m_registry;
    }

    friend bool operator!=(Entity left, Entity right) { return !(left == right); }

private:
    friend class Scene;

    Entity(entt::entity handle, entt::registry& registry, Scene& scene)
        : m_handle(handle), m_registry(&registry), m_scene(&scene) {}

    void RequireValid(const char* operation) const {
        if (!IsValid())
            throw std::logic_error(std::string(operation) + " called on an invalid entity");
    }

    entt::entity m_handle{entt::null};
    entt::registry* m_registry = nullptr;
    Scene* m_scene = nullptr;
};

} // namespace Bazzalt
