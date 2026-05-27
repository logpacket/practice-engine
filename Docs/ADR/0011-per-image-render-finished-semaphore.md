# ADR-0011: Per-image `render_finished` semaphore

**Status:** Accepted (post-§6.e validation finding)

## Context

The Stage 1 swapchain was first implemented with one `image_available` semaphore and one `render_finished` semaphore per swapchain (not per image). With a single frame in flight this looked sufficient: each frame waited the previous frame's `frame_done` fence, then reused both semaphores for the next acquire / submit / present cycle.

Running `practice-engine` for a few frames against the Vulkan validation layer produced:

```
VUID-vkQueueSubmit-pSignalSemaphores-00067:
  vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] is being signaled by
  VkQueue, but it may still be in use by VkSwapchainKHR.
  Swapchain image N was presented but was not re-acquired, so VkSemaphore
  may still be in use and cannot be safely reused with image index M.
```

The presentation engine holds the signal of the previously-presented image's semaphore until it has finished with that image, which can outlive the current frame's CPU fence. Reusing the same semaphore to signal the *next* image's submit races. The validation layer's suggested fix is "use a separate semaphore per swapchain image, indexed by the acquired image."

## Decision

`VulkanSwapchainPayload` holds:

- One `image_available` semaphore (consumed by submit before reuse; the per-frame fence guards reuse).
- A `std::vector<VkSemaphore> render_finished_per_image`, sized to the swapchain image count, indexed by the acquired image index.

`Submit` looks up `render_finished_per_image[bound_image_index]` (the image index the command list recorded during `TransitionToRenderTarget`) and uses it as the signal semaphore. `Present` uses the same semaphore as the wait.

## Consequences

**Positive:**
- Validation clean across hundreds of frames per second.
- Pattern extends naturally to multiple frames in flight (Stage 2): per-frame `image_available` semaphores join the per-image `render_finished` semaphores.

**Negative:**
- One extra `VkSemaphore` per swapchain image (~16 bytes each). Negligible.
- The command list now records `bound_image_index` during `TransitionToRenderTarget` and exposes it to `Submit` via `wrapper->BoundImageIndex()`. Slight coupling between command list and device that goes away in Stage 2 when `Submit` takes explicit sync arguments.

## Alternatives considered

- **Single semaphore everywhere** — what we started with. Empirically wrong; caught by validation immediately.
- **`VK_EXT_swapchain_maintenance1`** (lets a fence guard present) — rejected. Not core; not universal across the Vulkan 1.3 baseline we set in [ADR-0009](0009-vulkan-1-3-baseline-no-fallback.md).
- **Reset semaphore between uses** — semaphores cannot be reset on the host; only by the queue's wait operation. The reuse pattern would have been correct only if the OS guaranteed presentation completion, which it does not.

## References

- `Engine/Source/Runtime/VulkanRHI/Private/VulkanResources.h` — `render_finished_per_image`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanDevice.cpp` — Submit/Present use the per-image semaphore
- [Vulkan VUID-vkQueueSubmit-pSignalSemaphores-00067](https://vulkan.lunarg.com/doc/view/1.4.313.0/linux/antora/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)
