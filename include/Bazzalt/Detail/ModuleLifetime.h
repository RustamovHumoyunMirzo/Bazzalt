#pragma once
#include <memory>
#include "Bazzalt/Export.h"
namespace Bazzalt { class Scene; }
namespace Bazzalt::Detail {
// Implementation detail: retain native gameplay code while its ECS storage lives.
BAZZALT_API std::shared_ptr<void> GetCurrentGameplayModule();
BAZZALT_API std::shared_ptr<void> SetCurrentGameplayModule(std::shared_ptr<void> module);
BAZZALT_API void RetainGameplayModule(Scene* scene);
}
