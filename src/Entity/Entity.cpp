#include "Bazzalt/Entity.h"

#include "Bazzalt/Components/Identity.h"
#include "Bazzalt/Scene.h"

namespace Bazzalt {

UUID Entity::GetUUID() const {
    const Identity* identity = TryGetComponent<Identity>();
    return identity != nullptr ? identity->Value : UUID{};
}

Entity Entity::GetParent() const {
    return m_scene != nullptr ? m_scene->GetParent(*this) : Entity{};
}

bool Entity::HasParent() const {
    return static_cast<bool>(GetParent());
}

std::vector<Entity> Entity::GetChildren() const {
    return m_scene != nullptr ? m_scene->GetChildren(*this) : std::vector<Entity>{};
}

bool Entity::IsAncestorOf(Entity entity) const {
    return m_scene != nullptr && m_scene->IsAncestor(*this, entity);
}

Mat4 Entity::GetWorldMatrix() const {
    return m_scene != nullptr ? m_scene->GetWorldMatrix(*this) : Mat4::Identity();
}

Transform Entity::GetWorldTransform() const {
    return m_scene != nullptr ? m_scene->GetWorldTransform(*this) : Transform{};
}

bool Entity::SetWorldTransform(const Transform& transform) const {
    return m_scene != nullptr && m_scene->SetWorldTransform(*this, transform);
}

bool Entity::SetParent(Entity parent, bool worldPositionStays) const {
    return m_scene != nullptr && m_scene->SetParent(*this, parent, worldPositionStays);
}

bool Entity::AddChild(Entity child, bool worldPositionStays) const {
    return m_scene != nullptr && m_scene->SetParent(child, *this, worldPositionStays);
}

bool Entity::RemoveParent(bool worldPositionStays) const {
    return m_scene != nullptr && m_scene->RemoveParent(*this, worldPositionStays);
}

} // namespace Bazzalt
