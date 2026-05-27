# ADR-0014: Framework philosophy + per-module PCH

**Status:** Accepted (supersedes [ADR-0010](0010-pengine-umbrella-module.md))

## Context

[ADR-0010](0010-pengine-umbrella-module.md) introduced a `PEngine` INTERFACE module whose only purpose was a single `<PEngine.h>` umbrella header. The intent was to make samples easier to author by replacing ~10 scattered `#include <Core/...>`/`<RHI/...>`/`<ApplicationCore/...>`/`<Renderer/...>` lines with one `#include <PEngine.h>`.

After living with the umbrella for a single sample (`HelloTriangle`), the conceptual problem surfaced: an umbrella header treats the engine as a *library* that you assemble from outside. The engine is intended to be a *framework* — users embrace its conventions, not consume it as a polite library dependency. Conflating the two led to:

- A header (`<PEngine.h>`) that exists only as a textual convenience, not a real abstraction.
- Sample code that read like generic C++ ("import the library") instead of framework code ("you are inside the engine's world").
- A redundant CMake target (`Engine::PEngine`) whose deps were just `Core` + `RHI` + `Renderer` + `Launch` re-spelled, with awkward special-casing for the dlopen-only ApplicationCore.
- No build-speed benefit, despite "one include" reading like a compilation accelerator.

The user's request that triggered this ADR: *"we need PCH (Precompiled Header) pattern and just use 'Core/XXX.h'. we build framework. not library."*

## Decision

Two changes, taken together:

**1. Delete `PEngine` module.** The umbrella header is gone. Samples, the Launch bootstrap, and any external host all use **explicit per-module includes**: `<Core/Logging.h>`, `<RHI/IRHIDevice.h>`, `<ApplicationCore/IPlatformApplication.h>`, `<Renderer/Renderer.h>`, etc. Each include line tells the reader exactly which engine module owns the symbol on the next line.

**2. Add a per-module PCH.** `add_engine_module()` accepts a `PCH_HEADER <path>` parameter. CMake applies it via `target_precompile_headers(<target> PRIVATE <path>)`, so every `.cpp` in the module gets the listed header pre-prepended at compile time. Module PCHs included so far:

| Module | PCH | Pre-includes |
|---|---|---|
| Core | `Public/Core/CorePCH.h` | Own headers + stdlib hot-spots |
| Renderer | `Private/RendererPCH.h` | `<Core/CorePCH.h>` + RHI interfaces + IWindow |
| VulkanRHI | `Private/VulkanRHIPCH.h` | `<Core/CorePCH.h>` + volk + RHI interfaces + internal helpers |
| ApplicationCore | `Private/ApplicationCorePCH.h` | `<Core/CorePCH.h>` + PAL public surface (no backend headers) |
| Launch | `Private/LaunchPCH.h` | `<Core/CorePCH.h>` + RHI + PAL + Renderer + Launch's own headers |

`Core/CorePCH.h` is the only PCH placed in `Public/` because downstream PCH headers `#include` it. The rest live in `Private/` because no consumer should reach for them.

The RHI INTERFACE library has no `.cpp` and therefore no PCH. Tests inherit no PCH by default; if a test grows large enough to benefit, it can opt in by passing `PCH_HEADER` to `add_engine_executable` (a future small extension).

## Consequences

**Positive:**
- Sample code reads like framework code: each include is a deliberate statement of which module is being consumed.
- Compile time for engine modules drops noticeably — Core's PCH alone pre-parses spdlog (~6k LOC) once per module, not once per `.cpp`.
- The build system carries the umbrella concept (pre-include set) without the codebase having to express it as a header that pollutes the public surface.
- Backend swaps (Stage 4 Win32, Stage 6 Cocoa) do not invalidate the ApplicationCore PCH because the PCH deliberately excludes backend-specific headers.
- One CMake target removed (`Engine::PEngine`); one Public header removed (`<PEngine.h>`); one ADR superseded. Net surface reduction.

**Negative:**
- External hosts (samples, future game projects) write more `#include` lines per file than they would with an umbrella. Acceptable: the user explicitly preferred this. Sample authors can add their own PCH in their own `CMakeLists.txt` if a sample gets large.
- PCH headers add ~5 new files to maintain. Each is short and rarely changes.
- Build cache thrashes if a PCH header changes (everything in that module recompiles). Mitigated by keeping PCH content scoped to genuinely stable engine surfaces.

## Alternatives considered

- **Keep `PEngine` as an opt-in PCH provider for external hosts.** Rejected. Two ways to consume the engine (umbrella + per-module) is worse than one. The framework-style answer is unambiguous: per-module everywhere, PCH provides the speed-up.
- **One mega-PCH `EnginePCH.h` shared across every module.** Rejected. A change in any module's surface would invalidate everyone's PCH. Per-module PCH keeps invalidation local.
- **Auto-injected PCH that includes everything** (UE-style — sources do not even `#include` their own helpers). Rejected as overreach for this codebase's scale. Explicit `#include` lines stay valuable for IWYU clarity.

## References

- `CMake/EngineModule.cmake` — `PCH_HEADER` parameter
- `Engine/Source/Runtime/Core/Public/Core/CorePCH.h` — only Public PCH; downstream PCHs include it
- `Engine/Source/Runtime/{Renderer,VulkanRHI,ApplicationCore,Launch}/Private/*PCH.h` — per-module private PCHs
- `Samples/HelloTriangle/Private/main.cpp` — explicit per-module includes, framework-style
- [ADR-0010](0010-pengine-umbrella-module.md) — the superseded umbrella decision
