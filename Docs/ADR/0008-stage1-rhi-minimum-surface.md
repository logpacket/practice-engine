# ADR-0008: Stage 1 RHI minimum surface — 13 device methods + 9 command list methods

**Status:** Accepted

## Context

The v1 design committed Stage 1 to a 30+ method `IRHIDevice` (including Compute/Indirect/BindGroup/Map/Fence/Semaphore/MultiQueue). The Architect/Critic reviews flagged this as untested interface lock-in: declaring methods that no Stage 1 code calls means we will have shaped them wrong by the time the first caller (Stage 2-3) actually arrives, and changing pure-virtual methods in a SHARED library is an ABI break.

## Decision

Stage 1's `IRHIDevice` exposes exactly the methods the Renderer needs to draw one triangle:

- 4 resource constructors (`CreateBuffer`, `CreateShader`, `CreateGraphicsPipeline`, `CreateSwapchain`)
- 4 destructors (one per resource type)
- 2 command list lifecycle methods (`AcquireCommandList`, `Lock`)
- 3 frame methods (`Submit`, `AcquireNextSwapchainImage`, `Present`)
- 1 sync method (`WaitIdle`)

That is 14 method slots (one more than the original "13" count in the design — `AcquireNextSwapchainImage` was implicit in the design's loop diagram). `IRHICommandList` is similarly thin: `Begin`, `End`, `TransitionToRenderTarget`, `TransitionToPresent`, `BeginRenderPass`, `EndRenderPass`, `SetViewport`, `SetScissor`, `SetPipeline`, `SetVertexBuffer`, `Draw` — 11 methods (the original "7" undercounted by missing viewport/scissor/end-renderpass).

Everything else (Compute, Indirect, BindGroup, MapBuffer, fences/semaphores as user types, multi-queue Submit, debug labels) is *not declared*. Methods get added when the first caller appears in Stage 2-3.

## Consequences

**Positive:**
- Every method is exercised by Renderer code, so its shape is validated.
- Adding a method later is purely additive — no break.
- D3D12/Metal backends (Stage 5+) see a small set to implement.

**Negative:**
- The Stage 2 `TransitionToRenderTarget` / `TransitionToPresent` pair are clearly transitional — they will be replaced by a general `ResourceBarrier(span)` when RenderGraph arrives. This is an *intentional* break of a 2-method surface used by 1 caller, documented in [ADR-0004](0004-render-graph-emits-barriers.md).
- Anyone reading the interface might assume the engine is incapable of compute or indirect draws. Mitigated by the §9 roadmap in Architecture.md and per-method comments.

## Alternatives considered

- **Full 30+ method surface at Stage 1** — rejected. Locks in untested interfaces; reviewers (Architect, Critic) flagged it as the #1 risk in v1.
- **Even smaller surface (10 methods)** — rejected. `AcquireCommandList`/`Lock`/`Submit` separation, while feeling over-decomposed for one frame in flight, was kept to signal the Stage 2 multi-queue path. If still problematic in Stage 2 we will collapse via a fresh ADR.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/IRHIDevice.h`
- `Engine/Source/Runtime/RHI/Public/RHI/IRHICommandList.h`
- Architecture.md §3.3, §3.4
- ADR-0004 — render graph plan that drives later barrier-method redesign
