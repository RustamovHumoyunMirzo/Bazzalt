#pragma once
#include "Bazzalt/Export.h"
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Bazzalt::Runtime {
// Private modules use matching core/toolchain builds. Successful loads remain
// resident because tables, serializers and opaque handles can outlive a scene.
BAZZALT_API void* LoadSiblingModuleSymbol(const std::string& library, const std::string& symbol);

template<class Api>
const Api* LoadSiblingModuleApi(const std::string& library, const std::string& symbol,
                              std::uint32_t version) {
    using Entry = const Api* (*)(std::uint32_t, std::uint32_t);
    const auto entry = reinterpret_cast<Entry>(LoadSiblingModuleSymbol(library, symbol));
    const auto* api = entry(version, sizeof(Api));
    if (!api || api->Version != version || api->Size != sizeof(Api))
        throw std::runtime_error("Incompatible " + library + " interface " + symbol +
                                 ". Install modules from the same engine build.");
    return api;
}
}
