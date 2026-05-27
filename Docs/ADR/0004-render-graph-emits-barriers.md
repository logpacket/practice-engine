# ADR-0004: Render graph computes barriers, RHI emits them

**Status:** Accepted (RenderGraph itself is deferred to Stage 2)

## Context

Modern explicit GPU APIs (Vulkan, D3D12) require the caller to declare resource layout transitions and memory barriers explicitly. Implementing these correctly across a real frame (dozens of passes, multiple queues, async compute, transient resources) is the central correctness problem of a renderer.

Two design directions:

- **"Smart RHI"**: the RHI tracks resource state and inserts barriers automatically. Looks ergonomic. Becomes either slow (conservative barriers everywhere) or wrong (state escapes the tracker via multi-queue / bindless).
- **Render graph layer above the RHI**: the graph declares pass I/O, computes barriers from the dependency graph, and instructs the RHI to emit them. The RHI itself is thin and never tracks state.

Unreal RDG, Frostbite FrameGraph, and Granite's render graph all chose the graph-above-RHI design after iterating through smart-RHI approaches.

## Decision

The RHI is a thin emit-only layer. It exposes explicit barrier primitives (Stage 2: `ResourceBarrier(span)`) but never decides when to insert them. A future `RenderGraph` module (Stage 2) sits above the RHI, declares pass I/O, computes the barrier schedule, and calls into the RHI to emit.

Stage 1 has no RenderGraph yet. To avoid the §3.1 "automatic barriers" original-design self-contradiction, the Stage 1 RHI exposes two purpose-built methods, `TransitionToRenderTarget` and `TransitionToPresent`, used exclusively by the hand-rolled Renderer frame loop. These two methods are documented Stage-1-only API and will be replaced by the general `ResourceBarrier(span)` in Stage 2 when RenderGraph arrives. The call site is one file (`Renderer/Private/Renderer.cpp`), so the eventual replacement is bounded.

## Consequences

**Positive:**
- Multi-queue, async compute, and bindless resource access all work cleanly because the graph has total knowledge of resource use.
- RHI implementations stay small and uniform across backends.
- Renderer code reads top-down ("here is what this pass does"), not bottom-up ("here is the barrier I want").

**Negative:**
- The graph is non-trivial to build (Stage 2 work). Until it lands, the Stage 1 Renderer hand-rolls barriers — a known limitation.
- Debugging "missing barrier" bugs requires graph-level introspection tooling (a visualizer that prints the inferred barriers per pass).

## Alternatives considered

- **Smart RHI** — rejected. Either slow or wrong under multi-queue/bindless; the failure modes are silent GPU corruption.
- **Have Stage 1 ship a tiny render graph** — rejected. RenderGraph design depends on multiple use cases (offscreen, depth, multi-pass) which Stage 1 doesn't have; would lock in an untested interface.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/IRHICommandList.h` — `TransitionToRenderTarget` / `TransitionToPresent` (Stage 1)
- `Engine/Source/Runtime/Renderer/Private/Renderer.cpp` — hand-rolled barrier sequence
- Architecture.md §3.1, §9 Stage 2 — render graph plans
