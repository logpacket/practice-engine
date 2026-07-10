# ADR-0022: `ResourceBarrier(span)` + `ERHIResourceState` replaces Stage-1-only `TransitionTo*`

**Status:** Accepted (implemented in Stage 2 §6.c). Closes the transitional API flagged by [ADR-0004](0004-render-graph-emits-barriers.md) and [ADR-0008](0008-stage1-rhi-minimum-surface.md).

## Context

Stage 1 had no RenderGraph, so [ADR-0004](0004-render-graph-emits-barriers.md) gave the command list two purpose-built, swapchain-only methods — `TransitionToRenderTarget` and `TransitionToPresent` — used by the hand-rolled Renderer to move the swapchain image between layouts. Both ADR-0004 and [ADR-0008](0008-stage1-rhi-minimum-surface.md) documented these as Stage-1-only, to be replaced by a general barrier primitive "when RenderGraph arrives", with a single call site (`Renderer.cpp`) bounding the break.

A real frame transitions many resources between many states: a color target goes Undefined → RenderTarget → ShaderResource (sampled by the next pass) → ... ; a depth target Undefined → DepthAttachment; the swapchain image RenderTarget → Present. Two hard-coded swapchain methods cannot express this, and crucially the *RHI must not decide when* — the RenderGraph computes the schedule and the RHI only emits ([ADR-0004](0004-render-graph-emits-barriers.md)).

These two methods also carried a hidden cost: they were the side channel `Submit` used to discover the bound swapchain. Removing them forces the explicit Submit contract ([ADR-0027](0027-submit-sync-contract.md)).

## Decision

Remove `TransitionToRenderTarget` / `TransitionToPresent`. Add one general primitive:

```
void IRHICommandList::ResourceBarrier(EngineSpan<const RHIResourceBarrier>);
struct RHIResourceBarrier { RHITextureHandle texture; ERHIResourceState before, after; };
enum class ERHIResourceState { Undefined, RenderTarget, DepthAttachment, ShaderResource, CopySrc, CopyDst, Present };
```

The swapchain image is addressed as a borrowed `RHITextureHandle` via `device->GetSwapchainImageTexture` ([ADR-0026](0026-swapchain-recreation-and-resize.md)), so barriers operate uniformly over textures and swapchain images. The state enum is backend-neutral: on Vulkan each state maps to a `(VkImageLayout, VkPipelineStageFlags2, VkAccessFlags2)` triple emitted through `synchronization2`; on D3D12 to a `D3D12_RESOURCE_STATES`. Only the RenderGraph calls `ResourceBarrier` ([ADR-0023](0023-render-graph-minimum-scope.md)).

## Consequences

**Positive:**
- One primitive expresses every layout/access transition; new resource states are additive enum values.
- The RHI stays emit-only — no state tracking, the failure mode ADR-0004 rejected.
- `synchronization2` validation catches missing/wrong barriers, which gate G7 leans on.

**Negative:**
- An intentional break of a 2-method surface (one caller), exactly as ADR-0004/0008 predicted.
- Authoring raw barriers by hand is error-prone — which is *why* only the RenderGraph emits them, never gameplay or Renderer pass code.

## Alternatives considered

- **Keep `TransitionTo*` and add `ResourceBarrier` alongside** — rejected. Two ways to transition the swapchain, and the dead methods keep the `Submit` side channel alive.
- **Smart-RHI auto-barriers** — rejected by [ADR-0004](0004-render-graph-emits-barriers.md): slow or wrong under multi-queue/bindless.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/IRHICommandList.h` — `TransitionTo*` (removed), `ResourceBarrier` (added)
- `Engine/Source/Runtime/Renderer/Private/Renderer.cpp` — the single Stage 1 call site
- [ADR-0004](0004-render-graph-emits-barriers.md), [ADR-0008](0008-stage1-rhi-minimum-surface.md), [ADR-0023](0023-render-graph-minimum-scope.md), [ADR-0026](0026-swapchain-recreation-and-resize.md), [ADR-0027](0027-submit-sync-contract.md)
- Stage2.md §6.c, gate G7
