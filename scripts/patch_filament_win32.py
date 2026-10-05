"""Apply an exact, idempotent compatibility patch to pinned Filament v1.77.0."""
import argparse
import re
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
    if text.count(NEW)==1 and OLD not in text:patched=text
    elif text.count(OLD)==1:patched=text.replace(OLD,NEW)
    else:raise ValueError("Pinned Filament Vulkan handle patch context changed; review upstream before building")
    helper=Path(source)/"filament/backend/src/vulkan/utils/Helper.h"
    original=helper.read_text(encoding="utf-8")
    # All three enumerate overloads accept Vulkan API function pointers. Win32
    # VKAPI_PTR is __stdcall, not the compiler's default __cdecl convention.
    pattern=r"VkResult\s*\(\s*\*"
    already=r"VkResult\s*\(\s*VKAPI_PTR\s*\*"
    plain=len(re.findall(pattern,original));fixed=len(re.findall(already,original))
    if plain+fixed!=3:raise ValueError("Pinned Filament Vulkan enumerate patch context changed; expected three function pointers")
    helper_patched=re.sub(pattern,"VkResult (VKAPI_PTR *",original)
    # Validate both inputs before changing either source file.
    changed=False
    for filename,before,after in ((path,text,patched),(helper,original,helper_patched)):
        if before!=after:filename.write_text(after,encoding="utf-8",newline="\n");changed=True
    return changed

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("source",type=Path)
    args=parser.parse_args();print("Applied Win32 Vulkan handle patch" if Patch(args.source) else "Win32 Vulkan handle patch already applied")
