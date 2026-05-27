# Building practice-engine

Build requirements and commands based on the §5.6 M5 environment assumptions in the design doc.

## Required environment

| Item | Version | Note |
|---|---|---|
| CMake | >= 3.26 | Required to guarantee `FindVulkan` `COMPONENTS glslc` support |
| C++ compiler | C++20 (GCC >= 12 / Clang >= 15 / MSVC >= 19.36 / Visual Studio 2022 17.6+) | concepts/ranges |
| Vulkan SDK | >= 1.3.250 (LunarG) | `VULKAN_SDK` env var required; validation layer + glslc included |
| GPU driver | Vulkan 1.3 + `VK_KHR_dynamic_rendering` (core in 1.3) + unified graphics/present queue | Latest NVIDIA/AMD/Intel drivers / Linux Mesa >= 23.3 / lavapipe >= 23.3 |

No environment-specific fallback shims are introduced (design §5.6 M5). Anything outside the range above fails clearly at configure or runtime, prompting the user to fix the environment.

## Verifying the Vulkan SDK installation

```bash
# All OSes
echo $VULKAN_SDK
glslc --version
vulkaninfo --summary | head -20
```

All three commands must succeed. Confirm that `glslc --version` matches the Vulkan SDK release.

### Linux (Ubuntu/Debian)

```bash
# Recommended: official LunarG apt repo
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-noble.list \
    https://packages.lunarg.com/vulkan/lunarg-vulkan-noble.list
sudo apt update && sudo apt install vulkan-sdk
```

The apt install drops binaries into `/usr/bin/` and headers into `/usr/include/vulkan/`; no `setup-env.sh` is shipped (that script is only in the tarball SDK). CMake's `find_package(Vulkan)` discovers them through standard paths. Setting `VULKAN_SDK` is **optional** for the apt layout; if you want it for tooling, `export VULKAN_SDK=/usr` works.

### Windows

- Run the [LunarG Vulkan SDK installer](https://vulkan.lunarg.com/sdk/home#windows) (it sets `VULKAN_SDK` automatically).
- Install the "Desktop development with C++" workload in Visual Studio 2022 17.6+.

## Build commands

### Linux

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug -j
./Binaries/Linux/Debug/HelloTriangle
```

### Windows (Developer Command Prompt for VS 2022)

```powershell
cmake --preset win64-debug
cmake --build --preset win64-debug --config Debug
.\Binaries\Win64\Debug\HelloTriangle.exe
```

## Stage 1 verification gates (G1-G5)

Run the following after a successful build to confirm Stage 1 acceptance:

```bash
# G1: VulkanRHI is NOT a link-time dependency of the host executable
ldd Binaries/Linux/Debug/HelloTriangle | grep -qi vulkanrhi && echo G1-FAIL || echo G1-OK

# G2: Renderer binary contains no Vulkan symbols
nm -D Binaries/Linux/Debug/libRenderer.so | awk '$2=="T" || $2=="U"' | grep -qE ' vk[A-Z]' && echo G2-FAIL || echo G2-OK

# G3: Renderer sources do not reference Vulkan headers
./CMake/check_no_vulkan_includes.sh

# G4: zero validation ERROR after a normal run
grep -E '\[VK_ERROR\]' Saved/Logs/engine.log

# G5: module loader regression
./Binaries/Linux/Debug/dummy_module_smoke && echo G5-OK || echo G5-FAIL
```

See [`Docs/Stages/Stage1.md`](Docs/Stages/Stage1.md) for the full gate criteria and [`Docs/ADR/`](Docs/ADR/) for the rationale behind each decision.

## Troubleshooting

| Symptom | Cause / Fix |
|---|---|
| CMake configure fails: `Could NOT find Vulkan` | `VULKAN_SDK` not set. Install the SDK, then restart the shell or `source /usr/share/vulkan/setup-env.sh` |
| `Vulkan::glslc` target missing | CMake < 3.21. This project requires >= 3.26 |
| `vkCreateInstance` fails at runtime | GPU driver does not support Vulkan 1.3. Update the driver or use lavapipe |
| FATAL `Vulkan 1.3 dynamic rendering required` | Driver does not expose `dynamicRendering`. Update to a current driver |
| No window appears on Linux | Wayland/X11 libraries missing. `sudo apt install libxkbcommon-dev xorg-dev` |
