#include "Bazzalt/Scene.h"

#include <functional>
#include <stdexcept>

#include "Bazzalt/AssetManager.h"
#include "Bazzalt/Components/Mesh.h"
#include "Bazzalt/Components/ModelInstance.h"
#include "Bazzalt/Components/ModelNode.h"

namespace Bazzalt {

Scene::Scene() {
    RetainGameplayModule();
    CreateRootEntity();
}

void Scene::RetainGameplayModule() {
    auto module = Detail::GetCurrentGameplayModule();
    if (module && std::find(m_gameplayModules.begin(), m_gameplayModules.end(), module) == m_gameplayModules.end())
        m_gameplayModules.push_back(std::move(module));
}

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
    if (uuid.IsRoot() || m_uuidLookup.find(uuid) != m_uuidLookup.end()) {
        throw std::invalid_argument("Entity UUID must be non-zero and unique within the scene");
    }
    const entt::entity handle = m_registry.create();
    Entity entity(handle, m_registry, *this);
    entity.AddComponent<Identity>(uuid);
    entity.AddComponent<Hierarchy>();
    entity.AddComponent<Transform>();
    entity.AddComponent<Name>(std::move(name));
    m_uuidLookup.emplace(uuid, handle);
    SetParent(entity, GetRootEntity());
    return entity;
}

void Scene::DestroyEntity(Entity entity) {
    if (Owns(entity) && !entity.GetUUID().IsRoot()) DestroyEntityRecursive(entity);
}

