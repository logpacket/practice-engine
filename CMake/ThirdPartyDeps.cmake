# CMake/ThirdPartyDeps.cmake
#
# Single entry point for every third-party dependency. Module CMakeLists must
# never call FetchContent_Declare directly; they only use IMPORTED/ALIAS
# targets defined here.
#
# Stage 1 deps: Vulkan SDK (system), volk, glfw, spdlog, glm.
# VMA and GoogleTest are Stage 2+.

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# --- Vulkan SDK (system-installed) -----------------------------------------
# M5 assumption: Vulkan SDK >= 1.3.250, VULKAN_SDK env var set, glslc included.
find_package(Vulkan REQUIRED COMPONENTS glslc)
message(STATUS "Vulkan: ${Vulkan_VERSION} (glslc: ${Vulkan_GLSLC_EXECUTABLE})")

# --- volk (Vulkan meta-loader) ---------------------------------------------
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.3.295
    GIT_SHALLOW    TRUE)
# volk needs Vulkan headers; set the option before MakeAvailable.
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)

# --- GLFW -------------------------------------------------------------------
# Prefer the system package; fetch if not present.
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "glfw3 system package not found; using FetchContent")
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
endif()

# --- spdlog -----------------------------------------------------------------
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE)

# --- glm --------------------------------------------------------------------
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# --- Materialize everything -------------------------------------------------
if(NOT glfw3_FOUND)
    FetchContent_MakeAvailable(volk glfw spdlog glm)
else()
    FetchContent_MakeAvailable(volk spdlog glm)
endif()
