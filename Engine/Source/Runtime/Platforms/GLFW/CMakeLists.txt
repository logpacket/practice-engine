# GLFWPlatform - configure-time-selected PAL backend backed by GLFW.
#
# STATIC library, linked directly by the host. Selected at CMake configure time
# via ENGINE_APP_BACKEND - the Platforms/CMakeLists.txt only enters this
# subdirectory when "GLFW" is the selected backend. See ADR-0018 for the
# configure-time decision (supersedes the brief runtime-loadable detour in
# ADR-0017) and ADR-0019 for the group-directory layout.
#
# Backend-agnostic alias `Engine::Platform` lets Launch link the active backend
# without spelling its name.
add_engine_module(
    NAME         GLFWPlatform
    TYPE         STATIC
    PUBLIC_DEPS  Engine::Core Engine::ApplicationCore
    PRIVATE_DEPS glfw
    PCH_HEADER   Private/GLFWPlatformPCH.h
)

# Backend-agnostic alias used by Launch and any sibling host that wants the
# active PAL backend without naming its concrete spelling.
add_library(Engine::Platform ALIAS GLFWPlatform)
