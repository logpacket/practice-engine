# ADR-0003: RHI uses typed-Tag handles, not smart pointers

**Status:** Accepted

## Context

The RHI needs to identify GPU resources (buffers, shaders, pipelines, swapchains, command lists) across module boundaries. Three approaches:

- **`shared_ptr<IRHIResource>`** — natural C++, automatic lifetime. But cannot be a bindless-table index, breaks across the C ABI, and the control block crosses heap boundaries (Architect M1 risk).
- **Typed raw pointers** — efficient. But no Debug-mode safety, no double-destroy detection, no obvious correspondence with bindless tables.
- **Typed `Tag` handles** — `struct RHIHandle<Tag> { uint32_t index; }`. 8 bytes (Stage 2 will widen to 16 with a generation counter). Indices map directly to descriptor table slots.

## Decision

Every RHI resource type is a `RHIHandle<Tag>` (templated by a per-type tag struct so handles are type-safe and not interchangeable). Index `0` is reserved as "invalid handle".

Stage 1 keeps handles simple (`uint32_t index` only). VulkanRHI's internal slot pool (`TResourcePool<TPayload, Tag>`) tracks an `ESlotState` per slot in Debug builds, asserting `ENGINE_CHECK(state == Live)` on every `Get` and `Remove`. Release builds skip the assert but still maintain the state machine for future generation-counter promotion.

Stage 2 will add a generation counter alongside the index when deferred-delete + multi-frame-in-flight arrive — the change is internal to RHI, the public handle type widens from 32 to 64 bits.

## Consequences

**Positive:**
- Trivially copyable, ABI-safe, fits in a register.
- Direct correspondence with bindless descriptor indices once Stage 2 enables it.
- Survives hot reload because handles refer to engine-owned tables, not module-allocated objects.
- Use-after-destroy is a Debug FATAL with a clear log line, not a silent GPU crash.

**Negative:**
- Debugger watch windows show integers instead of named resources. Mitigated by an optional debug name table queried via `device->DebugName(handle)`.
- No automatic lifetime — callers must explicitly `Destroy(handle)` (Stage 1) or rely on RAII wrappers built atop handles (Stage 2+).

## Alternatives considered

- **`shared_ptr<IRHIResource>`** — rejected. Bindless rendering (Vulkan `VK_EXT_descriptor_indexing`, D3D12 SM6.6 dynamic resources) needs an index, not a pointer. Hot reload survivability is also weakened.
- **Generation counter in Stage 1** — rejected as Stage 1 has 5-10 resources with program-lifetime durability; the slot state enum catches the relevant bug class for free.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/RHITypes.h` — `RHIHandle<Tag>`
- `Engine/Source/Runtime/VulkanRHI/Private/VulkanResourcePool.h` — slot pool with Debug `ESlotState`
- Architecture.md §3.2 — Stage 1 handle model
