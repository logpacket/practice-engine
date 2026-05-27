# ADR-0015: PAL graphics-interop methods take a backend enum, not Vulkan-named methods

**Status:** Accepted

## Context

The Stage 1 `IPlatformApplication` interface exposed two methods with Vulkan in their names:

```cpp
virtual EngineSpan<const char* const> GetRequiredVulkanInstanceExtensions() const = 0;
virtual EngineResult CreateVulkanSurface(void* vk_instance_opaque,
                                         IWindow* window,
                                         void** out_surface_opaque) = 0;
```

The PAL is supposed to be backend-neutral ([ADR-0005](0005-applicationcore-as-pal.md)) — its job is to abstract the platform (Windows / Linux / macOS / console) for any client, not to enshrine one graphics API. Vulkan-named methods would force Stage 5 (D3D12) and Stage 6 (Metal) to either grow parallel methods (`GetRequiredD3D12...`, `CreateMetalSurface`) or to leave the abstraction in awkward shape where one backend's needs are first-class and others' are bolted on.

The "PAL backend implementation knows about graphics backends" reality cannot be hidden — surface creation IS per-API. GLFW knows how to call `glfwCreateWindowSurface(VkInstance, …)` for Vulkan; future Win32 PAL will know how to return an `HWND` for D3D12. So the interface must let callers say which backend they need; the implementation may say "I do not interoperate with that one."

## Decision

Replace the two methods with their parameterized equivalents that take an `EGraphicsBackend` enum value:

```cpp
enum class EGraphicsBackend : std::uint16_t {
    Unknown = 0,
    Vulkan  = 1,
    D3D12   = 2,
    Metal   = 3,
};

class IPlatformApplication {
    virtual EngineSpan<const char* const>
        GetRequiredGraphicsInstanceExtensions(EGraphicsBackend backend) const = 0;

    virtual EngineResult CreateGraphicsSurface(EGraphicsBackend backend,
                                               void* backend_instance_opaque,
                                               IWindow* window,
                                               void** out_surface_opaque) = 0;
};
```

The enum lives in `Core/EngineAbi.hpp` so the PAL can reference it without depending on the RHI module (which is the natural conceptual home for "list of supported backends", but adding RHI as an ApplicationCore dep just for one enum is overhead).

PAL backend implementation pattern:
- `EGraphicsBackend::Vulkan` — implemented via `glfwGetRequiredInstanceExtensions` / `glfwCreateWindowSurface` (GLFW backend) or per-OS surface APIs (Win32 / Wayland / Cocoa native backends).
- `EGraphicsBackend::D3D12` — empty extension span; `CreateGraphicsSurface` returns `HWND` as `void*`. Native Win32 PAL will implement; GLFW returns failure.
- `EGraphicsBackend::Metal` — empty extension span; `CreateGraphicsSurface` returns `CAMetalLayer*`. Cocoa PAL will implement.

A PAL backend that cannot interoperate with the requested graphics backend on the active platform returns an empty span (extensions) or `EngineResult::Fail(...)` (surface) with a clear log line. Callers (Launch, samples) know which backend they loaded and pass that backend explicitly.

## Consequences

**Positive:**
- The PAL is now backend-neutral by contract. Adding D3D12 (Stage 5) or Metal (Stage 6) does not require touching `IPlatformApplication`'s shape.
- Per-backend interop quirks (D3D12 has no `surface` concept, Metal uses `CAMetalLayer`) are encoded in the implementation's case dispatch, not in the interface.
- Hosts state intent explicitly (`pa->CreateGraphicsSurface(EGraphicsBackend::Vulkan, …)`), which reads more clearly than a method named after the API.

**Negative:**
- One extra parameter at call sites (`EGraphicsBackend::Vulkan`). Trivial.
- PAL backends must `switch` on the enum and return failure for backends they don't support. Acceptable — the interface honestly reflects that PAL↔graphics interop is bilateral.
- The `void*` opaque cast convention remains; readers must know "for Vulkan this is `VkInstance` / `VkSurfaceKHR`, for D3D12 this is nullptr / `HWND`". Documented inline in `IPlatformApplication.h`.

## Alternatives considered

- **Keep Vulkan-named methods + add D3D12-named ones later** — rejected. Bloats the interface, treats Vulkan as privileged. The PAL has no business knowing which graphics API is "primary".
- **Per-graphics-backend sub-interface** (`IPlatformVulkan` queried via `IModule::QueryInterface`) — overkill for two methods. Worth considering at Stage 5+ if interop surface grows beyond extensions+surface (e.g. swapchain interop, sync interop). Easy to add then; no need now.
- **Templated method `Create<Backend>(...)` with a backend tag type** — would force the PAL header to declare per-backend tag types, which is more lock-in than the enum.
- **Put `EGraphicsBackend` in the RHI module** — rejected; would force ApplicationCore to PUBLIC-depend on RHI, which has no other reason to exist for the PAL. The enum is small and cross-cutting; Core is the right home.

## References

- `Engine/Source/Runtime/Core/Public/Core/EngineAbi.hpp` — `EGraphicsBackend`
- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/IPlatformApplication.h` — renamed methods
- `Engine/Source/Runtime/ApplicationCore/Private/GLFW/GLFWApplication.cpp` — Vulkan-only dispatch
- `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` — call sites
- `Samples/HelloTriangle/Private/main.cpp` — sample call sites
- [ADR-0005](0005-applicationcore-as-pal.md) — the PAL contract the rename brings the interface closer to
- [ADR-0013](0013-surface-creation-via-callback.md) — the surface-callback indirection between RHI and PAL (unchanged; only the PAL side's spelling changed)
