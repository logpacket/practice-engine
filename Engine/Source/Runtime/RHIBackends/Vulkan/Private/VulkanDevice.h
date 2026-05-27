// VulkanDevice.h - concrete IRHIDevice implementation.
//
// Stage 1 (§6.e fills in the body): real implementations of CreateBuffer / Shader /
// Pipeline / Swapchain, command list lifecycle, frame loop (acquire/present).
//
// All resources live in TResourcePool<...> instances on the device. Stage 1
// uses simple uint32 handles with a Debug ESlotState guard against double-destroy
// (Architecture.md §3.2 / §3.3).

#pragma once

#include "VulkanCommon.h"
#include "VulkanResourcePool.h"
#include "VulkanResources.h"

namespace pe::vk {

class FVulkanCommandList;

class FVulkanDevice final : public IRHIDevice {
public:
    FVulkanDevice(const RHIDeviceCreateDesc& desc, bool& out_failed);
    // Not 'override' - IRHIDevice's destructor is protected non-virtual by design.
    ~FVulkanDevice();

    // --- IRHIDevice ---
    RHIBufferHandle    CreateBuffer(const RHIBufferDesc& desc) override;
    RHIShaderHandle    CreateShader(const RHIShaderDesc& desc) override;
    RHIPipelineHandle  CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    RHISwapchainHandle CreateSwapchain(const RHISwapchainDesc& desc) override;

    void Destroy(RHIBufferHandle handle) override;
    void Destroy(RHIShaderHandle handle) override;
    void Destroy(RHIPipelineHandle handle) override;
    void Destroy(RHISwapchainHandle handle) override;

    RHICommandListHandle AcquireCommandList() override;
    IRHICommandList*     Lock(RHICommandListHandle handle) override;
    EngineResult         Submit(RHICommandListHandle handle) override;

    uint32_t     AcquireNextSwapchainImage(RHISwapchainHandle swapchain) override;
    EngineResult Present(RHISwapchainHandle swapchain, uint32_t image_index) override;
    EngineResult WaitIdle() override;

    // --- internal API consumed by FVulkanCommandList ---
    VulkanSwapchainPayload* GetSwapchainPayload(RHISwapchainHandle h) { return swapchains_.Get(h); }
    VulkanBufferPayload*    GetBufferPayload(RHIBufferHandle h)       { return buffers_.Get(h); }
    VulkanPipelinePayload*  GetPipelinePayload(RHIPipelineHandle h)   { return pipelines_.Get(h); }

    VkDevice         Device()                const noexcept { return device_; }
    VkPhysicalDevice PhysicalDevice()        const noexcept { return physical_device_; }
    VkInstance       Instance()              const noexcept { return instance_; }
    VkQueue          GraphicsQueue()         const noexcept { return graphics_queue_; }
    uint32_t         GraphicsQueueFamily()   const noexcept { return graphics_queue_family_; }

private:
    bool CreateInstance(const RHIDeviceCreateDesc& desc);
    bool CreateDebugMessenger();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateCommandPool();

    // Returns UINT32_MAX if no matching memory type. memory_properties is a
    // mask of VkMemoryPropertyFlagBits (HOST_VISIBLE|HOST_COHERENT etc.).
    uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const;

    void DestroyBufferPayload(VulkanBufferPayload& p);
    void DestroyShaderPayload(VulkanShaderPayload& p);
    void DestroyPipelinePayload(VulkanPipelinePayload& p);
    void DestroySwapchainPayload(VulkanSwapchainPayload& p);
    void DestroyCommandListPayload(VulkanCommandListPayload& p);

    bool                     validation_enabled_   = false;
    VkInstance               instance_             = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_      = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device_      = VK_NULL_HANDLE;
    VkDevice                 device_               = VK_NULL_HANDLE;
    VkQueue                  graphics_queue_       = VK_NULL_HANDLE;
    uint32_t                 graphics_queue_family_ = UINT32_MAX;

    VkCommandPool            command_pool_         = VK_NULL_HANDLE;

    PFN_RHICreateSurface     create_surface_       = nullptr;
    void*                    create_surface_userdata_ = nullptr;

    TResourcePool<VulkanBufferPayload,      BufferTag>      buffers_;
    TResourcePool<VulkanShaderPayload,      ShaderTag>      shaders_;
    TResourcePool<VulkanPipelinePayload,    PipelineTag>    pipelines_;
    TResourcePool<VulkanSwapchainPayload,   SwapchainTag>   swapchains_;
    TResourcePool<VulkanCommandListPayload, CommandListTag> command_lists_;
};

}  // namespace pe::vk
