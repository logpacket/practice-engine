# ADR-0010: `PEngine` umbrella module for external hosts

**Status:** Superseded by [ADR-0014](0014-framework-philosophy-and-per-module-pch.md)

> The `PEngine` module was removed after one sample's worth of experience showed the umbrella was a library-style abstraction inside what is intended to be a framework. Samples and external hosts now use explicit per-module includes (`<Core/...>`, `<RHI/...>`, etc.); compile speed is delivered by per-module PCH instead. See ADR-0014 for the full reasoning. The original ADR text follows for historical context.

---

**Status (original):** Accepted (v5)

## Context

External hosts (samples, embedders, future game projects) need to consume the engine. The Stage 1 modular structure exposes a fair number of pieces — `Core`, `RHI`, `Renderer`, `ApplicationCore`, `Launch` — each with its own header subdirectory and CMake target. Two early samples (the original `practice-engine` exe and `Samples/HelloTriangle/`) ended up with ~10 scattered includes and 3-line `DEPS` clauses, plus a `target_include_directories` for ApplicationCore (which is dlopen-only). The user flagged the surface as confusing: "must import with PEngine name. Not Core or Application."

## Decision

A new `INTERFACE` library `Engine::PEngine` lives at `Engine/Source/Runtime/PEngine/`. It exposes a single header `<PEngine.h>` that re-includes every public engine header an external host can legitimately use:

- Core (logging, assert, types, paths, allocator, IEngineContext, module, module loader)
- RHI interface types
- ApplicationCore PAL headers (IPlatformApplication, IWindow, EKey) — **headers only; dlopen-only contract preserved**
- Renderer
- Launch (LaunchEngineLoop for one-call hosts)

`Engine::PEngine` links `Engine::Core`, `Engine::RHI`, `Engine::Renderer`, `Engine::Launch`. ApplicationCore's headers are added via `target_include_directories(... INTERFACE ...)` without linking, mirroring the pattern in `Launch/CMakeLists.txt` (the `.so` is dlopened, never linked).

External hosts now read:

```cpp
#include <PEngine.h>            // one include, one target
```

```cmake
target_link_libraries(MyHost PRIVATE Engine::PEngine)
add_dependencies(MyHost ApplicationCore VulkanRHI)   # dlopen-only build ordering
```

## Consequences

**Positive:**
- New samples have a 2-line CMake dependency declaration instead of 4+.
- Sample authors do not need to know which engine module owns which symbol.
- The umbrella surfaces all of `pe::` in one namespace, which mirrors how users would write `using namespace pe;` anyway.
- The dlopen contract is unchanged — `Engine::PEngine` deliberately does NOT add `Engine::ApplicationCore` or `Engine::VulkanRHI` to its link deps.

**Negative:**
- Compile times for samples may rise slightly because `<PEngine.h>` includes more than a curated narrow header would. Negligible for a project of this size; revisit if it ever bites.
- IDE go-to-definition still lands on the source `Core/Logging.h` etc., which is the right behavior but does mean the umbrella isn't a deep abstraction (it's a convenience).
- A future refactor (e.g. moving a header out of Core) requires updating `<PEngine.h>` too. Cheap.

## Alternatives considered

- **Status quo (no umbrella)** — rejected. The user explicitly called the scattered surface confusing.
- **Per-feature headers under `<PEngine/...>`** (`<PEngine/Logging.h>`, `<PEngine/Renderer.h>`) — rejected as duplication: the underlying headers already exist at `<Core/Logging.h>` etc. The umbrella is one file; per-feature forwarders would be many.
- **A separate `pengine::` namespace alias** — deferred. The user mentioned import-name confusion, not namespace confusion. The `pe::` namespace is short enough; if a future user asks for `pengine::` we can add a namespace alias trivially.

## References

- `Engine/Source/Runtime/PEngine/Public/PEngine.h`
- `Engine/Source/Runtime/PEngine/CMakeLists.txt`
- `Samples/HelloTriangle/Private/main.cpp` — first consumer
- ADR-0001, ADR-0002, ADR-0005 — the dlopen contracts that the umbrella deliberately preserves
