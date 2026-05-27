add_engine_executable(
    NAME HelloTriangle
    DEPS Engine::Core
         Engine::RHI
         Engine::Renderer
         Engine::ApplicationCore   # PAL interface (INTERFACE library, header propagation)
         Engine::Platform          # the active PAL backend (STATIC, ADR-0018)
)

# VulkanRHI is dlopened at runtime - build ordering only, no link.
add_dependencies(HelloTriangle VulkanRHI)
