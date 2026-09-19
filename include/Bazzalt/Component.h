#pragma once

namespace Bazzalt {

// Marker base for components managed by Bazzalt's ECS. It intentionally has
// no virtual functions or state so components remain lightweight data types.
struct Component {};

} // namespace Bazzalt
