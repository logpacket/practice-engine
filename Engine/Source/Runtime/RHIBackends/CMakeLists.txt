# RHIBackends/ - group directory holding RHI backend modules.
#
# Each subdirectory is one backend (Vulkan today; D3D12 / Metal later). Backends
# here are SHARED libraries dlopened at runtime via pe::ModuleLoader - see
# ADR-0002. The group name signals the runtime-loading lifecycle, distinct
# from sibling Platforms/ (configure-time STATIC, see ADR-0018 + ADR-0019).
#
# All backends compile if they can on the active OS; the host picks one by
# name at runtime via LoadModule("VulkanRHI") / future LoadModule("D3D12RHI").

add_subdirectory(Vulkan)
