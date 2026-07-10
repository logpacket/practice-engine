cmake_minimum_required(VERSION 3.26)

project(practice-engine
    VERSION   0.1.0
    LANGUAGES CXX C)

# ============================================================================
# Toolchain
# ============================================================================
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

# Default build type for single-config generators (make/ninja)
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT CMAKE_BUILD_TYPE AND NOT IS_MULTI_CONFIG)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Windows: force every module onto the same CRT (M5-adjacent - ABI consistency)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ============================================================================
# Options
# ============================================================================
option(ENGINE_BUILD_TESTS        "Build tests (DummyModule, dummy_module_smoke)" ON)
option(ENGINE_WARNINGS_AS_ERRORS "Treat warnings as errors"                      ON)
option(ENGINE_VULKAN_VALIDATION  "Enable Vulkan validation layer in Debug"       ON)

# ApplicationCore PAL backends are configure-time STATIC-linked (ADR-0018,
# supersedes ADR-0017). One backend per build; Launch links the
# Engine::Platform ALIAS that the selected backend provides. Backends live
# grouped under Engine/Source/Runtime/Platforms/<Backend>/ (ADR-0019).
set(ENGINE_APP_BACKEND "GLFW" CACHE STRING "PAL backend: which Platforms/<Backend>/ to link")
set_property(CACHE ENGINE_APP_BACKEND PROPERTY STRINGS GLFW)

# ============================================================================
# Output dirs (Binaries/<Platform>/<Config>/)
# ============================================================================
include(CMake/Platform.cmake)
set(ENGINE_BINARIES_ROOT "${CMAKE_SOURCE_DIR}/Binaries/${ENGINE_PLATFORM_DIR}")

if(IS_MULTI_CONFIG)
    foreach(CFG IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${CFG} CFG_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} ${ENGINE_BINARIES_ROOT}/${CFG})
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} ${ENGINE_BINARIES_ROOT}/${CFG})
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} ${ENGINE_BINARIES_ROOT}/${CFG}/lib)
    endforeach()
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${ENGINE_BINARIES_ROOT}/${CMAKE_BUILD_TYPE})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${ENGINE_BINARIES_ROOT}/${CMAKE_BUILD_TYPE})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${ENGINE_BINARIES_ROOT}/${CMAKE_BUILD_TYPE}/lib)
endif()

# ============================================================================
# Helpers / third-party
# ============================================================================
include(CMake/CompilerWarnings.cmake)
include(CMake/EngineModule.cmake)
include(CMake/ThirdPartyDeps.cmake)

# ============================================================================
# Modules (§6.b: Core + Tests baseline registered)
# Subsequent stages add an add_subdirectory entry per checklist item.
# ============================================================================
# ============================================================================
# G6 gate: enforces engine type-alias usage (Core/Types.h) over raw stdint
# types in non-bridge engine code. Runs once per configure-or-build cycle.
# See CMake/check_engine_type_aliases.sh + ADR-0016.
# ============================================================================
if(NOT WIN32)
    add_custom_target(check_engine_type_aliases ALL
        COMMAND ${CMAKE_COMMAND} -E echo "[G6] Checking engine type-alias usage..."
        COMMAND ${CMAKE_SOURCE_DIR}/CMake/check_engine_type_aliases.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "G6: engine code must use Core/Types.h aliases"
        VERBATIM)
endif()

add_subdirectory(Engine/Source/Runtime/Core)                 # §6.b
add_subdirectory(Engine/Source/Runtime/RHI)                  # §6.c (interface)
add_subdirectory(Engine/Source/Runtime/RHIBackends)          # §6.c (Vulkan; ADR-0019)
add_subdirectory(Engine/Source/Runtime/ApplicationCore)      # §6.d (interface)
add_subdirectory(Engine/Source/Runtime/Platforms)            # §6.d (GLFW; ADR-0018/0019)
add_subdirectory(Engine/Source/Runtime/Renderer)             # §6.e
add_subdirectory(Engine/Source/Runtime/Launch)               # §6.f

if(ENGINE_BUILD_TESTS)
    add_subdirectory(Engine/Source/Runtime/Tests/DummyModule)
    add_subdirectory(Engine/Source/Runtime/Tests/dummy_module_smoke)
    add_subdirectory(Engine/Source/Runtime/Tests/rhi_smoke)
    add_subdirectory(Engine/Source/Runtime/Tests/rhi_pool_smoke)   # Stage 2 §6.a
    add_subdirectory(Engine/Source/Runtime/Tests/app_smoke)
endif()

# Samples - external-facing example apps that demonstrate engine usage.
if(EXISTS ${CMAKE_SOURCE_DIR}/Samples/CMakeLists.txt)
    add_subdirectory(Samples)
endif()

# ============================================================================
# Reporting
# ============================================================================
message(STATUS "")
message(STATUS "==== practice-engine ${PROJECT_VERSION} ====")
message(STATUS "  Platform:           ${ENGINE_PLATFORM_DIR}")
message(STATUS "  C++ standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "  Generator:          ${CMAKE_GENERATOR}")
if(NOT IS_MULTI_CONFIG)
    message(STATUS "  Build type:         ${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "  Binaries:           ${ENGINE_BINARIES_ROOT}")
message(STATUS "  PAL backend:        ${ENGINE_APP_BACKEND} (configure-time STATIC; ADR-0018)")
message(STATUS "  Warnings as errors: ${ENGINE_WARNINGS_AS_ERRORS}")
message(STATUS "  Vulkan validation:  ${ENGINE_VULKAN_VALIDATION}")
message(STATUS "  Build tests:        ${ENGINE_BUILD_TESTS}")
message(STATUS "")
