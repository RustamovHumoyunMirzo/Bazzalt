#pragma once
#include "Bazzalt/Serialization.h"
#include "Bazzalt/Physics.h"
namespace Bazzalt::Runtime {
BAZZALT_API PropertyMap WritePhysicsSettings(const PhysicsSettings&);
BAZZALT_API bool ReadPhysicsSettings(PhysicsSettings&,const PropertyMap&);
void RegisterPhysicsComponents(ComponentSerializationRegistry&);
}
