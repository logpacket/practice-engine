# Samples

Self-contained applications that demonstrate how to use the practice-engine modules.

## Building & running

Samples are built whenever you build the project:

```bash
cmake --build --preset linux-debug -j
./Binaries/Linux/Debug/HelloTriangle
```

Each sample is a separate executable in `Binaries/<Platform>/<Config>/`, alongside `practice-engine` and the test executables.

## Available samples

| Sample | What it shows |
|---|---|
| [HelloTriangle](HelloTriangle/Private/main.cpp) | Full inline bootstrap: loads ApplicationCore + VulkanRHI as dynamic modules, opens a window, creates a `FRenderer`, draws a colored triangle. Mirrors `LaunchEngineLoop` so it doubles as a copy-paste starting point for new hosts. |

## Adding a sample

```
Samples/
└── YourSample/
    ├── CMakeLists.txt
    └── Private/
        └── main.cpp
```

`Samples/YourSample/CMakeLists.txt`:

```cmake
add_engine_executable(
    NAME YourSample
    DEPS Engine::Core Engine::RHI Engine::Renderer
)

# ApplicationCore is dlopen-only - headers without link.
target_include_directories(YourSample PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/ApplicationCore/Public)

add_dependencies(YourSample ApplicationCore VulkanRHI)
```

Then add one line to `Samples/CMakeLists.txt`:

```cmake
add_subdirectory(YourSample)
```

## When to write a sample vs an engine test

- **Test** (under `Engine/Source/Runtime/Tests/`): exercises one engine module's contract, runs in CI, fails the build on regression. Examples: `dummy_module_smoke`, `rhi_smoke`, `app_smoke`.
- **Sample** (here): shows users how to *use* the engine end-to-end. Not run in CI. Allowed to be interactive (open a window, wait for input).

## Isolation rules (same as engine code)

Samples are subject to the Stage 1 design's isolation gates:
- MUST NOT link `VulkanRHI` or `ApplicationCore` at link time — both are runtime-loaded via `pe::ModuleLoader`.
- MUST NOT include Vulkan or GLFW headers directly — go through the RHI and PAL interfaces.

The HelloTriangle CMakeLists demonstrates the correct pattern.
