# ADR-0001: Modules are dynamically loaded shared libraries

**Status:** Accepted

## Context

The engine needs to be modular in a way that supports (a) swapping a backend without rebuilding the host (e.g. picking VulkanRHI vs a future D3D12RHI), (b) hot-reloading gameplay code without restarting the editor, and (c) keeping the link-time dependency graph between modules from collapsing into a single statically-linked monolith.

Two broad approaches existed:

- **Static linking with module registry**: every module compiles as `STATIC` and registers itself into a global registry at process start. Simple to build but no runtime swap, no hot reload, and every change recompiles the world.
- **Dynamic loading of `.so` / `.dll`**: each module ships as a shared library with a tiny C factory entry point. The host loads modules at runtime via `dlopen` / `LoadLibrary`. Pattern matches Unreal's `IModuleInterface`.

## Decision

Modules ship as shared libraries (`SHARED` in CMake). The host (`Launch` or any external embedder) discovers them at runtime via `pe::ModuleLoader::LoadModule(name, allocator, ctx)`. Every module exports exactly one `extern "C"` factory pair (`CreateModule_<Name>` / `DestroyModule_<Name>`) emitted by `DECLARE_ENGINE_MODULE`.

Stage 1 keeps the loader minimal (~100 LOC including Windows UTF-16 handling): direct `dlopen` + symbol lookup, no shadow-copy, no dependency graph, no manifest scanning, no hot reload. Those layers are deferred to Stage 3 when gameplay modules arrive.

## Consequences

**Positive:**
- Backends are runtime-swappable (Stage 5 D3D12 will slot in without touching the executable).
- Test executables (`rhi_smoke`, `app_smoke`, `dummy_module_smoke`) verify isolation by NOT linking the module they exercise.
- Gameplay hot reload becomes possible later by upgrading `ModuleLoader`, not by rewriting the load path.

**Negative:**
- ABI boundary requires discipline. Pure-virtual interfaces only, POD value types only across the C symbol boundary. See [ADR-0007](0007-abi-strict-guards-staged-promotion.md).
- Cross-module logging is restricted by spdlog being statically linked into each `.so`. Resolved at Stage 2 by routing all logging through Core's exposed wrappers.

## Alternatives considered

- **Static linking everywhere** — rejected. No backend swap, no hot reload, no isolation gates worth running.
- **Plugin manifest + dependency graph at Stage 1** (like Unreal's `.uplugin` + UBT) — rejected as YAGNI. Six modules and a linear dependency graph do not need Tarjan SCC. Deferred to Stage 3.
- **Shadow-copy DLLs at Stage 1** — rejected. Shadow copies only matter for hot reload, which is Stage 3.

## References

- `Engine/Source/Runtime/Core/Public/Core/Module.h` — `IModule`, `DECLARE_ENGINE_MODULE`
- `Engine/Source/Runtime/Core/Public/Core/ModuleLoader.h` — load/unload API
- `Engine/Source/Runtime/Core/Private/ModuleLoader.cpp` — dlopen/LoadLibrary wrapper
- Architecture.md §2 — module system overview
