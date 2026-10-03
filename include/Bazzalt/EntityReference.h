#pragma once
#include "Bazzalt/UUID.h"
#include "Bazzalt/Components/Transform.h"
#include "Bazzalt/Scene.h"
#include "Bazzalt/SceneManager.h"

namespace Bazzalt {
namespace Detail {
struct EntityServices {
    bool (*GetTransform)(UUID, Transform*);
    bool (*SetTransform)(UUID, const Transform*);
};
inline EntityServices* BoundEntityServices = nullptr;
}
// Stable, non-owning gameplay reference. Resolves against the active scene.
class EntityReference final {
public:
    EntityReference() = default;
    explicit EntityReference(UUID id) : m_id(id) {}
    [[nodiscard]] UUID GetUUID() const { return m_id; }
    [[nodiscard]] Entity Resolve() const {
        auto* scene = SceneManager::GetActiveScene();
        return m_id && scene ? scene->GetEntity(m_id) : Entity{};
    }
    [[nodiscard]] bool IsValid() const { Transform value; return TryGetWorldTransform(value); }
    [[nodiscard]] bool TryGetWorldTransform(Transform& value) const {
        auto entity = Resolve();
        if (!entity) return false;
        value = entity.GetWorldTransform();
        return true;
    }
    bool SetWorldTransform(const Transform& value) const {
        auto entity = Resolve();
        return entity && entity.SetWorldTransform(value);
    }
private:
    UUID m_id{};
};
}
