#pragma once

#include <algorithm>
#include <memory>
#include <map>
#include <exception>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

#include "Bazzalt/Components/Name.h"
#include "Bazzalt/Components/Hierarchy.h"
#include "Bazzalt/Components/Identity.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Components/LOD.h"
#include "Bazzalt/Entity.h"
#include "Bazzalt/ModelAsset.h"
#include "Bazzalt/SceneQuery.h"
#include "Bazzalt/System.h"
#include "Bazzalt/SceneEnvironment.h"
#include "Bazzalt/Physics.h"

namespace Bazzalt {

namespace Runtime { class Engine; }

class BAZZALT_API Scene final {
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    [[nodiscard]] PhysicsScene GetPhysics() { return PhysicsScene(*this); }
    [[nodiscard]] const PhysicsSettings& GetPhysicsSettings() const { return m_physicsSettings; }
    bool SetPhysicsSettings(const PhysicsSettings& settings);
    [[nodiscard]] PhysicsHit Raycast(Vec3 origin,Vec3 direction,float distance=10000,const PhysicsQueryOptions& options={}) { return GetPhysics().Raycast(origin,direction,distance,options); }
    [[nodiscard]] std::vector<PhysicsHit> RaycastAll(Vec3 origin,Vec3 direction,float distance=10000,const PhysicsQueryOptions& options={}) { return GetPhysics().RaycastAll(origin,direction,distance,options); }
    [[nodiscard]] PhysicsHit ShapeCast(const PhysicsShape& shape,const Transform& transform,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) { return GetPhysics().ShapeCast(shape,transform,direction,distance,options); }
    [[nodiscard]] std::vector<PhysicsHit> ShapeCastAll(const PhysicsShape& shape,const Transform& transform,Vec3 direction,float distance,const PhysicsQueryOptions& options={}) { return GetPhysics().ShapeCastAll(shape,transform,direction,distance,options); }
    [[nodiscard]] std::vector<PhysicsHit> OverlapShape(const PhysicsShape& shape,const Transform& transform,const PhysicsQueryOptions& options={}) { return GetPhysics().OverlapShape(shape,transform,options); }
    Entity CreateEntity(std::string name = {});
    Entity CreateEntity(UUID uuid, std::string name = {});
    void DestroyEntity(Entity entity);
    void Clear();

    [[nodiscard]] Entity FindEntityByName(const std::string& name);
    [[nodiscard]] Entity GetEntity(Entity::Id id);
    [[nodiscard]] Entity GetEntity(UUID uuid);
    [[nodiscard]] Entity GetRootEntity();
    [[nodiscard]] UUID GetUUID() const { return m_uuid; }
    [[nodiscard]] const SceneEnvironment& GetEnvironment() const { return m_environment; }
    bool SetEnvironment(const SceneEnvironment& value) { if(!value.IsValid())return false;m_environment=value;return true; }
    [[nodiscard]] std::size_t GetEntityCount() const;
    [[nodiscard]] std::string GetExtensionProperty(const std::string& extension,const std::string& name,const std::string& fallback="") const {auto found=m_extensionProperties.find(extension+":"+name);return found==m_extensionProperties.end()?fallback:found->second;}
    bool SetExtensionProperty(const std::string& extension,const std::string& name,const std::string& value){if(extension.empty()||name.empty()||extension.find(':')!=std::string::npos||name.find(':')!=std::string::npos||extension.size()>128||name.size()>128||value.size()>4096)return false;auto key=extension+":"+name;if(!m_extensionProperties.contains(key)&&m_extensionProperties.size()>=1024)return false;m_extensionProperties[key]=value;return true;}
    [[nodiscard]] const std::map<std::string,std::string>& GetExtensionProperties()const{return m_extensionProperties;}


    bool SetParent(Entity child, Entity parent, bool worldPositionStays = true);
    bool RemoveParent(Entity child, bool worldPositionStays = true);
    [[nodiscard]] Entity GetParent(Entity child);
    [[nodiscard]] std::vector<Entity> GetChildren(Entity parent);
    [[nodiscard]] bool IsAncestor(Entity ancestor, Entity descendant) const;
    [[nodiscard]] Mat4 GetWorldMatrix(Entity entity) const;
    [[nodiscard]] Transform GetWorldTransform(Entity entity) const;
    bool SetWorldTransform(Entity entity, const Transform& transform);
    Entity InstantiateModel(const ModelAsset& model, Entity parent = {},
                            std::string name = {});
    Entity InstantiateModel(UUID modelAsset, Entity parent = {}, std::string name = {});

    template<typename ComponentType>
    [[nodiscard]] const ComponentType* TryGetInheritedComponent(Entity entity) const {
        static_assert(std::is_base_of_v<Component, ComponentType>,
                      "ComponentType must derive from Bazzalt::Component");
        if (!Owns(entity)) return nullptr;
        if (const auto* local = entity.TryGetComponent<ComponentType>()) return local;
        if constexpr (ComponentInheritance<ComponentType>::Mode !=
                      ComponentInheritanceMode::NearestAncestor) return nullptr;
        Entity current = entity;
        while (current && !current.GetUUID().IsRoot()) {
            const auto found = m_uuidLookup.find(current.GetComponent<Hierarchy>().Parent);
            if (found == m_uuidLookup.end() || !m_registry.valid(found->second)) return nullptr;
            current = Entity(found->second, const_cast<entt::registry&>(m_registry),
                             const_cast<Scene&>(*this));
            if (const auto* inherited = current.TryGetComponent<ComponentType>()) return inherited;
        }
        return nullptr;
    }

