// engine/src/renderer/VulkanCommon.hpp  (PRIVATE — never leaves src/)
//
// The one way renderer internals include Vulkan. Vulkan's macros use C-style
// casts, so we relax -Wold-style-cast for renderer TUs at this third-party
// boundary — the same tradeoff VulkanContext.cpp already made.
#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

#include <volk.h>
