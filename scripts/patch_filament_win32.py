"""Apply an exact, idempotent compatibility patch to pinned Filament v1.77.0."""
import argparse
from pathlib import Path

OLD="""            context.getDebugUtils().setName(VK_OBJECT_TYPE_SHADER_MODULE,
                    reinterpret_cast<uint64_t>(module), name.c_str());"""
NEW="""            // Non-dispatchable Vulkan handles are integers on 32-bit targets.
#if VK_USE_64_BIT_PTR_DEFINES
            uint64_t const moduleHandle = reinterpret_cast<uint64_t>(module);
#else
            uint64_t const moduleHandle = static_cast<uint64_t>(module);
#endif
            context.getDebugUtils().setName(VK_OBJECT_TYPE_SHADER_MODULE,
                    moduleHandle, name.c_str());"""

def Patch(source):
    path=Path(source)/"filament/backend/src/vulkan/VulkanAsyncHandles.cpp"
    text=path.read_text(encoding="utf-8")
    if text.count(NEW)==1 and OLD not in text:return False
    if text.count(OLD)!=1:raise ValueError("Pinned Filament Vulkan handle patch context changed; review upstream before building")
    path.write_text(text.replace(OLD,NEW),encoding="utf-8",newline="\n")
    return True

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("source",type=Path)
    args=parser.parse_args();print("Applied Win32 Vulkan handle patch" if Patch(args.source) else "Win32 Vulkan handle patch already applied")
