#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

#include "Bazzalt/Components/Name.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Entity.h"
#include "Bazzalt/System.h"

namespace Bazzalt {

class Scene final {
public:
    Scene() = default;
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    Entity CreateEntity(std::string name = {});
    void DestroyEntity(Entity entity);
    void Clear();

    [[nodiscard]] Entity FindEntityByName(const std::string& name);
    [[nodiscard]] Entity GetEntity(Entity::Id id);
    [[nodiscard]] std::size_t GetEntityCount() const;

    void Update(float deltaTime);

    template<typename SystemType, typename... Args>
    SystemType& AddSystem(Args&&... args) {
        static_assert(std::is_base_of_v<System, SystemType>,
                      "SystemType must derive from Bazzalt::System");
        const std::type_index type = typeid(SystemType);
        auto existing = m_systemLookup.find(type);
        if (existing != m_systemLookup.end()) {
            return static_cast<SystemType&>(*existing->second);
        }

        auto system = std::make_unique<SystemType>(std::forward<Args>(args)...);
        auto* result = system.get();
        m_systemLookup.emplace(type, result);
        m_systems.emplace_back(std::move(system));
        result->OnCreate(*this);
        return *result;
    }

    template<typename SystemType>
    [[nodiscard]] bool HasSystem() const {
        return m_systemLookup.find(typeid(SystemType)) != m_systemLookup.end();
    }

    template<typename SystemType>
    SystemType& GetSystem() {
        return static_cast<SystemType&>(*m_systemLookup.at(typeid(SystemType)));
    }

    template<typename SystemType>
    const SystemType& GetSystem() const {
        return static_cast<const SystemType&>(*m_systemLookup.at(typeid(SystemType)));
    }

    template<typename SystemType>
    bool RemoveSystem() {
        const std::type_index type = typeid(SystemType);
        auto found = m_systemLookup.find(type);
        if (found == m_systemLookup.end()) {
            return false;
        }

        System* target = found->second;
        target->OnDestroy(*this);
        m_systemLookup.erase(found);
        m_systems.erase(std::remove_if(m_systems.begin(), m_systems.end(),
            [target](const std::unique_ptr<System>& system) {
                return system.get() == target;
            }), m_systems.end());
        return true;
    }

    [[nodiscard]] entt::registry& GetRegistry() { return m_registry; }
    [[nodiscard]] const entt::registry& GetRegistry() const { return m_registry; }

private:
    void DestroySystems();

    entt::registry m_registry;
    std::vector<std::unique_ptr<System>> m_systems;
    std::unordered_map<std::type_index, System*> m_systemLookup;
};

} // namespace Bazzalt
