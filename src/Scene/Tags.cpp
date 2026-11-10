#include "Bazzalt/Scene.h"
namespace Bazzalt {
bool Entity::AddTag(const std::string &tag) const {
    return GetComponent<Tags>().Value.Add(tag);
}
bool Entity::RemoveTag(const std::string &tag) const {
    return GetComponent<Tags>().Value.Remove(tag);
}
void Entity::ClearTags() const {
    GetComponent<Tags>().Value.Clear();
}
TagSet Entity::GetTags() const {
    return GetComponent<Tags>().Value;
}
void Entity::SetTags(const TagSet &tags) const {
    GetComponent<Tags>().Value = tags;
}
bool Entity::HasTag(const std::string &tag) const {
    const auto *tags = TryGetComponent<Tags>();
    return tags && tags->Value.Has(tag);
}
bool Entity::HasAnyTag(const TagSet &tags) const {
    const auto *own = TryGetComponent<Tags>();
    return own && own->Value.HasAny(tags);
}
bool Entity::HasAllTags(const TagSet &tags) const {
    const auto *own = TryGetComponent<Tags>();
    return own && own->Value.HasAll(tags);
}
std::vector<Entity> Scene::FindEntitiesWithAnyTag(const TagSet &tags) {
    std::vector<Entity> result;
    for (auto handle : m_registry.view<Tags>()) {
        auto entity = GetEntity(static_cast<Entity::Id>(handle));
        if (!entity.GetUUID().IsRoot() && entity.HasAnyTag(tags))
            result.push_back(entity);
    }
    std::sort(result.begin(), result.end(), [](Entity a, Entity b) { return a.GetUUID() < b.GetUUID(); });
    return result;
}
std::vector<Entity> Scene::FindEntitiesWithAllTags(const TagSet &tags) {
    std::vector<Entity> result;
    for (auto handle : m_registry.view<Tags>()) {
        auto entity = GetEntity(static_cast<Entity::Id>(handle));
        if (!entity.GetUUID().IsRoot() && entity.HasAllTags(tags))
            result.push_back(entity);
    }
    std::sort(result.begin(), result.end(), [](Entity a, Entity b) { return a.GetUUID() < b.GetUUID(); });
    return result;
}
std::vector<Entity> Scene::FindEntitiesWithTag(const std::string &tag) {
    return FindEntitiesWithAnyTag(TagSet{tag});
}
Entity Scene::FindEntityWithTag(const std::string &tag) {
    auto values = FindEntitiesWithTag(tag);
    return values.empty() ? Entity{} : values.front();
}
} // namespace Bazzalt
