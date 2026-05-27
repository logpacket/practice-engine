# Platforms/ - group directory holding PAL backend modules.
#
# Each subdirectory is one backend (GLFW today; Win32/Wayland/Cocoa/Console
# later). Backends here are STATIC libraries selected at CMake configure time
# via ENGINE_APP_BACKEND - see ADR-0018. The selected backend additionally
# registers an Engine::Platform ALIAS target so Launch can link backend-agnostic.
#
# This group is sibling to ApplicationCore (the PAL interface) and to
# RHIBackends/ (the runtime-loaded RHI backends, a different lifecycle - see
# ADR-0019 for why the two groups exist as separate directories).

if(ENGINE_APP_BACKEND STREQUAL "GLFW")
    add_subdirectory(GLFW)
else()
    message(FATAL_ERROR "ENGINE_APP_BACKEND='${ENGINE_APP_BACKEND}' has no matching directory under Platforms/")
endif()
