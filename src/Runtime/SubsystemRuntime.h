#pragma once
#include "Bazzalt/Subsystem.h"
#include <filesystem>
#include <memory>
#include <vector>
namespace Bazzalt::Runtime {
struct SubsystemBinding { UUID Asset;std::filesystem::path Module;int Level=4; };
class SubsystemRuntime final {
public:
    SubsystemRuntime();~SubsystemRuntime();
    void Configure(std::vector<SubsystemBinding> bindings);
    void ReachLevel(int level);
    void Stop() noexcept;
private:
    struct Instance;
    std::vector<SubsystemBinding> m_bindings;
    std::vector<std::unique_ptr<Instance>> m_instances;
    int m_level=-1;
};
}
