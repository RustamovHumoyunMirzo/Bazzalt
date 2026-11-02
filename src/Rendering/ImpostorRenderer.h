#pragma once
#include <memory>
#include <unordered_set>
#include "Rendering/LODVisibility.h"
namespace filament {class View;class Scene;}
namespace Bazzalt {class Scene;}
namespace Bazzalt::Runtime {
class RenderBackend;
class ImpostorRenderer final {
public:
    explicit ImpostorRenderer(RenderBackend&);~ImpostorRenderer();
    void Prepare(Bazzalt::Scene&);
    void BeginView(Bazzalt::Scene&,filament::View*,const std::unordered_set<UUID>& hidden,const std::unordered_set<UUID>& editorHidden,const std::unordered_map<UUID,LODMask>& masks);
    void InvalidateTextures();
    void EndView();bool IsActive(filament::View*)const;
    bool BeginSelectionMask(filament::Scene&,const std::unordered_set<UUID>& selected,const std::unordered_set<UUID>& hovered);
    void EndSelectionMask();
private:struct Impl;std::unique_ptr<Impl> m_impl;
};
}
