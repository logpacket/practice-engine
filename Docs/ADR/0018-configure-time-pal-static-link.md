# ADR-0018: PAL backends — configure-time STATIC link with interface/implementation separation

**Status:** Accepted (supersedes [ADR-0017](0017-runtime-loadable-pal-backend.md); revises [ADR-0005](0005-applicationcore-as-pal.md))

## Context

[ADR-0005](0005-applicationcore-as-pal.md) introduced the PAL but conflated "interface separation" with "backend selection mechanism". The Stage 1 implementation kept the GLFW backend nested inside the `ApplicationCore` module, selected at CMake configure time via a `BACKEND_DIR` parameter. That mixed two concerns:

1. **Interface vs. implementation separation** — the headers `IPlatformApplication.h` / `IWindow.h` / `EKey` should live separately from any backend's implementation so future backends do not edit the interface module's CMakeLists.
2. **Backend selection mechanism** — whether the backend is chosen at CMake configure time (one backend per build) or at runtime (`pe::ModuleLoader::LoadModule(...)`).

[ADR-0017](0017-runtime-loadable-pal-backend.md) briefly pivoted to runtime selection for symmetry with the RHI but was reverted because the five PAL-specific constraints (cross-OS compile impossibility, boot order, console NDA, no shipping use case, PAL ≠ RHI domain) outweighed symmetry.

This ADR settles both questions: **separate interface from implementation, choose at configure time, link STATIC**.

## Decision

### Structure

- `Engine/Source/Runtime/ApplicationCore/` — **INTERFACE** library. Holds only the public PAL headers (`IPlatformApplication.h`, `IWindow.h`, `PlatformKey.h`) plus `PlatformBackend.h` declaring the configure-time factory:
  ```cpp
  namespace pe {
      IPlatformApplication* CreatePlatformApplication();
      void                  DestroyPlatformApplication(IPlatformApplication*);
  }
  ```
- `Engine/Source/Runtime/Platforms/<Backend>/` — one **STATIC** library per backend. Each provides the definitions of the two factory functions (with `extern` to a backend-specific concrete `FXxxApplication`). See [ADR-0019](0019-backend-group-directories.md) for the directory grouping.
- `Engine/Source/Runtime/Platforms/CMakeLists.txt` — group entry point. Reads `ENGINE_APP_BACKEND` (CMake cache var) and `add_subdirectory()`s exactly one backend.
- The selected backend registers an `Engine::Platform` ALIAS target so hosts (Launch, Samples) link backend-agnostic.

### Boot flow

```cpp
// Host (Launch or sample) writes:
IPlatformApplication* pa = pe::CreatePlatformApplication();
// ...use pa...
pa->Shutdown();
pe::DestroyPlatformApplication(pa);
```

No `dlopen`, no `IModule`, no `QueryInterface`. The linker resolves `CreatePlatformApplication` to whichever backend's STATIC library is in the link line.

### CMake selection

```cmake
set(ENGINE_APP_BACKEND "GLFW" CACHE STRING "...")
set_property(CACHE ENGINE_APP_BACKEND PROPERTY STRINGS GLFW)
# Future: STRINGS GLFW Win32 Wayland Cocoa ConsoleX
```

Adding a backend = drop in `Platforms/<Backend>/`, register in `Platforms/CMakeLists.txt` conditional dispatch, optionally guard with `if(WIN32)` / `if(APPLE)` so the wrong OS doesn't try to compile it.

## Consequences

**Positive:**
- One CMake-time decision, deterministic build artifact (no two-step "build + pick backend at boot").
- No runtime overhead for backend selection (no `dlopen`, no `QueryInterface`).
- Matches what UE does (Public/{Windows,Mac,Linux,IOS,Android}/ inside ApplicationCore), translated to our flatter module layout.
- The `Engine::Platform` alias keeps hosts ignorant of which backend ships — same code links any of GLFW/Win32/Cocoa/Console.
- Stage 7 console SDKs (NDA-locked) can ship as a closed Platforms/ subdirectory without exposing source to anyone but licensees.
- PAL/RHI asymmetry is now principled, not accidental: PAL = configure-time (no runtime use case + cross-OS compile constraint), RHI = runtime (multiple coexisting backends on one OS, real selection use case). [ADR-0002](0002-rhi-is-a-dynamically-loaded-module.md) and ADR-0018 together codify the asymmetry.

**Negative:**
- Two ABI patterns coexist in the engine: PAL backend uses a free function factory, RHI backend uses `IModule` + `QueryInterface`. Readers must learn both. Acceptable: each is the right tool for its domain.
- The `Engine::Platform` alias has to be added by every Platforms/<Backend>/CMakeLists.txt; it's an easy thing to forget. Mitigated by a comment in the group `CMakeLists.txt` + (future) lint check.
- Cross-platform development that wants to test a second OS's backend requires a cross-compile rather than runtime switch. Same as UE; acceptable cost.

## Alternatives considered

- **Runtime-loadable PAL** (ADR-0017) — rejected. Five PAL-specific constraints outweigh the symmetry argument; see ADR-0017 for full reasoning.
- **Keep ADR-0005's `BACKEND_DIR` (nested backend inside ApplicationCore)** — rejected as failing the symmetry-with-RHI test for *directory layout*: RHI backends sit as siblings of the RHI interface; PAL backends should too. ADR-0019 groups them under `Platforms/`.
- **`IModule` for STATIC backends** — overkill. `IModule` exists for the dlopen lifecycle (Init/Shutdown of a loaded `.so`); for a STATIC backend the loaded-module concept is meaningless. Free-function factory is the minimal correct surface.
- **Single `Engine::Platform` always, no backend selection** — would force a single backend forever. Not viable.

## References

- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/PlatformBackend.h` — the factory declaration
- `Engine/Source/Runtime/Platforms/CMakeLists.txt` — group + selection logic
- `Engine/Source/Runtime/Platforms/GLFW/` — Stage 1 backend (defines the factory functions and the `Engine::Platform` alias)
- `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` — boot site
- `Samples/HelloTriangle/Private/main.cpp` — sample call site
- [ADR-0005](0005-applicationcore-as-pal.md) — original PAL decision (this revises it)
- [ADR-0017](0017-runtime-loadable-pal-backend.md) — the runtime-load detour (superseded)
- [ADR-0019](0019-backend-group-directories.md) — the group-directory layout (Platforms/, RHIBackends/)
