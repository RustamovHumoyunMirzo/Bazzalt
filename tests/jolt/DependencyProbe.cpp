// This module shares C++ exceptions with core; solver-local flags must not leak.
#if defined(_MSC_VER) && defined(_HAS_EXCEPTIONS) && !_HAS_EXCEPTIONS
#error Bazzalt physics consumers require standard MSVC exception handling
#endif
// Dependency ABI/link check only. No bodies, colliders or simulation are created.
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/Memory.h>

int main() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    return 0;
}
