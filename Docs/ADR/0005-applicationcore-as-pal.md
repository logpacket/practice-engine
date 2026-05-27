# ADR-0005: ApplicationCore is a PAL with swappable backends; GLFW is a backend, not the interface

**Status:** Accepted (v3→v4 design pivot)

## Context

The original Stage 1 design exposed GLFW directly: external modules (Renderer, Launch) could `#include <GLFW/glfw3.h>` and call GLFW functions. The Architect review caught that this lock-in defeats the engine's stated portability goal — Stage 4 editor work needs Win32-native dialogs, Stage 6 needs macOS Cocoa, Stage 7 needs console SDKs, and each replacement would require rewriting every consumer of the windowing API.

Two alternatives:

- **Keep GLFW as the API**: simple, ergonomic, but ties every consumer to GLFW. Replacing GLFW = touching all consumers.
- **Introduce a PAL (Platform Abstraction Layer)**: `IPlatformApplication` + `IWindow` + `EKey` are the interface; GLFW is one of multiple implementations under `Private/<Backend>/`. External consumers see only the interface.

## Decision

`Engine/Source/Runtime/ApplicationCore/` exposes three public interfaces in `Public/ApplicationCore/`:

- `IPlatformApplication` — Initialize/Shutdown, CreateWindow/DestroyWindow, PumpEvents, GetRequiredVulkanInstanceExtensions, CreateVulkanSurface (the latter two were later renamed to `GetRequiredGraphicsInstanceExtensions(EGraphicsBackend)` / `CreateGraphicsSurface(EGraphicsBackend, ...)` to keep the PAL backend-neutral; see [ADR-0015](0015-pal-graphics-backend-abstraction.md))
- `IWindow` — ShouldClose, GetNativeWindowHandle, GetNativeDisplayHandle, GetWidth, GetHeight
- `EKey` enum + `FKeyEvent` — backend-neutral key codes (Stage 1 minimum: `Unknown`, `Escape`)

Backends live under `Private/<Backend>/`, one per directory. Stage 1 ships `Private/GLFW/`. CMake option `ENGINE_APP_BACKEND` selects which `Private/<Backend>/*.cpp` set compiles in. Stage 4 adds `Private/Win32/` (native dialogs for editor), Stage 6 adds `Private/Cocoa/`, Stage 7 adds console SDK backends — all as siblings, no consumer change.

External modules (Renderer, Launch, samples, VulkanRHI) MUST NOT include GLFW headers or link the GLFW library. CMake enforces this by keeping `glfw` as a `PRIVATE` dep of ApplicationCore.

## Consequences

**Positive:**
- Future platform support is purely additive (sibling backend directory).
- Editor (Stage 4) can demand native Win32 APIs without touching the rest of the engine.
- Console ports do not need to fork the whole windowing layer.
- The interface is small enough that swapping backends is feasible: ~30-50 LOC per IPlatformApplication impl.

**Negative:**
- Adds one indirection at the PAL boundary (virtual call per `PumpEvents`/`ShouldClose`/...). Negligible for any realistic frame rate.
- Native handles cross the boundary as `void*`. Renderer must trust that "this is the right kind of native handle for the active backend." Acceptable because exactly one backend exists per build.
- Stage 1 spent +1-2 days on the PAL split (estimated Stage 4-7 savings: 5-8 weeks).

## Alternatives considered

- **GLFW as the API** — rejected. Locks in a portability ceiling at Stage 4. Architect M-priority finding.
- **Out-of-process platform host (Win32 daemon, game in another process)** — rejected. Way out of scope for Stage 1, and not how either Unreal or Godot organize the platform layer.

## References

- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/IPlatformApplication.h`
- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/IWindow.h`
- `Engine/Source/Runtime/ApplicationCore/Public/ApplicationCore/PlatformKey.h`
- `Engine/Source/Runtime/ApplicationCore/Private/GLFW/` — Stage 1 backend
- `Engine/Source/Runtime/ApplicationCore/CMakeLists.txt` — `BACKEND_DIR ${ENGINE_APP_BACKEND}`
- Architecture.md §3.8 — PAL design
