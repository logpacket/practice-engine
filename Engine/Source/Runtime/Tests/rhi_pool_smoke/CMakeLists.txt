add_engine_executable(
    NAME rhi_pool_smoke
    DEPS Engine::Core Engine::RHI
)

# TResourcePool is VulkanRHI-private but deliberately Vulkan-header-free; the
# test instantiates the template host-side without linking VulkanRHI.
target_include_directories(rhi_pool_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/RHIBackends/Vulkan/Private)