void Scene::Clear() {
    m_environment={};
    m_registry.clear();
    m_uuidLookup.clear();
    CreateRootEntity();
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

Entity Scene::GetRootEntity() {
    return GetEntity(UUID::Root());
}

std::size_t Scene::GetEntityCount() const {
    const auto* entities = m_registry.storage<entt::entity>();
    return entities != nullptr ? entities->in_use() : 0u;
}

bool Scene::SetParent(Entity child, Entity parent, bool worldPositionStays) {
    if (!Owns(child) || !Owns(parent) || child.GetUUID().IsRoot() || child == parent || IsAncestor(child, parent)) return false;
    const UUID childUuid = child.GetUUID();
    const UUID parentUuid = parent.GetUUID();
    auto& childHierarchy = child.GetComponent<Hierarchy>();
    if (childHierarchy.Parent == parentUuid) {
        auto& existingChildren = parent.GetComponent<Hierarchy>().Children;
        if (std::find(existingChildren.begin(), existingChildren.end(), childUuid) ==
            existingChildren.end()) existingChildren.push_back(childUuid);
        return true;
    }

    const Mat4 world = worldPositionStays ? GetWorldMatrix(child) : Mat4::Identity();
    Mat4 inverseParent;
    if (worldPositionStays && !GetWorldMatrix(parent).TryInverse(inverseParent)) return false;
    Transform preservedLocal;
    if (worldPositionStays && !(inverseParent * world).Decompose(
            preservedLocal.Position, preservedLocal.Rotation, preservedLocal.Scale)) return false;

    DetachFromParent(child);
    childHierarchy.Parent = parentUuid;
    auto& children = parent.GetComponent<Hierarchy>().Children;
    if (std::find(children.begin(), children.end(), childUuid) == children.end()) children.push_back(childUuid);
    if (worldPositionStays) child.GetComponent<Transform>() = preservedLocal;
    return true;
}

bool Scene::RemoveParent(Entity child, bool worldPositionStays) {
    if (!Owns(child) || child.GetUUID().IsRoot()) return false;
    return SetParent(child, GetRootEntity(), worldPositionStays);
}

Entity Scene::GetParent(Entity child) {
    if (!Owns(child) || child.GetUUID().IsRoot()) return {};
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
    while (current && !current.GetUUID().IsRoot()) {
        const auto& hierarchy = current.GetComponent<Hierarchy>();
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
    Entity current = entity;
    while (!current.GetUUID().IsRoot()) {
        const UUID parentUuid = current.GetComponent<Hierarchy>().Parent;
        const auto found = m_uuidLookup.find(parentUuid);
        if (found == m_uuidLookup.end() || !m_registry.valid(found->second)) break;
        Entity parent(found->second, const_cast<entt::registry&>(m_registry), const_cast<Scene&>(*this));
        world = parent.GetComponent<Transform>().GetMatrix() * world;
        current = parent;
    }
    return world;
}

Transform Scene::GetWorldTransform(Entity entity) const {
    Transform result;
    if (!Owns(entity) || !GetWorldMatrix(entity).Decompose(
            result.Position, result.Rotation, result.Scale)) return result;
    return result;
}

bool Scene::SetWorldTransform(Entity entity, const Transform& transform) {
    if (!Owns(entity) || entity.GetUUID().IsRoot()) return false;
    Mat4 local = transform.GetMatrix();
    const Entity parent = GetParent(entity);
    if (parent) {
        Mat4 inverseParent;
        if (!GetWorldMatrix(parent).TryInverse(inverseParent)) return false;
        local = inverseParent * local;
    }
    Transform value;
    if (!local.Decompose(value.Position, value.Rotation, value.Scale)) return false;
    entity.GetComponent<Transform>() = value;
    return true;
}

Entity Scene::InstantiateModel(const ModelAsset& model, Entity parent, std::string name) {
    if (!model.Id || model.Nodes.empty() || model.Roots.empty())
        throw std::invalid_argument("Model asset must have an id, nodes, and roots");
    if (parent && !Owns(parent))
        throw std::invalid_argument("Model parent must belong to this scene");
    if (!parent) parent = GetRootEntity();

    std::vector<std::uint32_t> parentCount(model.Nodes.size(), 0);
    for (const auto& node : model.Nodes) for (const auto child : node.Children) {
        if (child >= model.Nodes.size() || ++parentCount[child] > 1)
            throw std::invalid_argument("Model hierarchy contains an invalid or shared child");
    }
    std::vector<bool> visiting(model.Nodes.size()), visited(model.Nodes.size());
    std::function<void(std::uint32_t)> validate = [&](std::uint32_t index) {
        if (index >= model.Nodes.size() || visiting[index])
            throw std::invalid_argument("Model hierarchy contains a cycle or invalid root");
        if (visited[index]) return;
        visiting[index] = true;
        for (const auto child : model.Nodes[index].Children) validate(child);
        visiting[index] = false; visited[index] = true;
    };
    for (const auto root : model.Roots) validate(root);
    if (std::find(visited.begin(), visited.end(), false) != visited.end())
        throw std::invalid_argument("Every model node must be reachable from a model root");

    Entity instance = CreateEntity(name.empty() ? model.Name : std::move(name));
    instance.AddComponent<ModelInstance>().ModelAsset = model.Id;
    instance.SetParent(parent, false);
    std::vector<Entity> entities;
    entities.reserve(model.Nodes.size());
    try {
        for (const auto& node : model.Nodes) {
            Entity entity = CreateEntity(node.Name);
            auto& metadata = entity.AddComponent<ModelNode>();
            metadata.ModelAsset = model.Id;
            metadata.SourceIndex = node.SourceIndex;
            metadata.MeshIndex = node.MeshIndex;
            metadata.StablePath = node.StablePath;
            metadata.HasMesh = node.HasMesh();
            auto& transform = entity.GetComponent<Transform>();
            transform.Position = node.Position;
            transform.Rotation = node.Rotation;
            transform.Scale = node.Scale;
            if (node.HasMesh()) {
                auto& mesh = entity.AddComponent<Mesh>();
                mesh.MeshAsset = model.Id;
                mesh.ModelNodeIndex = node.SourceIndex;
                // Zero slot overrides retain the model's original PBR material.
                mesh.Materials.resize(node.MaterialNames.size());
            }
            entities.push_back(entity);
        }
        for (std::size_t index = 0; index < model.Nodes.size(); ++index)
            for (const auto child : model.Nodes[index].Children)
                if (!entities[child].SetParent(entities[index], false))
                    throw std::logic_error("Could not construct model hierarchy");
        for (const auto root : model.Roots)
            if (!entities[root].SetParent(instance, false))
                throw std::logic_error("Could not attach model root node");
        return instance;
    } catch (...) {
        DestroyEntity(instance);
        for (const Entity entity : entities) if (entity) DestroyEntity(entity);
        throw;
    }
}

Entity Scene::InstantiateModel(UUID modelAsset, Entity parent, std::string name) {
    const auto model = AssetManager::LoadModel(modelAsset);
    if (!model) throw std::runtime_error("Model asset is missing, not ready, or invalid");
    return InstantiateModel(*model, parent, std::move(name));
}

void Scene::Update(float deltaTime) {
    struct UpdateGuard {
        bool& Flag;
        explicit UpdateGuard(bool& flag) : Flag(flag) { Flag = true; }
        ~UpdateGuard() { Flag = false; }
    } guard(m_isUpdating);
    for (const auto& system : m_systems) {
        if (system->IsEnabled()) system->OnUpdate(*this, deltaTime);
    }
}

void Scene::FixedUpdate(float deltaTime) {
    struct UpdateGuard {
        bool& Flag;
        explicit UpdateGuard(bool& flag) : Flag(flag) { Flag = true; }
        ~UpdateGuard() { Flag = false; }
    } guard(m_isUpdating);
    for (const auto& system : m_systems) {
        if (system->IsEnabled()) system->OnFixedUpdate(*this, deltaTime);
    }
}

void Scene::DestroySystems() {
    for (auto it = m_systems.rbegin(); it != m_systems.rend(); ++it) {
        try {
            (*it)->OnDestroy(*this);
        } catch (...) {
            // Destructors must not allow user system callbacks to terminate the host.
        }
    }
    m_systemLookup.clear();
    m_systems.clear();
}

void Scene::DestroyEntityRecursive(Entity entity) {
    const std::vector<Entity> children = GetChildren(entity);
    for (Entity child : children) DestroyEntityRecursive(child);
    DetachFromParent(entity);
    m_uuidLookup.erase(entity.GetUUID());
    m_registry.destroy(entity.m_handle);
}

void Scene::DetachFromParent(Entity entity) {
    auto& childHierarchy = entity.GetComponent<Hierarchy>();
    Entity parent = GetEntity(childHierarchy.Parent);
    if (parent && parent != entity) {
        auto& children = parent.GetComponent<Hierarchy>().Children;
        const UUID childUuid = entity.GetUUID();
        children.erase(std::remove(children.begin(), children.end(), childUuid), children.end());
    }
    childHierarchy.Parent = UUID::Root();
}

Entity Scene::CreateRootEntity() {
    const entt::entity handle = m_registry.create();
    Entity root(handle, m_registry, *this);
    root.AddComponent<Identity>(UUID::Root());
    root.AddComponent<Hierarchy>();
    root.AddComponent<Transform>();
    root.AddComponent<Name>("Root");
    m_uuidLookup.emplace(UUID::Root(), handle);
    return root;
}

bool Scene::Owns(Entity entity) const {
    return entity.m_scene == this && entity.m_registry == &m_registry && entity.IsValid();
}

} // namespace Bazzalt
