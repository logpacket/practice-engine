// VulkanResourcePool.h - generic slot pool for VulkanRHI handles.
//
// Per ADR-0021 (Stage 2 §6.a):
//   - Handles are {index, generation}; each slot carries a generation counter
//   - Insert returns the slot's current generation; Remove bumps it, so any
//     handle held across a Remove is rejected (generation mismatch, Debug FATAL)
//   - Remove marks the slot Destroyed; it never becomes reusable Free in §6.a.
//     Deferred-delete reclamation (§6.b) is what eventually recycles slots.
//   - Index 0 is reserved as "invalid handle" (matches RHIHandle::valid() check)
//
// Deliberately includes no Vulkan headers: the pool is a pure template over
// RHIHandle + payload, and unit tests instantiate it host-side (rhi_pool_smoke).

#pragma once

#include <Core/Assert.h>
#include <Core/Types.h>
#include <RHI/RHITypes.h>

#include <utility>
#include <vector>

namespace pe::vk {

template <typename TPayload, typename Tag>
class TResourcePool {
public:
    enum class EState : uint8 { Free, Live, Destroyed };

    struct Slot {
        TPayload payload{};
        // Generation issued with the slot's current occupant. Starts at 1 so a
        // zero-initialized stale handle {index, 0} can never match.
        uint32   generation = 1;
        EState   state = EState::Free;
    };

    TResourcePool() {
        // Index 0 sentinel - RHIHandle::valid() returns false for index 0.
        slots_.emplace_back();
    }

    RHIHandle<Tag> Insert(TPayload value) {
        // Reuse a Free slot if available; otherwise grow.
        for (size_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].state == EState::Free) {
                slots_[i].payload = std::move(value);
                slots_[i].state   = EState::Live;
                return RHIHandle<Tag>{static_cast<uint32>(i), slots_[i].generation};
            }
        }
        Slot s;
        s.payload = std::move(value);
        s.state   = EState::Live;
        slots_.push_back(std::move(s));
        const auto index = static_cast<uint32>(slots_.size() - 1);
        return RHIHandle<Tag>{index, slots_[index].generation};
    }

    TPayload* Get(RHIHandle<Tag> handle) {
        CheckHandle(handle);
        return &slots_[handle.index].payload;
    }

    TPayload Remove(RHIHandle<Tag> handle) {
        CheckHandle(handle);
        TPayload taken = std::move(slots_[handle.index].payload);
        slots_[handle.index].payload = TPayload{};
        slots_[handle.index].state   = EState::Destroyed;
        // Invalidate every outstanding handle to this slot.
        ++slots_[handle.index].generation;
        return taken;
    }

    template <typename F>
    void ForEachLive(F&& fn) {
        for (size_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].state == EState::Live) {
                fn(slots_[i].payload);
            }
        }
    }

    size_t LiveCount() const {
        size_t n = 0;
        for (size_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].state == EState::Live) { ++n; }
        }
        return n;
    }

private:
    // [[maybe_unused]]: ENGINE_CHECK compiles out in Release.
    void CheckHandle([[maybe_unused]] RHIHandle<Tag> handle) const {
        ENGINE_CHECK(handle.valid());
        ENGINE_CHECK(handle.index < slots_.size());
        // Generation first: a stale handle to a recycled/destroyed slot must
        // report as a generation mismatch (ADR-0021), not a state error.
        ENGINE_CHECK(slots_[handle.index].generation == handle.generation);
        ENGINE_CHECK(slots_[handle.index].state == EState::Live);
    }

    std::vector<Slot> slots_;
};

}  // namespace pe::vk
