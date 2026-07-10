# ADR-0026: Swapchain recreation + `IWindow` resize signaling; Renderer owns acquire/present, RenderGraph owns barriers

**Status:** Accepted (implemented in Stage 2 §6.b/§6.f)

## Context

Stage 1 dodged resize: `GLFW_RESIZABLE=FALSE`, a fixed 1280×720 window, FIFO present, and `IWindow` exposes only `GetWidth`/`GetHeight` with no change notification. A real window resizes, and the swapchain (plus every size-dependent resource — here the offscreen color+depth targets and the composite descriptor set) must be recreated. Vulkan also signals this implicitly: `vkAcquireNextImageKHR`/`vkQueuePresentKHR` return `VK_ERROR_OUT_OF_DATE_KHR` / `VK_SUBOPTIMAL_KHR`.

Two design questions: (1) how does the resize signal reach the engine; (2) with a RenderGraph in the frame, who owns the acquire→render→present cycle and who owns the barriers. These are entangled because recreation must happen at a safe point in that cycle, and the borrowed swapchain-texture handles ([ADR-0022](0022-resource-barrier-replaces-transition.md)) change identity on recreate.

## Decision

**Resize signaling.** `IWindow` gains `bool ConsumeResized()` (returns and clears a dirty flag). The GLFW backend sets the flag from its framebuffer-size callback; `GLFW_RESIZABLE=TRUE`. The frame loop recreates when `ConsumeResized()` is true *or* acquire/present returns out-of-date/suboptimal.

**Ownership split.** The **Renderer owns the acquire→present cycle** (it calls `AcquireNextSwapchainImage`, builds the per-frame graph, presents, and reacts to out-of-date by calling `RecreateSwapchain` then rebuilding offscreen targets). The **RenderGraph owns barrier scheduling** within a frame ([ADR-0023](0023-render-graph-minimum-scope.md)) and never touches acquire/present. The boundary submit's swapchain semaphores flow through the explicit Submit contract ([ADR-0027](0027-submit-sync-contract.md)), which is what makes this split clean — the present-owner (Renderer) supplies present sync.

**Borrowed swapchain-texture lifecycle** (also [ADR-0021](0021-handle-generation-and-deferred-delete.md)): wrapper slots are allocated at swapchain create/recreate, `Destroy` on them is non-owning, and on recreate the wrapper generation bumps so a cached handle from before the resize is a Debug FATAL rather than a dangling `VkImage`.

## Consequences

**Positive:**
- One coherent owner for the present cycle; the RenderGraph stays a pure barrier/pass scheduler.
- Recreation is driven by both the explicit resize flag and Vulkan's out-of-date signal, covering programmatic and user-driven resizes.
- The generation bump turns "used a stale swapchain texture across resize" from silent corruption into a clear fatal.

**Negative:**
- Recreation rebuilds offscreen targets and the composite descriptor set — a visible hitch on resize; acceptable for Stage 2 (no resize-time streaming).
- The Renderer must handle out-of-date at two call sites (acquire and present); bounded and well-trodden Vulkan code.

## Alternatives considered

- **RenderGraph owns acquire/present** — rejected. Couples the barrier scheduler to swapchain/OS lifecycle; the graph should know only passes and resources.
- **Poll window size each frame and diff** — rejected in favor of an explicit dirty flag + Vulkan's out-of-date return; polling misses the driver's own suboptimal signal.
- **Recreate synchronously inside the RHI on out-of-date, hiding it from the Renderer** — rejected. The Renderer must rebuild *its* size-dependent resources (offscreen targets, descriptor set), so it has to know.

## References

- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/IWindow.h` — `ConsumeResized`
- `Engine/Source/Runtime/Platforms/GLFW/Private/GLFWWindow.cpp` — framebuffer-size callback, `GLFW_RESIZABLE`
- `Engine/Source/Runtime/RHIBackends/Vulkan/Private/VulkanDevice.cpp` — `RecreateSwapchain`, out-of-date handling
- [ADR-0021](0021-handle-generation-and-deferred-delete.md), [ADR-0022](0022-resource-barrier-replaces-transition.md), [ADR-0023](0023-render-graph-minimum-scope.md), [ADR-0027](0027-submit-sync-contract.md)
- Stage2.md §6.f, gates G10/G11
