#pragma once
#include "Bazzalt/Export.h"
#include <memory>
namespace Bazzalt::Runtime {
struct JobAccess {
    // Script teardown joins that module's jobs, never unrelated engine IO.
    BAZZALT_API static void WaitForModule(const std::shared_ptr<void>& module);
};
}
