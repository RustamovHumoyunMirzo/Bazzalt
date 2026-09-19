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

bool Entity::SetParent(Entity parent) const {
    return m_scene != nullptr && m_scene->SetParent(*this, parent);
}

bool Entity::AddChild(Entity child) const {
    return m_scene != nullptr && m_scene->SetParent(child, *this);
}

bool Entity::RemoveParent() const {
    return m_scene != nullptr && m_scene->RemoveParent(*this);
}

} // namespace Bazzalt
