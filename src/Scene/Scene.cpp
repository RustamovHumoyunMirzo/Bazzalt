#include "Bazzalt/Scene.h"

#include <stdexcept>

namespace Bazzalt {

Scene::~Scene() {
    DestroySystems();
}

Entity Scene::CreateEntity(std::string name) {
    UUID uuid;
    do {
        uuid = UUID::Generate();
    } while (m_uuidLookup.find(uuid) != m_uuidLookup.end());
    return CreateEntity(uuid, std::move(name));
}

Entity Scene::CreateEntity(UUID uuid, std::string name) {
    if (!uuid || m_uuidLookup.find(uuid) != m_uuidLookup.end()) {
        throw std::invalid_argument("Entity UUID must be valid and unique within the scene");
    }
    const entt::entity handle = m_registry.create();
    Entity entity(handle, m_registry, *this);
    entity.AddComponent<Identity>(uuid);
    entity.AddComponent<Hierarchy>();
    entity.AddComponent<Transform>();
    entity.AddComponent<Name>(std::move(name));
    m_uuidLookup.emplace(uuid, handle);
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (Owns(entity)) DestroyEntityRecursive(entity);
}

void Scene::Clear() {
    m_registry.clear();
    m_uuidLookup.clear();
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

Entity Scene::GetEntity(UUID uuid) {
    const auto found = m_uuidLookup.find(uuid);
    if (found == m_uuidLookup.end() || !m_registry.valid(found->second)) return {};
    return Entity(found->second, m_registry, *this);
}

std::size_t Scene::GetEntityCount() const {
    const auto* entities = m_registry.storage<entt::entity>();
    return entities != nullptr ? entities->in_use() : 0u;
}

bool Scene::SetParent(Entity child, Entity parent) {
    if (!Owns(child) || !Owns(parent) || child == parent || IsAncestor(child, parent)) return false;
    const UUID childUuid = child.GetUUID();
    const UUID parentUuid = parent.GetUUID();
    auto& childHierarchy = child.GetComponent<Hierarchy>();
    if (childHierarchy.Parent == parentUuid) return true;

    RemoveParent(child);
    childHierarchy.Parent = parentUuid;
    auto& children = parent.GetComponent<Hierarchy>().Children;
    if (std::find(children.begin(), children.end(), childUuid) == children.end()) children.push_back(childUuid);
    return true;
}

bool Scene::RemoveParent(Entity child) {
    if (!Owns(child)) return false;
    auto& childHierarchy = child.GetComponent<Hierarchy>();
    if (!childHierarchy.Parent) return false;
    Entity parent = GetEntity(childHierarchy.Parent);
    if (parent) {
        auto& children = parent.GetComponent<Hierarchy>().Children;
        const UUID childUuid = child.GetUUID();
        children.erase(std::remove(children.begin(), children.end(), childUuid), children.end());
    }
    childHierarchy.Parent = {};
    return true;
}

Entity Scene::GetParent(Entity child) {
    if (!Owns(child)) return {};
    return GetEntity(child.GetComponent<Hierarchy>().Parent);
}

std::vector<Entity> Scene::GetChildren(Entity parent) {
    std::vector<Entity> result;
    if (!Owns(parent)) return result;
    const auto& children = parent.GetComponent<Hierarchy>().Children;
    result.reserve(children.size());
    for (const UUID uuid : children) {
        Entity child = GetEntity(uuid);
        if (child) result.push_back(child);
    }
    return result;
}

bool Scene::IsAncestor(Entity ancestor, Entity descendant) const {
    if (!Owns(ancestor) || !Owns(descendant) || ancestor == descendant) return false;
    const UUID ancestorUuid = ancestor.GetUUID();
    Entity current = descendant;
    while (current) {
        const auto& hierarchy = current.GetComponent<Hierarchy>();
        if (!hierarchy.Parent) return false;
        if (hierarchy.Parent == ancestorUuid) return true;
        auto found = m_uuidLookup.find(hierarchy.Parent);
        if (found == m_uuidLookup.end() || !m_registry.valid(found->second)) return false;
        current = Entity(found->second, const_cast<entt::registry&>(m_registry), const_cast<Scene&>(*this));
    }
    return false;
}

Mat4 Scene::GetWorldMatrix(Entity entity) const {
    if (!Owns(entity)) return Mat4::Identity();

    Mat4 world = entity.GetComponent<Transform>().GetMatrix();
    UUID parentUuid = entity.GetComponent<Hierarchy>().Parent;
    while (parentUuid) {
        const auto found = m_uuidLookup.find(parentUuid);
        if (found == m_uuidLookup.end() || !m_registry.valid(found->second)) break;
        Entity parent(found->second, const_cast<entt::registry&>(m_registry), const_cast<Scene&>(*this));
        world = parent.GetComponent<Transform>().GetMatrix() * world;
        parentUuid = parent.GetComponent<Hierarchy>().Parent;
    }
    return world;
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

void Scene::DestroyEntityRecursive(Entity entity) {
    const std::vector<Entity> children = GetChildren(entity);
    for (Entity child : children) DestroyEntityRecursive(child);
    RemoveParent(entity);
    m_uuidLookup.erase(entity.GetUUID());
    m_registry.destroy(entity.m_handle);
}

bool Scene::Owns(Entity entity) const {
    return entity.m_scene == this && entity.m_registry == &m_registry && entity.IsValid();
}

} // namespace Bazzalt
