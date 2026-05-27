# practice-engine

C++20 modular game engine built on Vulkan. Unreal-style dynamic module loading, strict RAII, swappable RHI backend.

## Stage 1 status

A colored triangle is rendered into a window through a runtime `dlopen`-ed `VulkanRHI` module, while the Renderer module reaches no Vulkan symbol or header.

Documentation:
- [`Docs/Architecture.md`](Docs/Architecture.md) — goal architecture (what the engine looks like at maturity)
- [`Docs/Stages/`](Docs/Stages/) — per-stage implementation plans (start with [`Stage1.md`](Docs/Stages/Stage1.md))
- [`Docs/ADR/`](Docs/ADR/) — architecture decision records (why each choice was made)
- [`BUILDING.md`](BUILDING.md) — build environment assumptions and commands

## Build (quick)

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug -j
./Binaries/Linux/Debug/HelloTriangle
```

The `HelloTriangle` executable lives in [`Samples/HelloTriangle/`](Samples/) and is the canonical demonstration host - it bootstraps the engine inline so its 140-line `main.cpp` doubles as a copy-paste starting point for new hosts.

See `BUILDING.md` for detailed environment requirements (Vulkan SDK >= 1.3.250 etc.) and `Samples/README.md` for the sample-authoring template.

## Directory guide

- `Engine/Source/Runtime/` - modules that ship in the game (Core, RHI, VulkanRHI, ApplicationCore, Renderer, Launch, Tests)
- `Engine/Shaders/Private/` - shader sources (.glsl)
- `Samples/` - example apps that exercise the engine (`HelloTriangle/` is the first)
- `CMake/` - shared CMake scripts
- `Docs/` - design documents
- `Binaries/<Platform>/<Config>/` - build outputs (executables and .so/.dll colocated)

## Using the engine from your own host

This is a framework, not a library. Hosts include each engine module explicitly so the dependency on each module is visible in the source file, then opt into a precompiled-header set for build speed.

```cpp
#include <Core/Logging.h>
#include <Core/Module.h>
#include <Core/ModuleLoader.h>
#include <Launch/LaunchEngineLoop.h>

int main(int argc, char** argv) {
    // The one-liner host:
    return pe::LaunchEngineLoop(argc, argv);

    // ...or do the inline bootstrap yourself - see Samples/HelloTriangle/Private/main.cpp.
}
```

CMake side:

```cmake
add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE
    Engine::Core Engine::RHI Engine::Renderer Engine::Launch)

# ApplicationCore is dlopen-only - headers without link.
target_include_directories(MyApp PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/ApplicationCore/Public)

add_dependencies(MyApp ApplicationCore VulkanRHI)   # dlopen-only deps

# Optional: opt into your own PCH for compile speed.
# target_precompile_headers(MyApp PRIVATE MyPCH.h)
```

See [`Docs/ADR/0014-framework-philosophy-and-per-module-pch.md`](Docs/ADR/0014-framework-philosophy-and-per-module-pch.md) for why per-module includes + per-module PCH is preferred over a single umbrella header.

## License

See `LICENSE` for the repository license. Third-party dependencies (volk, GLFW, spdlog, glm) follow their own licenses.
