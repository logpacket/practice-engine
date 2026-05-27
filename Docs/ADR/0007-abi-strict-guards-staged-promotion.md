# ADR-0007: ABI strict guards introduced in Stage 1, enforced in Stage 3

**Status:** Accepted

## Context

Dynamic module loading (ADR-0001) makes the C++ ABI between modules safety-critical: Windows DLLs may use different CRT instances, STL implementations can diverge across translation units in release-build configurations, and hot-reloaded modules (Stage 3+) must be swappable while preserving live object identity.

The strict rules that make this safe are:
1. Only `extern "C"` functions and pure-abstract interfaces cross the boundary.
2. No `std::string`, `std::vector`, `std::function`, exceptions, or RTTI across the boundary.
3. Memory allocated by one side is freed by the same side (or routed through `IEngineAllocator`).

The v1 design wanted these as enforced `#error` checks in Stage 1 headers. The Critic review found this over-aggressive: Stage 1 has zero gameplay modules, every binary is built by the same compiler in the same build, and the boilerplate cost of strictness (no `std::string` arguments, no `std::function` callbacks) is real and slows authoring.

## Decision

**Stage 1 introduces the ABI types** (`EngineResult`, `EngineStringView`, `EngineSpan<T>`, `IEngineAllocator`, `EngineInterfaceId`) and uses them on every interface that *might* eventually cross the boundary. The strict `#error` guards (banning STL/exceptions/RTTI in public headers) are written into `<Core/EngineAbi.hpp>` but **not turned on** in Stage 1.

**Stage 3 promotes the guards to enforced.** The promotion procedure:
1. Audit `Core` / `RHI` / `VulkanRHI` / `ApplicationCore` / `Renderer` / `Launch` public headers for STL leakage.
2. Enable the `#error` guards on public headers first (private files stay unconstrained).
3. After violations reach 0, enable guards on all headers.
4. Gameplay modules from then on are subject to strict ABI.

## Consequences

**Positive:**
- Stage 1 authoring stays ergonomic; std::string/std::filesystem::path are usable in IEngineContext, FPaths, etc.
- The ABI infrastructure (POD value types, factory pattern, EngineInterfaceId for QueryInterface) is in place from day 1 — no big-bang migration.
- Stage 3 promotion is bounded and auditable: walk the public header set once.

**Negative:**
- Stage 1 / 2 code can accumulate latent ABI violations that surface only at Stage 3 promotion. Mitigated by the audit being mechanical (grep for `<string>`, `<vector>`, etc. in `Public/`).
- Hot reload of Stage 1 / 2 code (if anyone tries) will not be safe. Acceptable: there are no Stage 1 / 2 use cases.

## Alternatives considered

- **Enforce strict ABI from Stage 1** — rejected as overaggressive (Critic). Forces engine-owned containers / two-header API patterns before any gameplay use case has shown what is actually painful.
- **Never enforce strict ABI** — rejected. Hot reload (Stage 3+) does not work safely without it; Windows CRT mismatches are silent corruption.
- **Enforce only on hot-reloadable modules** — partially incorporated. The `hot_reloadable` flag on `ModuleDescriptor` is reserved for Stage 3 and will gate the strict check.

## References

- `Engine/Source/Runtime/Core/Public/Core/EngineAbi.hpp` — POD value types + `IEngineAllocator` + `EngineInterfaceId`
- `Engine/Source/Runtime/Core/Public/Core/Module.h` — `IModule`, `DECLARE_ENGINE_MODULE`
- Architecture.md §2.1 — staged enforcement plan
- ADR-0001 — the dynamic loading regime that makes this matter
