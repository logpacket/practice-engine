# ADR-0002: RHI backend is a runtime-loaded module, not link-time

**Status:** Accepted
**Builds on:** [ADR-0001](0001-modules-are-dynamically-loaded.md)

## Context

The engine targets multiple GPU APIs over its lifetime (Vulkan now, D3D12 in Stage 5, Metal in Stage 6, possibly more on console). Two ways to organize the relationship between the host executable and the GPU backend:

- **Link the backend**: the executable links `VulkanRHI.so`. Switching backends requires a different build.
- **Load the backend**: the executable links only the RHI *interface*. The backend is discovered by name at runtime via the module loader.

## Decision

The RHI **interface** (`Engine/Source/Runtime/RHI/`) is a header-only `INTERFACE` library that any module can include. The RHI **implementation** (`VulkanRHI` and future siblings) is a `SHARED` module that the host loads at runtime via `pe::ModuleLoader::LoadModule("VulkanRHI", ...)`.

The host (Launch / sample) never links the backend, never includes its private headers, and never sees a Vulkan symbol. Three gates enforce this:

- **G1**: `ldd Binaries/.../HelloTriangle | grep vulkanrhi` returns nothing.
- **G2**: `nm -D libRenderer.so | grep ' vk[A-Z]'` returns nothing.
- **G3**: `CMake/check_no_vulkan_includes.sh` finds no `#include <vulkan/...>` under `Renderer/`.

## Consequences

**Positive:**
- Adding a new backend in Stage 5+ is purely additive: drop in `D3D12RHI` next to `VulkanRHI`, no host changes.
- Renderer code can be reasoned about without Vulkan knowledge.
- CI can validate isolation by running the three gates on every build.

**Negative:**
- The Renderer cannot reach Vulkan-only features. Anything backend-specific must be exposed through the RHI interface — which forces real abstraction work.
- One unavoidable backend-aware seam exists: `IPlatformApplication::CreateVulkanSurface` knows about Vulkan to bridge PAL ↔ RHI. See [ADR-0013](0013-surface-creation-via-callback.md) for how this is contained.

## Alternatives considered

- **Link the backend** — rejected. Defeats the purpose of having an RHI abstraction; the gates would be impossible to enforce.
- **Multiple backends linked simultaneously, runtime select** — rejected. Doubles binary size and forces every backend to compile on every platform.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/IRHIBackendModule.h` — `IRHIBackendModule::kInterfaceId`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanRHIModule.cpp` — backend implements `IModule` + `IRHIBackendModule`
- `CMake/check_no_vulkan_includes.sh` — G3 gate
- Architecture.md §1.2, §3 — isolation principles
