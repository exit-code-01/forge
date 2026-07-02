// engine/include/forge/ecs/Entity.hpp
//
// An entity is NOT an object — it's a handle: a slot index plus a generation
// counter. When a slot is destroyed and recycled, its generation bumps, so
// every old handle to it becomes detectably stale instead of silently aliasing
// the new occupant (the classic ABA bug, made impossible by construction).

#pragma once

#include <cstdint>

namespace forge::ecs {

struct Entity {
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool isNull() const { return index == kInvalidIndex; }
    constexpr bool operator==(const Entity&) const = default;
};

inline constexpr Entity kNullEntity{};

} // namespace forge::ecs
