# ADR-0027: Submit sync contract — explicit `RHISubmitInfo`; binary semaphores supplied by Renderer only at the boundary submit

**Status:** Accepted (implemented in Stage 2 §6.b). Raised by the Architect review during Stage 2 planning.

## Context

Stage 1's `Submit(handle)` discovers which swapchain's binary semaphores to thread by reading a side channel: `FVulkanCommandList::BoundSwapchain()` / `BoundImageIndex()`, members set inside `TransitionToRenderTarget`. `Submit` then waits `image_available` and signals `render_finished_per_image[idx]` and errors with "no bound swapchain" if the channel was never set.

Stage 2 breaks this three ways at once:
1. `ResourceBarrier` replaces `TransitionTo*` ([ADR-0022](0022-resource-barrier-replaces-transition.md)), deleting the only carrier of "which swapchain/image this submit targets."
2. The RenderGraph submits interior passes that touch no swapchain at all — "no bound swapchain" is now a normal case, not an error.
3. Timeline-semaphore multi-frame ([ADR-0020](0020-timeline-semaphores-primary-sync.md)) changes what `Submit` must signal (a timeline value), independent of the binary swapchain semaphores.

A subtle layering fact makes this load-bearing: `ResourceBarrier` records a layout transition *into the command buffer*; it never touches `vkQueueSubmit`'s `pWaitSemaphores`/`pSignalSemaphores`. Unifying barriers via a borrowed swapchain texture does **not** re-home the submit-time semaphore handshake. So "keep `Submit`'s signature and let the borrowed texture carry it" is not a real option — the two live in different layers.

## Decision

`Submit` takes an explicit sync parameter:

```
EngineResult Submit(RHICommandListHandle, const RHISubmitInfo& sync);
struct RHISubmitInfo {
    EngineSpan<const RHISemaphore> wait;     // binary; empty for interior passes
    EngineSpan<const RHISemaphore> signal;   // binary; empty for interior passes
    uint64                         timeline_signal_value;
};
```

The **Renderer** — which owns acquire/present ([ADR-0026](0026-swapchain-recreation-and-resize.md)) — populates `wait`/`signal` with the binary `image_available` / `render_finished` **only on the boundary submit** that writes the swapchain. It obtains them through device accessors added in §6.b: `AcquireNextSwapchainImage` returns `RHIAcquiredImage { uint32 image_index; RHISemaphore image_available; }`, and `GetRenderFinishedSemaphore(swapchain, image_index)`. Interior pass submits pass empty binary spans and a timeline value only.

This contract lands in §6.b, *before* §6.c removes `TransitionTo*`, so there is never a window where `Submit` has lost its side channel but not yet gained the parameter.

## Consequences

**Positive:**
- `Submit` is genuinely decoupled from the swapchain — interior passes submit with no swapchain knowledge, the side channel is gone (not revived under another name).
- Coherent ownership: the layer that owns present (Renderer) supplies present sync; the RHI neither tracks nor guesses.
- This is the shape D3D12 (`ExecuteCommandLists` + fence signal) and Metal (`MTLCommandBuffer` + shared event) want.

**Negative:**
- Widens the RHI surface that §6.b otherwise keeps internal — `RHISemaphore`, `RHISubmitInfo`, and two device accessors become public.
- Callers must thread the binary semaphores correctly to exactly one submit; getting it wrong is a validation error (`VUID-vkQueueSubmit-*`), caught by gates G7/G8.

## Alternatives considered

- **Keep `Submit(handle)`; have the command list record "I wrote swapchain-backed external texture X", Submit reads it back** — rejected. This is `BoundSwapchain` reborn: `Submit` stays coupled to the swapchain and the "decoupled" claim is false.
- **Device tracks the boundary submit implicitly (first submit that writes a swapchain texture)** — rejected. Implicit state in the emit-only RHI is the smart-RHI failure mode ([ADR-0004](0004-render-graph-emits-barriers.md)); fragile under multi-pass ordering.

## References

- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanDevice.cpp` — `Submit` (Stage 1 side-channel reads), `AcquireNextSwapchainImage`, `Present`
- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanCommandList.h` — `BoundSwapchain()` (removed with `TransitionTo*`)
- `Engine/Source/Runtime/RHI/Public/RHI/IRHIDevice.h` — `RHISubmitInfo`, `RHIAcquiredImage`, `GetRenderFinishedSemaphore`
- [ADR-0020](0020-timeline-semaphores-primary-sync.md), [ADR-0022](0022-resource-barrier-replaces-transition.md), [ADR-0026](0026-swapchain-recreation-and-resize.md)
- Stage2.md §6.b, gates G7/G8
