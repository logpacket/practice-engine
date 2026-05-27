# Launch - STATIC bootstrap glue consumed by Programs/practice-engine and samples.
#
# Links:
#   Engine::Core, Engine::RHI (interface), Engine::Renderer  - normal link
#   Engine::ApplicationCore (interface)                        - normal link (header-only interface)
#   Engine::Platform                                            - the active PAL backend (ADR-0018)
#                                                                 STATIC alias provided by Platforms/<Backend>/
#
# VulkanRHI is NOT linked - it is dlopened at runtime via pe::ModuleLoader
# (ADR-0002). Only its Public/RHI interface headers are needed at compile time.
add_engine_module(
    NAME        Launch
    TYPE        STATIC
    PUBLIC_DEPS Engine::Core Engine::RHI Engine::ApplicationCore Engine::Renderer Engine::Platform
    PCH_HEADER  Private/LaunchPCH.h
)

# Build ordering: VulkanRHI must exist on disk before Launch can run (dlopen).
add_dependencies(Launch VulkanRHI)
