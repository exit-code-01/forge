// engine/include/forge/ecs/Registry.hpp
//
// Sparse-set ECS core (same storage family as EnTT, sized for readability).
//
// Per component type T there is one Pool<T>:
//   sparse : entityIndex -> dense slot (or kTombstone)   [big, holey]
//   dense  : packed entity indices                        [small, contiguous]
//   data   : packed T values, parallel to dense           [what iteration touches]
//
// Why: iterating "every Transform" walks ONE contiguous array — the cache
// behavior that is the entire point of ECS. Removal is O(1) swap-and-pop,
// which is also why iteration order is unspecified and you must never remove
// components from inside an each() over the same pool.
//
// THREADING: none. Single-threaded by design until systems exist (P4+),
// at which point parallelism happens ACROSS systems, not inside the registry.

#pragma once

#include "forge/ecs/Entity.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forge::ecs {

namespace detail {

inline constexpr uint32_t kTombstone = 0xFFFFFFFFu;

class PoolBase {
public:
    virtual ~PoolBase() = default;
    virtual void removeIfPresent(uint32_t entityIndex) = 0;
};

template <typename T> class Pool final : public PoolBase {
public:
    template <typename... Args> T& emplace(uint32_t entityIndex, Args&&... args) {
        assert(!has(entityIndex) && "component already present on entity");
        if (entityIndex >= m_sparse.size()) {
            m_sparse.resize(entityIndex + 1, kTombstone);
        }
        m_sparse[entityIndex] = static_cast<uint32_t>(m_dense.size());
        m_dense.push_back(entityIndex);
        return m_data.emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] bool has(uint32_t entityIndex) const {
        return entityIndex < m_sparse.size() && m_sparse[entityIndex] != kTombstone;
    }

    [[nodiscard]] T& get(uint32_t entityIndex) {
        assert(has(entityIndex));
        return m_data[m_sparse[entityIndex]];
    }

    [[nodiscard]] T* tryGet(uint32_t entityIndex) {
        return has(entityIndex) ? &m_data[m_sparse[entityIndex]] : nullptr;
    }

    void removeIfPresent(uint32_t entityIndex) override {
        if (!has(entityIndex)) {
            return;
        }
        // Swap-and-pop: move the LAST element into the vacated slot so the
        // arrays stay packed. O(1), order not preserved.
        const uint32_t slot = m_sparse[entityIndex];
        const uint32_t lastEntity = m_dense.back();
        m_dense[slot] = lastEntity;
        m_data[slot] = std::move(m_data.back());
        m_sparse[lastEntity] = slot;
        m_dense.pop_back();
        m_data.pop_back();
        m_sparse[entityIndex] = kTombstone;
    }

    [[nodiscard]] size_t size() const { return m_dense.size(); }
    [[nodiscard]] uint32_t entityAt(size_t denseSlot) const { return m_dense[denseSlot]; }
    [[nodiscard]] T& dataAt(size_t denseSlot) { return m_data[denseSlot]; }

private:
    std::vector<uint32_t> m_sparse;
    std::vector<uint32_t> m_dense;
    std::vector<T> m_data;
};

} // namespace detail

class Registry {
public:
    // ---- Entity lifetime ----
    [[nodiscard]] Entity create() {
        if (!m_freeIndices.empty()) {
            const uint32_t index = m_freeIndices.back();
            m_freeIndices.pop_back();
            return {index, m_generations[index]};
        }
        const auto index = static_cast<uint32_t>(m_generations.size());
        m_generations.push_back(0);
        return {index, 0};
    }

    void destroy(Entity entity) {
        assert(alive(entity) && "destroying a dead or stale entity");
        for (auto& [type, pool] : m_pools) {
            pool->removeIfPresent(entity.index);
        }
        ++m_generations[entity.index]; // every outstanding handle is now stale
        m_freeIndices.push_back(entity.index);
    }

    [[nodiscard]] bool alive(Entity entity) const {
        return !entity.isNull() && entity.index < m_generations.size() &&
               m_generations[entity.index] == entity.generation;
    }

    // ---- Components ----
    template <typename T, typename... Args> T& emplace(Entity entity, Args&&... args) {
        assert(alive(entity));
        return pool<T>().emplace(entity.index, std::forward<Args>(args)...);
    }

    template <typename T> void remove(Entity entity) {
        assert(alive(entity));
        pool<T>().removeIfPresent(entity.index);
    }

    template <typename T> [[nodiscard]] bool has(Entity entity) const {
        assert(alive(entity));
        const auto* p = tryPool<T>();
        return p != nullptr && p->has(entity.index);
    }

    template <typename T> [[nodiscard]] T& get(Entity entity) {
        assert(alive(entity));
        return pool<T>().get(entity.index);
    }

    template <typename T> [[nodiscard]] T* tryGet(Entity entity) {
        assert(alive(entity));
        auto* p = tryPoolMutable<T>();
        return p != nullptr ? p->tryGet(entity.index) : nullptr;
    }

    // ---- Iteration ----
    // Single component: walks one packed array. fn(Entity, T&).
    template <typename T, typename Fn> void each(Fn&& fn) {
        auto* p = tryPoolMutable<T>();
        if (p == nullptr) {
            return;
        }
        for (size_t slot = 0; slot < p->size(); ++slot) {
            const uint32_t index = p->entityAt(slot);
            fn(Entity{index, m_generations[index]}, p->dataAt(slot));
        }
    }

    // Two components: walks A's packed array, filters by B.
    // (Choosing the smaller pool to drive is a P4-era optimization; the API
    // won't change when we do it.) fn(Entity, A&, B&).
    template <typename A, typename B, typename Fn> void each(Fn&& fn) {
        auto* pa = tryPoolMutable<A>();
        auto* pb = tryPoolMutable<B>();
        if (pa == nullptr || pb == nullptr) {
            return;
        }
        for (size_t slot = 0; slot < pa->size(); ++slot) {
            const uint32_t index = pa->entityAt(slot);
            if (B* b = pb->tryGet(index)) {
                fn(Entity{index, m_generations[index]}, pa->dataAt(slot), *b);
            }
        }
    }

private:
    template <typename T> detail::Pool<T>& pool() {
        auto& slot = m_pools[std::type_index(typeid(T))];
        if (slot == nullptr) {
            slot = std::make_unique<detail::Pool<T>>();
        }
        return static_cast<detail::Pool<T>&>(*slot);
    }

    template <typename T> [[nodiscard]] const detail::Pool<T>* tryPool() const {
        const auto it = m_pools.find(std::type_index(typeid(T)));
        return it == m_pools.end() ? nullptr
                                   : static_cast<const detail::Pool<T>*>(it->second.get());
    }

    template <typename T> [[nodiscard]] detail::Pool<T>* tryPoolMutable() {
        const auto it = m_pools.find(std::type_index(typeid(T)));
        return it == m_pools.end() ? nullptr : static_cast<detail::Pool<T>*>(it->second.get());
    }

    std::vector<uint32_t> m_generations;
    std::vector<uint32_t> m_freeIndices;
    std::unordered_map<std::type_index, std::unique_ptr<detail::PoolBase>> m_pools;
};

} // namespace forge::ecs
