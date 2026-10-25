#include <mutex>
#include <utils/compiler.h>
// libstdc++ mutexes have no Clang capability annotations. This must compile
// under the same warning-as-error flags as the pinned x86 Filament build.
struct MutexProbe {
    std::mutex Lock;
    int Value UTILS_GUARDED_BY(Lock) = 0;
};
