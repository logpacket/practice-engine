# ADR-0025: Asset system v1 scope — synchronous, in-memory, no cooking/streaming/decoders

**Status:** Accepted (implemented in Stage 2 §6.g)

## Context

The Renderer currently hand-rolls file loading: `LoadShaderBytes` (`Renderer/Private/Renderer.cpp`) opens an `std::ifstream`, reads the SPIR-V into a `std::vector<uint8>`, and feeds it to `CreateShader`. This is a real, present consumer of "load bytes from disk", duplicated wherever else a module needs a file. Stage 2 introduces an Asset module; the question is how much of an asset pipeline v1 should be.

A mature asset system has cooking (offline transform to a runtime format), streaming (async partial loads with budget), decoders (PNG/KTX/glTF), a content-addressed cache, and hot-reload. Each is substantial and none has a Stage 2 consumer: the only thing that needs loading is SPIR-V (and later, raw blobs). Building decoders/streaming now would be speculative — the failure mode [ADR-0008](0008-stage1-rhi-minimum-surface.md) names one layer up.

## Decision

Asset v1 is the minimum that absorbs the existing consumer:

- **Synchronous** `LoadBytes(path) → blob`, owning memory through the engine allocator.
- A **path → AssetId** registry and an **in-memory cache** (same path returns the cached blob).
- The Renderer routes shader loading through Asset; no direct `ifstream` remains in the Renderer.

Explicitly **out of scope** (Stage 3+): cooking, async/streaming loads, image/mesh decoders, content-addressed hashing, hot reload, reference counting / eviction.

## Consequences

**Positive:**
- The justifying consumer is real and already in the tree — the API shape is validated, not speculative.
- A single choke point for "load bytes" that later async/streaming versions can sit behind without changing callers.
- No new third-party dependency (no image decoder) enters Stage 2.

**Negative:**
- Synchronous loads block the calling thread — fine for a handful of SPIR-V blobs at init; revisited when streaming is needed (Stage 3+).
- The in-memory cache never evicts; bounded by the tiny Stage 2 asset set.

## Alternatives considered

- **Add an image decoder (stb_image) so Asset loads a texture** — rejected. Invents a consumer and re-imports the sampler/decode scope Stage 2 is bounding; the real consumer is SPIR-V loading.
- **Leave loading ad-hoc in the Renderer** — rejected. Duplicated `ifstream` logic; no choke point for later async loading; nothing to justify an Asset module.

## References

- `Engine/Source/Runtime/Asset/` — new module
- `Engine/Source/Runtime/Renderer/Private/Renderer.cpp` — `LoadShaderBytes` (absorbed)
- [ADR-0008](0008-stage1-rhi-minimum-surface.md)
- Stage2.md §6.g, gate G13
