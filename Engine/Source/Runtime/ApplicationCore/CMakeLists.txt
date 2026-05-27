# ApplicationCore - interface-only module.
#
# Defines IPlatformApplication / IWindow / EKey. The actual backend (GLFW,
# future Win32/Wayland/Cocoa/Console) ships as a separate SHARED module that
# is dlopened at runtime via pe::ModuleLoader (symmetric with how VulkanRHI
# relates to RHI - see ADR-0017).
#
# Hosts link Engine::ApplicationCore for the interface, then dlopen one of
# the backend modules (e.g. "GLFWPlatform") to obtain an IPlatformApplication
# via IModule::QueryInterface.

add_engine_module(
    NAME        ApplicationCore
    TYPE        INTERFACE
    PUBLIC_DEPS Engine::Core
)
