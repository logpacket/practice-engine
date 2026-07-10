# ADR-0023: RenderGraph minimum scope — declarative pass I/O + topo schedule; no aliasing/async-compute/multi-queue

**Status:** Accepted (implemented in Stage 2 §6.d)

## Context

[ADR-0004](0004-render-graph-emits-barriers.md) decided a render graph sits above the RHI, computes the barrier schedule from pass dependencies, and is the only thing that calls `ResourceBarrier`. It deferred the graph itself to Stage 2 and rejected shipping a tiny graph in Stage 1 because "RenderGraph design depends on multiple use cases (offscreen, depth, multi-pass) which Stage 1 doesn't have." Stage 2 now has exactly those use cases (ScenePass → offscreen color+depth → CompositePass sampling).

Mature render graphs (Unreal RDG, Frostbite FrameGraph, Granite) also do resource aliasing (transient memory reuse), async-compute scheduling across queues, and split barriers. Each is a real feature and a real way to ship a subtly-wrong barrier. The risk of Stage 2 is over-building the graph to those targets before there is a frame that needs them.

## Decision

The Stage 2 RenderGraph implements the minimum that drives a two-pass frame correctly:

- **Declarative pass I/O.** Each pass declares the resources it reads and writes and the `ERHIResourceState` it needs them in. Public pass-I/O types use `EngineSpan` and RHI handles, never `std::vector`/`std::string`, at the module boundary ([ADR-0007](0007-abi-strict-guards-staged-promotion.md)).
- **Topological order + barrier computation.** The graph topo-sorts passes by dependency and, for each resource edge, emits the `before→after` `ResourceBarrier` before the consuming pass.
- **Barrier introspection log.** The graph can print the inferred barriers per pass — the debugging tool [ADR-0004](0004-render-graph-emits-barriers.md) called out as necessary.

Explicitly **out of scope** (deferred): resource aliasing / transient memory pools, async compute, multi-queue scheduling, split barriers, automatic culling of unused passes. The graph holds no queue knowledge; everything runs on the single graphics queue.

## Consequences

**Positive:**
- The barrier-correctness problem is solved once, centrally; the Renderer reads top-down ("here is what each pass does").
- Adding aliasing/async-compute later is additive to a working, tested scheduler.
- Small enough that gate G7 (validation-clean across the graph) is a meaningful proof of the whole layer.

**Negative:**
- No transient-memory reuse: offscreen targets hold their own allocations. Fine for two passes; revisited when pass count and resolution grow.
- A linear/topo schedule leaves GPU parallelism (async compute) on the table — intentionally, until a frame needs it.

## Alternatives considered

- **Port a full graph (aliasing + async compute) now** — rejected. Over-builds against one frame; locks in an untested scheduler, the failure mode [ADR-0008](0008-stage1-rhi-minimum-surface.md) guards against.
- **No graph; keep hand-rolled barriers in the Renderer** — rejected by [ADR-0004](0004-render-graph-emits-barriers.md); hand-rolling does not scale past the two Stage 1 barriers and puts barrier logic in pass code.

## References

- `Engine/Source/Runtime/RenderGraph/` — new module
- [ADR-0004](0004-render-graph-emits-barriers.md), [ADR-0007](0007-abi-strict-guards-staged-promotion.md), [ADR-0022](0022-resource-barrier-replaces-transition.md)
- Stage2.md §6.d, gate G7
