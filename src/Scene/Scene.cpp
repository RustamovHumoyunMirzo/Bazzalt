#include "Bazzalt/Scene.h"

namespace Bazzalt {

Scene::~Scene() {
    DestroySystems();
}

Entity Scene::CreateEntity(std::string name) {
    const entt::entity handle = m_registry.create();
    Entity entity(handle, m_registry, *this);
    entity.AddComponent<Transform>();
    entity.AddComponent<Name>(std::move(name));
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (entity.GetScene() == this && entity.IsValid()) {
        m_registry.destroy(entity.m_handle);
    }
}

void Scene::Clear() {
    m_registry.clear();
}

Entity Scene::FindEntityByName(const std::string& name) {
    auto view = m_registry.view<Name>();
    for (const entt::entity handle : view) {
        if (view.get<Name>(handle).Value == name) {
            return Entity(handle, m_registry, *this);
        }
    }
    return {};
}

Entity Scene::GetEntity(Entity::Id id) {
    const auto handle = static_cast<entt::entity>(id);
    return m_registry.valid(handle) ? Entity(handle, m_registry, *this) : Entity{};
}

std::size_t Scene::GetEntityCount() const {
    const auto* entities = m_registry.storage<entt::entity>();
    return entities != nullptr ? entities->in_use() : 0u;
}

void Scene::Update(float deltaTime) {
    for (const auto& system : m_systems) {
        if (system->IsEnabled()) {
            system->OnUpdate(*this, deltaTime);
        }
    }
}

void Scene::DestroySystems() {
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        (*it)->OnDestroy(*this);
    }
    m_systemLookup.clear();
    m_systems.clear();
}

} // namespace Bazzalt
