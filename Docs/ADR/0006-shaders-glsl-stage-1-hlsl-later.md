# ADR-0006: Shaders use GLSL+glslc for Stage 1; HLSL+DXC deferred to Stage 3+

**Status:** Accepted (v1→v2 contradiction resolution)

## Context

The v1 design tried to commit to HLSL+DXC at Stage 1 and treat ShaderCompiler as a runtime module. The Critic review found a 4-way contradiction: the design listed HLSL in §2.5/§3.7 while the directory tree and `§6.e` checklist listed `Triangle.{vert,frag}.glsl`, and the IShaderCompiler module had no caller in Stage 1.

Two paths to resolve:

- **Commit to HLSL+DXC now**: build the IShaderCompiler module, ship DXC at build time, rewrite triangle shaders as HLSL. Real work, no Stage 1 caller for the dynamic compiler.
- **GLSL+glslc for Stage 1, defer HLSL**: stage-build .glsl → .spv via `add_custom_command(glslc)` (build-time, not runtime). HLSL adoption moves to Stage 3+ when ShaderCompiler module + multiple shaders + D3D12 backend (Stage 5) all need it.

## Decision

Stage 1 ships triangle shaders as **GLSL**, compiled at build time by `glslc` via CMake `add_custom_command`. No runtime shader compiler module. The `IShaderCompiler` interface, `ShaderCompiler` module, and `Programs/ShaderCompiler/` directory are removed from Stage 1.

HLSL+DXC is reconsidered at Stage 3 — that is the earliest point where (a) we have a real corpus of shaders to convert, (b) the D3D12 backend is on the horizon making single-language authoring valuable, and (c) the cooked-asset pipeline needs a shipping shader format anyway.

The compiled .spv files land in `Binaries/<Platform>/<Config>/Shaders/`. `pe::FPaths::ShadersDir()` resolves to that path so the Renderer loads them with no additional configuration.

## Consequences

**Positive:**
- Stage 1 builds with the Vulkan SDK's bundled `glslc`. Zero extra dependencies.
- Compile errors surface at build time, not at first frame.
- The `-fshader-stage=<stage>` flag is derived from the filename pattern (`*.vert.glsl` → vert) so authoring a new shader is "drop a `.glsl` in `Engine/Shaders/Private/` and rebuild".

**Negative:**
- When D3D12 arrives, all shaders must be rewritten in HLSL or run through a GLSL→HLSL translator. The Stage 1 triangle is 2 small files, so this cost is real but bounded.
- We will not have native runtime shader compilation in Stage 1-2, ruling out e.g. user-supplied shader strings. Acceptable; that's a Stage 3+ feature.

## Alternatives considered

- **Ship IShaderCompiler at Stage 1** — rejected. No caller exists; the interface would be locked in untested. Original design contradiction.
- **HLSL+DXC at Stage 1 with build-time DXC** — rejected. Real work for no current benefit; HLSL `[[vk::*]]` attributes add Vulkan-specific noise to shaders before D3D12 even exists.

## References

- `Engine/Shaders/Private/Triangle.vert.glsl`, `Triangle.frag.glsl`
- `Engine/Source/Runtime/Renderer/CMakeLists.txt` — `add_custom_command(glslc -fshader-stage=... )`
- `Engine/Source/Runtime/Core/Public/Core/Paths.h` — `FPaths::ShadersDir()`
- Architecture.md §3.7 — Stage 1 single-language policy
