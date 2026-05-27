add_engine_executable(
    NAME rhi_smoke
    DEPS Engine::Core Engine::RHI
)

# Must NOT link VulkanRHI - the whole point is to verify runtime dlopen.
# Establish only a build ordering dependency.
add_dependencies(rhi_smoke VulkanRHI)
