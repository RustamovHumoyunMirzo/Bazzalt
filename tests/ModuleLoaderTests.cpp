#include "Runtime/ModuleLoader.h"
#include "AI/BehaviorModuleApi.h"
#include "Navigation/NavigationModuleApi.h"
#include <cassert>
#include <filesystem>
#include <string>

namespace {
template<class Function> void Reject(Function function, const std::string& text) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find(text) != std::string::npos;
    }
    assert(rejected);
}
}
int main() {
    using namespace Bazzalt::Runtime;
    Reject([] { LoadSiblingModuleSymbol("../bazzalt_ai", "Entry"); }, "identifiers");
    Reject([] { LoadSiblingModuleSymbol("bazzalt_ai", ""); }, "identifiers");
    Reject([] { LoadSiblingModuleSymbol("bazzalt_missing_test_module", "Entry"); }, "sibling");
    Reject([] { LoadSiblingModuleSymbol("bazzalt_ai", "BazzaltMissingTestEntry"); }, "Missing");
    // A failed lookup must leave the real entry usable, including from a different CWD.
    const auto original = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    const auto* api = LoadSiblingModuleApi<BehaviorModuleApi>(
        "bazzalt_ai", "BazzaltGetBehaviorModuleApi", 1);
    assert(api == &BehaviorModule());
    assert(LoadSiblingModuleApi<NavigationModuleApi>(
        "bazzalt_ai", "BazzaltGetNavigationModuleApi", 1) == &NavigationModule());
    Reject([] { LoadSiblingModuleApi<BehaviorModuleApi>(
        "bazzalt_ai", "BazzaltGetBehaviorModuleApi", 999); }, "Incompatible");
    struct WrongLayout { unsigned Version, Size, Padding; };
    Reject([] { LoadSiblingModuleApi<WrongLayout>(
        "bazzalt_ai", "BazzaltGetBehaviorModuleApi", 1); }, "Incompatible");
    std::filesystem::current_path(original);
}
