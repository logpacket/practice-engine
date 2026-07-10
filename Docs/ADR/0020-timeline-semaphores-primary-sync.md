# ADR-0020: Timeline semaphores are the primary CPU↔GPU sync; binary semaphores only for swapchain interop

**Status:** Accepted (implemented in Stage 2 §6.b)

## Context

Stage 1 ran one frame in flight with a single binary `VkFence` (`frame_done`) and binary semaphores for swapchain acquire/present. Stage 2 needs N frames in flight, a deferred-delete queue that reclaims resources once the GPU passes a known point, and pass-to-pass submit ordering inside a RenderGraph frame. Binary fences/semaphores express "signaled / not signaled" — they cannot encode "the GPU has reached point T", which is exactly what frame pacing and deferred-delete need. Stacking more binary fences (one per frame slot, one per resource generation) reproduces a monotonic counter badly.

Vulkan timeline semaphores (`VkSemaphoreTypeKHR = TIMELINE`) carry a monotonically increasing `uint64` value; both CPU and GPU can wait-for and signal arbitrary values. They are **core in Vulkan 1.2+**, so the 1.3 baseline ([ADR-0009](0009-vulkan-1-3-baseline-no-fallback.md)) has them with no extension gating. The one place they cannot be used is the swapchain handshake: `vkAcquireNextImageKHR` and `vkQueuePresentKHR` only accept binary semaphores — the OS presentation engine forces this on us.

## Decision

A single device-owned timeline semaphore (`frame_timeline_`) plus a monotonic `frame_value_` is the primary CPU↔GPU synchronization and frame-pacing primitive. Frame N waits until the timeline reaches `N − MAX_FRAMES_IN_FLIGHT` before reusing that frame slot's command buffers and transient state. The deferred-delete queue keys reclamation on timeline values ([ADR-0021](0021-handle-generation-and-deferred-delete.md)).

Binary semaphores are retained **only** at the swapchain boundary: `image_available` (signaled by acquire, waited by the boundary submit) and `render_finished` (signaled by the boundary submit, waited by present). These flow through the explicit `RHISubmitInfo` contract ([ADR-0027](0027-submit-sync-contract.md)), not a side channel.

## Consequences

**Positive:**
- One counter expresses frame pacing, deferred-delete, and interior-pass ordering — no fence-per-slot bookkeeping.
- No `vkDeviceWaitIdle` in the steady loop; teardown drains by waiting the final timeline value.
- D3D12 (`ID3D12Fence`) and Metal (`MTLSharedEvent`) have direct timeline analogues, so the model ports.

**Negative:**
- Two sync concepts coexist (timeline interior, binary at the swapchain edge); the boundary submit is the one place they meet and must be wired carefully (ADR-0027).
- Off-by-one errors in "wait for value N−k" are silent stalls or races; covered by gate G8 (an outstanding-frame counter must reach `MAX_FRAMES_IN_FLIGHT`).

## Alternatives considered

- **Per-frame-slot binary fences (extend Stage 1)** — rejected. Reconstructs a monotonic counter from many binary objects; deferred-delete still needs "GPU passed point T" which fences express only at fixed slot granularity.
- **Timeline semaphores for the swapchain too** — impossible. `vkAcquireNextImageKHR`/`vkQueuePresentKHR` accept binary semaphores only.

## References

- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanResources.h` — Stage 1 `frame_done` fence + binary semaphores (replaced)
- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanDevice.cpp` — `Submit` / `AcquireNextSwapchainImage` / `Present`
- [ADR-0009](0009-vulkan-1-3-baseline-no-fallback.md), [ADR-0021](0021-handle-generation-and-deferred-delete.md), [ADR-0027](0027-submit-sync-contract.md)
- Stage2.md §6.b, gate G8
