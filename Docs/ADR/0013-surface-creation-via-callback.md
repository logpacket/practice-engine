# ADR-0013: Surface creation via callback in `RHIDeviceCreateDesc`

**Status:** Accepted

> The PAL-side method `CreateVulkanSurface` referenced below was later renamed to `CreateGraphicsSurface(EGraphicsBackend, …)` to keep the PAL backend-neutral - see [ADR-0015](0015-pal-graphics-backend-abstraction.md). The callback-via-`RHIDeviceCreateDesc` mechanism described here is unchanged; only the PAL method spelling differs in current code.

## Context

Vulkan swapchain creation needs a `VkSurfaceKHR`, which is created from a `VkInstance` plus an OS-native window handle. The instance lives in VulkanRHI; the window handle lives in ApplicationCore. The natural way to bridge them creates a module-dependency problem:

- **VulkanRHI calls ApplicationCore**: forces `Engine::VulkanRHI` to link or include `Engine::ApplicationCore`, defeating the PAL's "ApplicationCore is dlopen-only" rule ([ADR-0005](0005-applicationcore-as-pal.md)).
- **ApplicationCore calls VulkanRHI**: forces ApplicationCore to know about Vulkan types, which breaks its backend-neutrality (Win32/Cocoa/SDL3 backends would all need the same knowledge).
- **Host (Launch) bridges them with raw types** by getting the `VkInstance` from one and the window from the other: requires exposing `VkInstance` through the RHI interface, which is the very Vulkan-leak we are trying to avoid.

## Decision

`RHIDeviceCreateDesc` carries a C function pointer + opaque userdata:

```cpp
using PFN_RHICreateSurface = EngineResult (*)(void* userdata,
                                              void* backend_instance_opaque,
                                              void** out_surface_opaque);

struct RHIDeviceCreateDesc {
    // ...
    PFN_RHICreateSurface create_surface          = nullptr;
    void*                create_surface_userdata = nullptr;
};
```

Launch (or any host) writes a tiny free-function thunk that captures the IPlatformApplication and IWindow via the userdata pointer and calls `pa->CreateVulkanSurface(vk_instance, window, out_surface)`. VulkanRHI invokes the callback from inside `CreateSwapchain`. Both sides exchange Vulkan handles as `void*`, so neither side's headers leak into the other.

```cpp
// Launch side:
struct FSurfaceBridge { IPlatformApplication* pa; IWindow* window; };
EngineResult SurfaceCreateThunk(void* ud, void* inst, void** out) {
    auto* b = static_cast<FSurfaceBridge*>(ud);
    return b->pa->CreateVulkanSurface(inst, b->window, out);
}
```

## Consequences

**Positive:**
- `Engine::RHI` does not depend on `Engine::ApplicationCore`. `Engine::VulkanRHI` does not depend on either ApplicationCore or any specific window library.
- The same pattern extends to a future D3D12 backend: its surface-equivalent (a `IDXGISwapChain` from `IDXGIFactory4::CreateSwapChainForHwnd`) plugs into the same callback shape, with the HWND riding in the userdata.
- Headless callers leave `create_surface = nullptr` and get a device that does not enable `VK_KHR_swapchain` (see [ADR-0012](0012-swapchain-device-extension-gated.md)).

**Negative:**
- The host writes ~5 lines of thunk + bridge struct per device created. Acceptable; this is exactly the cost the inline-bootstrap sample (`Samples/HelloTriangle/`) demonstrates.
- Vulkan handles cross as `void*`, losing the type safety of `VkInstance` / `VkSurfaceKHR`. Acceptable: the cast happens in two well-bounded places (PAL's `CreateVulkanSurface`, RHI's swapchain creation) and the rest of the engine never touches a raw Vulkan handle.

## Alternatives considered

- **Direct PAL-knows-RHI call** — rejected. Forces PAL to include Vulkan headers, weakens backend-neutrality.
- **Direct RHI-knows-PAL call** — rejected. Forces RHI to include PAL headers (and thus IModule + Core), inverting the dependency direction.
- **Pass an `IPlatformApplication*` directly through `RHIDeviceCreateDesc`** — rejected. Same coupling violation as the previous alternative; just hidden behind a void cast in user code.
- **Expose `VkInstance` via `IRHIBackendModule::GetVulkanInstance()`** — rejected as a Vulkan-ism in a backend-neutral interface. The callback pattern keeps the Vulkan-ism contained to the implementation files of the two modules that already are Vulkan-aware (VulkanRHI + ApplicationCore/GLFW).

## References

- `Engine/Source/Runtime/RHI/Public/RHI/RHITypes.h` — `PFN_RHICreateSurface`, `RHIDeviceCreateDesc::create_surface`
- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/IPlatformApplication.h` — `CreateVulkanSurface`
- `Engine/Source/Runtime/ApplicationCore/Private/GLFW/GLFWApplication.cpp` — GLFW implementation via `glfwCreateWindowSurface`
- `Samples/HelloTriangle/Private/main.cpp` — `SurfaceCreateThunk` + `FSurfaceBridge` example
- `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` — same pattern in production-style use
