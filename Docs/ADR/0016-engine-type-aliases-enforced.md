# ADR-0016: Engine type aliases (`uint32`, `int64`, `usize`, …) are enforced over raw stdint types

**Status:** Accepted

## Context

`Core/Public/Core/Types.h` defines the engine's canonical integer / float / size aliases inside `namespace pe`:

```cpp
using int8/16/32/64   = std::int8/16/32/64_t;
using uint8/16/32/64  = std::uint8/16/32/64_t;
using usize           = std::size_t;
using isize           = std::ptrdiff_t;
using float32         = float;
using float64         = double;
using FString         = std::string;
using FStringView     = std::string_view;
```

Despite the file existing since §6.b, much of the codebase used raw `std::uint32_t` / `uint64_t` / `size_t` directly. The two spellings drifted file by file: `RHITypes.h` was full of `uint32_t`, `Renderer.cpp` mixed both, samples used whatever the underlying C library returned. The cost was inconsistency at every API surface (`uint32_t image_index` returned from RHI, then promptly assigned to a `pe::uint32` variable two lines later) and obscured signal at backend boundaries: when a function genuinely had to interop with Vulkan/GLFW (which use raw `uint32_t`), that fact was no longer visually distinct from incidental stdint usage.

A framework codebase should have one canonical name for its domain types. The aliases were *available*; they were not *enforced*.

## Decision

Two changes:

**1. Forbid raw `(std::)?u?int(8|16|32|64)_t` / `size_t` / `ptrdiff_t` in non-bridge engine code.** The canonical spelling for any integer or size in `pe::` is the alias (`uint32`, `int64`, `usize`, …). Stdlib container indexing uses `usize` (new; same as `std::size_t`).

**2. Add a build-time gate (G6) that fails the build on any violation.** The gate (`CMake/check_engine_type_aliases.sh`) is the first step of `cmake --build`, so a violation aborts before any compilation. It scans `Engine/Source/Runtime/` and `Samples/` for the banned patterns; it allowlists a small, named set of bridge files where the raw stdint types are part of an external API contract:

| Allowlisted file | Why |
|---|---|
| `Core/Public/Core/Types.h` | Defines the aliases via `std::int*_t` |
| `Core/Public/Core/EngineAbi.hpp` | C ABI surface — POD value types use `std::*_t` deliberately |
| `Core/Public/Core/MallocAllocator.h` + `.cpp` | Overrides `IEngineAllocator::Allocate(std::uint64_t, std::uint32_t)` — signature must match |
| `Core/Private/Paths.cpp` | Win32 `GetModuleFileNameW` etc. use platform types |
| `Core/Private/ModuleLoader.cpp` | dlopen / LoadLibrary takes platform types |
| `VulkanRHI/Private/Vulkan*.{h,cpp}` | Vulkan API uses `uint32_t`, `VkBool32`, etc. throughout |
| `ApplicationCore/Private/GLFW/*.{h,cpp}` | GLFW API uses `uint32_t` for `glfwGetRequiredInstanceExtensions` etc. |
| `*PCH.h` | PCH headers pull in `<cstdint>` directly |
| `*/Generated/*` | CMake-generated export headers |

A future violation in any other file fails the build with a precise line-and-column error message and the canonical fix ("use `uint32` instead of `uint32_t`").

## Consequences

**Positive:**
- One canonical spelling, audit-on-write. Reviewers do not have to remember to flag `uint32_t` in PRs.
- Backend bridges (Vulkan, GLFW, OS calls) become visually distinct: only those files contain raw stdint types, so reading "this file uses `uint32_t`" tells you "we are touching an external API here".
- Type-aliasing decisions (e.g. promoting `FString` away from `std::string`) become single-find-and-replace operations because every callsite uses the alias.

**Negative:**
- Adding a new external-API bridge requires a one-line allowlist update to the gate script. Cheap; documented in the script itself.
- The gate is a bash script (not portable to Windows native build hosts). The `if(NOT WIN32)` guard in the root CMakeLists skips it on Windows; the Linux/macOS-side CI catches everything. Stage 4+ may rewrite as a CMake-native check (`file(STRINGS ... REGEX ...)`) for cross-platform parity.
- Slight inconvenience when copy-pasting code from external sources (Vulkan tutorials, GLFW examples) — the engine aliases must be applied before commit.

## Alternatives considered

- **clang-tidy custom check** — rejected. Writing a Clang AST matcher is heavier than the grep-based gate; the gate covers the use case at near-zero infrastructure cost.
- **Just-rename-them-by-hand-and-trust-discipline** — rejected. The user explicitly asked for *enforcement*. Drift recurs without a build-fail loop.
- **Allow stdint everywhere; treat the aliases as optional** — rejected. The previous status quo. Produced the inconsistency this ADR resolves.
- **Provide aliases but enforce only on `Public/` headers** — partial; rejected for the same reason as the third alternative. Stage 1 has too few files for tiered enforcement to matter.

## References

- `Engine/Source/Runtime/Core/Public/Core/Types.h` — the alias definitions, now including `usize` / `isize`
- `CMake/check_engine_type_aliases.sh` — G6 gate script
- `CMakeLists.txt` — `add_custom_target(check_engine_type_aliases ALL ...)` wires the gate as build step
- [ADR-0007](0007-abi-strict-guards-staged-promotion.md) — broader ABI strict-guard plan, of which type aliases are one component
