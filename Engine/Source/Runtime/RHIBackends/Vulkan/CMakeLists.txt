# VulkanRHI - the only target that depends on Vulkan SDK headers.
# Renderer/Launch/Programs MUST NOT link Vulkan; they reach VulkanRHI via dlopen
# (G1/G2/G3 gates enforce this in §7).
#
# volk is statically linked PRIVATELY here; it owns runtime loader binding for us.
# We do NOT link Vulkan::Vulkan at link time because volk is the loader.
add_engine_module(
    NAME         VulkanRHI
    TYPE         SHARED
    PUBLIC_DEPS  Engine::Core Engine::RHI
    PRIVATE_DEPS volk::volk
    PCH_HEADER   Private/VulkanRHIPCH.h
)

# volk needs to know we are using its dispatch instead of the static loader.
target_compile_definitions(VulkanRHI PRIVATE VK_NO_PROTOTYPES)
