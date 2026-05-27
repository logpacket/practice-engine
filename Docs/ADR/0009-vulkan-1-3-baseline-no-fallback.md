# ADR-0009: Vulkan 1.3 baseline; no driver-version fallback

**Status:** Accepted

## Context

The Renderer's design uses dynamic rendering (no `VkRenderPass`/`VkFramebuffer`) and `synchronization2` (the cleaner `VkImageMemoryBarrier2` flavor). Both are core in Vulkan 1.3 — earlier drivers need extensions (`VK_KHR_dynamic_rendering`, `VK_KHR_synchronization2`) that not every device honors.

The decision: define the minimum acceptable driver, or build a multi-path renderer that handles older variants.

## Decision

**Vulkan 1.3 with `dynamicRendering` and `synchronization2` features is the floor**, plus a graphics queue family that also supports present on the active surface. Any physical device that fails any of these is rejected at `SelectPhysicalDevice` with a clear `ENGINE_FATAL` log line. No alternative path is built.

`VkPhysicalDeviceVulkan13Features::dynamicRendering` and `synchronization2` are explicitly enabled at `vkCreateDevice` time and queried at selection time.

## Consequences

**Positive:**
- The renderer is one code path. No `#ifdef` rats' nests, no "if old driver, do X; else Y" branches.
- Architecture decisions (no VkRenderPass infrastructure, sync2 barrier types only) stay clean.
- Vulkan SDK ≥ 1.3 (LunarG) is already the documented build requirement in `BUILDING.md` — runtime requirement matches build requirement.

**Negative:**
- Users on ancient drivers (pre-2022 NVIDIA, pre-Mesa 23.3 for lavapipe) cannot run the engine. Acceptable: Stage 1 audience is the developer who chose to install the Vulkan SDK in the first place.
- CI runners without GPU hardware need software Vulkan 1.3 (lavapipe ≥ 23.3). Documented in `BUILDING.md`.
- Some valid GPUs without a unified graphics+present queue family (some older mobile, some older AMD) cannot be selected. Acceptable for Stage 1; Stage 2 will add a separate present-queue path if a real user surfaces.

## Alternatives considered

- **Build a 1.2-with-extensions path** — rejected. Doubles the maintenance surface for a renderer that has barely been written, before any user has asked.
- **Detect-and-fall-back at runtime** — rejected on architectural grounds: the engine prefers clear failure over silent feature regression. A user with a 1.2 driver will see "Vulkan 1.3 with dynamicRendering required" in the log, not a black screen and unexplained validation warnings.

## References

- `Engine/Source/Runtime/VulkanRHI/Private/VulkanDevice.cpp` — `SelectPhysicalDevice()` + `CreateLogicalDevice()`
- `BUILDING.md` — Vulkan SDK + driver requirements
- Architecture.md §3.6 — swapchain policy
