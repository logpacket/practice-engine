# ADR-0019: Backend modules live in group directories (`Platforms/`, `RHIBackends/`)

**Status:** Accepted

## Context

Stage 1 ships two backend modules (`GLFWPlatform` for PAL, `VulkanRHI` for RHI). Stage 5-7 plans add at least five more — `D3D12RHI`, `MetalRHI`, `Win32Platform`, `WaylandPlatform`, `CocoaPlatform`, `ConsoleXPlatform`. Without grouping, `Engine/Source/Runtime/` ends up with 15+ flat entries, most of which are backend implementations with the rest being interface modules — flat trees lose the interface-vs-backend distinction at a glance.

Three layout options surfaced:

1. **Flat layout** — keep current. `GLFWPlatform/`, `VulkanRHI/`, `D3D12RHI/`, `Win32Platform/`, … all siblings of `Core/`, `RHI/`, `ApplicationCore/`. UE does this with 60+ modules.
2. **Nested under interface** — `RHI/Backends/Vulkan/`, `ApplicationCore/Backends/GLFW/`. Each backend nested inside the interface module's directory. Misleading for RHI backends because they are independent dlopen `.so`s, not "private" to RHI ([ADR-0002](0002-rhi-is-a-dynamically-loaded-module.md)).
3. **Group directories as siblings** — `Platforms/<Backend>/` + `RHIBackends/<Backend>/`, each grouping the same-kind backends.

## Decision

Option 3. The Stage 1 tree:

```
Engine/Source/Runtime/
├── ApplicationCore/         INTERFACE library (PAL contract)
├── Core/
├── Launch/
├── Platforms/               group: PAL backends (STATIC, configure-time link, ADR-0018)
│   ├── CMakeLists.txt           dispatches by ENGINE_APP_BACKEND
│   └── GLFW/
├── Renderer/
├── RHI/                     INTERFACE library (RHI contract)
├── RHIBackends/             group: RHI backends (SHARED, runtime dlopen, ADR-0002)
│   ├── CMakeLists.txt           add_subdirectory each backend
│   └── Vulkan/
└── Tests/
```

Future entries (Stage 5+) land in their group:

```
Platforms/
├── GLFW/             ─┐
├── Win32/             │ ENGINE_APP_BACKEND chooses one
├── Wayland/           │
├── Cocoa/             │
└── ConsoleX/         ─┘

RHIBackends/
├── Vulkan/           ─┐
├── D3D12/             │ all build that the OS supports;
├── Metal/             │ Launch picks one by name at runtime
└── ConsoleXGraphics/ ─┘
```

The group directory names encode the lifecycle:
- **`Platforms/`** — configure-time STATIC link (ADR-0018). One backend per build.
- **`RHIBackends/`** — runtime dlopen (ADR-0002). Multiple backends may coexist in one build.

## Consequences

**Positive:**
- `Engine/Source/Runtime/` stays human-scannable: 7 entries today, ~9 at Stage 7 (vs. 15+ flat).
- Group name signals lifecycle, so a reader can predict the linking rules of a new entry without opening its CMakeLists.
- Adding a backend = one subdirectory + (for Platforms/) one line in the group dispatch. No edits to the interface module.
- Group CMakeLists become the natural place for backend-selection logic (`ENGINE_APP_BACKEND` dispatch) or backend-wide configuration (e.g. future `RHIBackends/CMakeLists.txt` could enable validation layers across all RHI backends at once).
- Symmetric in shape between PAL and RHI, even though the inner mechanics (STATIC vs SHARED) differ — readers learn one layout pattern.

**Negative:**
- One extra directory level vs. flat — `Engine/Source/Runtime/Platforms/GLFW/Private/...` is deeper than `Engine/Source/Runtime/GLFWPlatform/Private/...`. Minor; IDE navigation unaffected.
- Module name (`GLFWPlatform`, `VulkanRHI`) and directory path (`Platforms/GLFW/`, `RHIBackends/Vulkan/`) diverge. `grep -r` for `GLFWPlatform` still finds CMakeLists; `find` for paths needs to know the layout.
- Group directories (`Platforms/`, `RHIBackends/`) contain a `CMakeLists.txt` but are not themselves CMake targets — they look like modules but aren't. Comments at the top of each group CMakeLists name them as "group directories" to remove ambiguity.

## Alternatives considered

- **Flat layout (UE style)** — rejected. UE accepts 60+ flat Runtime/ entries because UBT/IDE generators do most navigation; we are smaller and value the visual grouping for now. If the cost of one extra directory level ever bites, we can flatten.
- **Backend nested in interface** (`RHI/Backends/Vulkan/`, `ApplicationCore/Backends/GLFW/`) — partially right for PAL (where ADR-0018 makes backend genuinely an "implementation private to the interface in lifecycle terms") but wrong for RHI (backends are independently-loadable peers, not nested implementations). Asymmetric layout to model the asymmetric lifecycle would obscure the consistent "backend selection" pattern. Group-sibling layout (Option 3) gets the visual symmetry while letting CMake encode the lifecycle differences (STATIC vs SHARED) internally.
- **Naming `RHIs/` instead of `RHIBackends/`** — rejected as awkward (pluralizing an acronym reads badly).
- **Naming `GPUBackends/` instead of `RHIBackends/`** — rejected; RHI is the engine's abstraction, GPU is the hardware. The group holds RHI implementations, not GPU drivers.

## References

- `Engine/Source/Runtime/Platforms/CMakeLists.txt` — group + dispatch
- `Engine/Source/Runtime/RHIBackends/CMakeLists.txt` — group + add_subdirectory each backend
- `Engine/Source/Runtime/Platforms/GLFW/CMakeLists.txt` — Stage 1 PAL backend
- `Engine/Source/Runtime/RHIBackends/Vulkan/CMakeLists.txt` — Stage 1 RHI backend
- [ADR-0002](0002-rhi-is-a-dynamically-loaded-module.md) — RHI backends are dlopen modules (drives RHIBackends/ lifecycle)
- [ADR-0018](0018-configure-time-pal-static-link.md) — PAL backends are STATIC (drives Platforms/ lifecycle)
