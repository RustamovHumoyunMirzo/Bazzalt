#pragma once
#include "Bazzalt/Export.h"
namespace Bazzalt::Detail {
enum class SubsystemService { Scene, Assets, Input, Audio, Rendering };
BAZZALT_API void RequireSubsystemService(SubsystemService service,const char* operation);
}
