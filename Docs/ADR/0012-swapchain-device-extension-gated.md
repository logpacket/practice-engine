# ADR-0012: `VK_KHR_swapchain` device extension gated on caller's surface-creation intent

**Status:** Accepted (post-§6.c validation finding)

## Context

`VK_KHR_swapchain` (a device extension) requires `VK_KHR_surface` (an instance extension) to be in the enabled instance-extension list. The `VK_KHR_surface` extension is normally contributed by the PAL via `IPlatformApplication::GetRequiredVulkanInstanceExtensions()` — which only runs when there is a window.

The headless test `rhi_smoke` (§6.c) loads VulkanRHI, creates a device, and tears down — with no window, no PAL, and no `VK_KHR_surface` in the instance extension list. The initial implementation unconditionally enabled `VK_KHR_swapchain` at `vkCreateDevice`, which validation flagged:

```
VK_KHR_swapchain device extension required VK_KHR_surface instance extension
which is not enabled.
```

## Decision

`VK_KHR_swapchain` is enabled at `vkCreateDevice` **only if** the caller supplied a non-null `create_surface` callback in `RHIDeviceCreateDesc`. The reasoning: a caller that has no way to create a surface cannot use a swapchain anyway, so requesting the extension is pointless and trips validation in headless tests.

```cpp
const bool want_swapchain = (create_surface_ != nullptr);
if (want_swapchain) {
    info.enabledExtensionCount   = std::size(kSwapchainDeviceExtensions);
    info.ppEnabledExtensionNames = kSwapchainDeviceExtensions;
}
```

The log line `VkDevice created (1 graphics queue, swapchain=enabled|disabled (headless))` makes the device mode visible.

## Consequences

**Positive:**
- Headless tests work without dragging in the windowing system.
- Future "compute-only" workloads (Stage 3+, e.g. cooked-asset processing on the GPU) get a natural device-create path with no windowing dependency.
- Validation stays clean across both code paths.

**Negative:**
- A caller that intends to swapchain but forgets to set `create_surface` will get a runtime error in `CreateSwapchain` ("create_surface callback is null") instead of a configure-time complaint. Acceptable: the path is one log line away from clarity.

## Alternatives considered

- **Always enable `VK_KHR_swapchain`** — what we started with. Triggers validation in headless tests; the only way out would be to add `VK_KHR_surface` to instance extensions unconditionally, which then forces validation to demand a platform-surface extension that doesn't exist headless.
- **Add a separate "headless" mode flag to `RHIDeviceCreateDesc`** — rejected as redundant. `create_surface == nullptr` already expresses the same intent.

## References

- `Engine/Source/Runtime/VulkanRHI/Private/VulkanDevice.cpp` — `CreateLogicalDevice` swapchain gating
- `Engine/Source/Runtime/Tests/rhi_smoke/Private/main.cpp` — the headless caller that surfaced the bug
- ADR-0013 — the surface-creation callback design that makes this gating possible
