# ADR-0021: Handle generation counter + deferred-delete queue (handle widened 32→64-bit)

**Status:** Accepted (implemented in Stage 2 §6.a/§6.b). Promotes the Stage 2 clause of [ADR-0003](0003-rhi-handle-model.md).

## Context

[ADR-0003](0003-rhi-handle-model.md) chose typed `RHIHandle<Tag>` with a bare `uint32 index` for Stage 1 and explicitly deferred a generation counter to Stage 2: "Stage 2 will add a generation counter alongside the index when deferred-delete + multi-frame-in-flight arrive — the public handle type widens from 32 to 64 bits." Stage 1 got away with a Debug-only `ESlotState` because it had 5–10 program-lifetime resources and called `vkDeviceWaitIdle` before every destroy.

Stage 2 breaks both assumptions: resources are created/destroyed mid-run (offscreen targets on resize), and there is no per-frame `vkDeviceWaitIdle` ([ADR-0020](0020-timeline-semaphores-primary-sync.md)). A resource the CPU "destroys" may still be referenced by command buffers in flight. Two problems follow: (1) immediate destruction is a use-after-free on the GPU; (2) a slot freed and re-issued to a new resource makes a stale handle silently alias a live one.

## Decision

**Widen the handle** to 64 bits: `RHIHandle<Tag> { uint32 index; uint32 generation; }`. `TResourcePool` stores a per-slot generation; `Insert` returns `{index, generation}`; `Get`/`Remove` assert the handle's generation equals the slot's; `Remove` increments the slot generation. A stale handle (generation mismatch) is a Debug FATAL with a clear log line, not a silent alias. Index 0 stays the invalid sentinel.

**Deferred-delete queue:** `Destroy(handle)` does not free immediately. It enqueues the backend payload stamped with the current `frame_value_`. Each frame, entries whose stamp the GPU timeline has passed are reclaimed. This removes the "caller must `WaitIdle` before destroy" precondition the Stage 1 `IRHIDevice.h` comment documented.

Slicing: the generation counter (§6.a) lands first and is testable on its own (destroy + re-allocate + reject stale handle). The deferred-delete queue (§6.b) lands with the timeline, because before the timeline exists the Stage 1 loop still `WaitIdle`s and there is nothing to drain against.

## Consequences

**Positive:**
- Use-after-destroy is a deterministic Debug FATAL, not GPU corruption.
- Mid-run destroy is safe with no global stall (resize recreation depends on this).
- The 64-bit handle still fits in a register, stays trivially copyable and ABI-safe, and keeps the index for future bindless indexing ([Architecture §4](../Architecture.md)).

**Negative:**
- Handle size doubles (8→16 bytes incl. padding); negligible for handles passed by value.
- A long-lived leak of a never-drained payload is possible if the timeline never advances; bounded by the frame loop always advancing `frame_value_`.

## Alternatives considered

- **Keep 32-bit + Debug `ESlotState` (Stage 1 model)** — rejected. No mid-run safety; `ESlotState` does not survive slot reuse and `WaitIdle`-before-destroy is incompatible with multi-frame.
- **Reference-counted RHI resources** — rejected by [ADR-0003](0003-rhi-handle-model.md): breaks across the C ABI and bindless indexing.
- **Generation in a side table, handle stays 32-bit** — rejected. An extra indirection per `Get` for no ABI benefit; the 64-bit handle is simpler.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/RHITypes.h` — `RHIHandle<Tag>` (widened)
- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanResourcePool.h` — per-slot generation + reclamation
- [ADR-0003](0003-rhi-handle-model.md), [ADR-0020](0020-timeline-semaphores-primary-sync.md), [ADR-0026](0026-swapchain-recreation-and-resize.md)
- Stage2.md §6.a/§6.b, gates G9
