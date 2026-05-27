// VulkanResourcePool.h - generic slot pool for VulkanRHI handles.
//
// Per Architecture.md §3.2:
//   - Stage 1 handles are simple uint32 indices (no generation counter)
//   - Debug builds carry an ESlotState per slot for use-after-destroy detection
//   - Index 0 is reserved as "invalid handle" (matches RHIHandle::valid() check)

#pragma once

#include "VulkanCommon.h"

#include <utility>
#include <vector>

namespace pe::vk {

template <typename TPayload, typename Tag>
class TResourcePool {
public:
    enum class EState : uint8 { Free, Live, Destroyed };

    struct Slot {
        TPayload payload{};
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
                return RHIHandle<Tag>{static_cast<uint32>(i)};
            }
        }
        Slot s;
        s.payload = std::move(value);
        s.state   = EState::Live;
        slots_.push_back(std::move(s));
        return RHIHandle<Tag>{static_cast<uint32>(slots_.size() - 1)};
    }

    TPayload* Get(RHIHandle<Tag> handle) {
        ENGINE_CHECK(handle.valid());
        ENGINE_CHECK(handle.index < slots_.size());
        ENGINE_CHECK(slots_[handle.index].state == EState::Live);
        return &slots_[handle.index].payload;
    }

    TPayload Remove(RHIHandle<Tag> handle) {
        ENGINE_CHECK(handle.valid());
        ENGINE_CHECK(handle.index < slots_.size());
        ENGINE_CHECK(slots_[handle.index].state == EState::Live);
        TPayload taken = std::move(slots_[handle.index].payload);
        slots_[handle.index].payload = TPayload{};
        slots_[handle.index].state   = EState::Destroyed;
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
    std::vector<Slot> slots_;
};

}  // namespace pe::vk
