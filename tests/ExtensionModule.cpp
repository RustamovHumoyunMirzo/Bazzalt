#include "Bazzalt/Extension.h"
extern "C" double BazzaltExtensionLibraryValue(double);
namespace {
double Double(double value){return BazzaltExtensionLibraryValue(value);}
Bazzalt::Vec3 Offset(Bazzalt::Vec3 value,Bazzalt::Vec3 offset){return value+offset;}
void Register(Bazzalt::ExtensionRegistry& registry){registry.Bind("FixtureMath","Double",Double);registry.Bind("FixtureMath","Offset",Offset);}
}
BAZZALT_EXTENSION(Register)
