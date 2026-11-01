#pragma once
#include "Bazzalt/Entity.h"
#include <memory>
#include <optional>
namespace Bazzalt {
class Scene;
namespace Runtime {struct PrefabAccess;struct PrefabDefinition;}
/// Immutable asset snapshot. Creation/apply-to-asset belong to editor tooling.
class BAZZALT_API Prefab final {
public:
    static std::optional<Prefab> Load(UUID asset);
    UUID GetUUID() const;
    std::string GetName() const;
    std::size_t GetEntityCount() const;
    Entity Instantiate(Scene& scene,Entity parent={}) const;
    static Entity GetInstanceRoot(Entity entity);
    static UUID GetSource(Entity entity);
    static bool HasOverrides(Entity entity);
    /// Three-way merge of the latest asset, preserving instance overrides and placement.
    static Entity Refresh(Entity entity);
    /// Restore source values and hierarchy, preserving the root's placement and UUID.
    static Entity Revert(Entity entity);
    /// Remove the link without changing any objects or components.
    static bool Unpack(Entity entity);
private:
    friend struct Runtime::PrefabAccess;
    std::shared_ptr<const Runtime::PrefabDefinition> m_data;
};
}
