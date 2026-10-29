#pragma once
#include <cstdint>
namespace Bazzalt::Runtime {
void* CreateMacSurface(std::uintptr_t view,std::uint32_t width,std::uint32_t height,float ratio);
void* MacSurfaceLayer(void* surface);
void ResizeMacSurface(void* surface,std::uint32_t width,std::uint32_t height,float ratio);
void DestroyMacSurface(void* surface);
}