    [[nodiscard]] SceneRay ScreenPointToRay(Entity camera, Vec2 screenPoint,
                                            Vec2 presentationSize) const;
    [[nodiscard]] SceneQueryHit Pick(Entity camera, Vec2 screenPoint,
                                     Vec2 presentationSize,
                                     const SceneQueryOptions& options = {});
    [[nodiscard]] std::vector<SceneQueryHit> PickAll(
        Entity camera, Vec2 screenPoint, Vec2 presentationSize,
        const SceneQueryOptions& options = {});
    [[nodiscard]] SceneQueryHit Raycast(const SceneRay& ray,
                                        const SceneQueryOptions& options = {});
    [[nodiscard]] std::vector<SceneQueryHit> RaycastAll(
        const SceneRay& ray, const SceneQueryOptions& options = {});
    [[nodiscard]] std::vector<Entity> OverlapSphere(
        Vec3 center, float radius, const SceneQueryOptions& options = {});
    [[nodiscard]] std::vector<Entity> OverlapBox(
        Vec3 center, Vec3 extents, const SceneQueryOptions& options = {});

    template<typename SystemType, typename... Args>
    SystemType& AddSystem(Args&&... args) {
        RetainGameplayModule();
        static_assert(std::is_base_of_v<System, SystemType>,
                      "SystemType must derive from Bazzalt::System");
        if (m_isUpdating)
            throw std::logic_error("Systems cannot be added during Scene::Update");
        const auto& type = SystemKey<SystemType>();
        auto existing = m_systemLookup.find(type);
        if (existing != m_systemLookup.end()) {
            return static_cast<SystemType&>(*existing->second);
        }

        auto system = std::make_unique<SystemType>(std::forward<Args>(args)...);
        auto* result = system.get();
        m_systemLookup.emplace(type, result);
        m_systems.emplace_back(std::move(system));
        try {
            static_cast<System*>(result)->OnCreate(*this);
        } catch (...) {
            m_systemLookup.erase(type);
            m_systems.pop_back();
            throw;
        }
        return *result;
    }

    template<typename SystemType>
    [[nodiscard]] bool HasSystem() const {
        return m_systemLookup.find(SystemKey<SystemType>()) != m_systemLookup.end();
    }

    template<typename SystemType>
    SystemType& GetSystem() {
        return static_cast<SystemType&>(*m_systemLookup.at(SystemKey<SystemType>()));
    }

    template<typename SystemType>
    const SystemType& GetSystem() const {
        return static_cast<const SystemType&>(*m_systemLookup.at(SystemKey<SystemType>()));
    }

    template<typename SystemType>
    bool RemoveSystem() {
        if (m_isUpdating)
            throw std::logic_error("Systems cannot be removed during Scene::Update");
        const auto& type = SystemKey<SystemType>();
        auto found = m_systemLookup.find(type);
        if (found == m_systemLookup.end()) {
            return false;
        }

        System* target = found->second;
        m_systemLookup.erase(found);
        auto iterator = std::find_if(m_systems.begin(), m_systems.end(),
            [target](const std::unique_ptr<System>& system) { return system.get() == target; });
        std::unique_ptr<System> system = std::move(*iterator);
        m_systems.erase(iterator);
        system->OnDestroy(*this);
        return true;
    }

    [[nodiscard]] entt::registry& GetRegistry() { RetainGameplayModule(); return m_registry; }
    [[nodiscard]] const entt::registry& GetRegistry() const { return m_registry; }

private:
    friend void Detail::RetainGameplayModule(Scene*);
    void RetainGameplayModule();
    friend class PhysicsScene;
    std::shared_ptr<void> m_lifetime=std::make_shared<int>(0);
    SceneEnvironment m_environment{};
    PhysicsSettings m_physicsSettings{};
    std::map<std::string,std::string> m_extensionProperties;
    friend class SceneSerializer;
    friend class Runtime::Engine;

    template<typename SystemType>
    static const std::string& SystemKey() {
        // Hidden RTTI objects can have different addresses across ELF modules.
        // Own and compare the type name instead; cache it per instantiation.
        static const std::string key(typeid(SystemType).name());
        return key;
    }

    template<typename SystemType>
    bool UpdateSystem(float deltaTime = 0.0f) {
        auto found = m_systemLookup.find(SystemKey<SystemType>());
        if (found == m_systemLookup.end() || !found->second->IsEnabled()) return false;
        found->second->OnUpdate(*this, deltaTime);
        return true;
    }

    void Update(float deltaTime);
    void FixedUpdate(float deltaTime);
    void LateUpdate(float deltaTime);
    Entity CreateRootEntity();
    void DestroySystems();
    void DestroyEntityRecursive(Entity entity);
    void DetachFromParent(Entity entity);
    [[nodiscard]] bool Owns(Entity entity) const;

    // Declared before registry: released after systems and component pools die.
    std::vector<std::shared_ptr<void>> m_gameplayModules;
    entt::registry m_registry;
    UUID m_uuid = UUID::Generate();
    std::unordered_map<UUID, entt::entity> m_uuidLookup;
    std::vector<std::unique_ptr<System>> m_systems;
    std::unordered_map<std::string, System*> m_systemLookup;
    bool m_isUpdating = false;
};

} // namespace Bazzalt
