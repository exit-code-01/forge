// engine/include/forge/forge.hpp
//
// Umbrella header for engine consumers (sandbox, editor).
// Only ever includes PUBLIC API headers. Engine internals never appear here.

#pragma once

#include "forge/core/Log.hpp"

namespace forge {

struct Version {
    int major, minor, patch;
};

constexpr Version version() { return {0, 1, 0}; }

} // namespace forge
