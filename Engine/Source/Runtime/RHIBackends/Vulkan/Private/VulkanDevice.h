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

#include <map>

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
    RHITextureHandle   CreateTexture(const RHITextureDesc& desc) override;
    RHISamplerHandle   CreateSampler(const RHISamplerDesc& desc) override;
    RHITextureHandle   GetSwapchainImageTexture(RHISwapchainHandle swapchain,
                                                uint32_t image_index) override;

    void Destroy(RHIBufferHandle handle) override;
    void Destroy(RHIShaderHandle handle) override;
    void Destroy(RHIPipelineHandle handle) override;
    void Destroy(RHISwapchainHandle handle) override;
    void Destroy(RHITextureHandle handle) override;
    void Destroy(RHISamplerHandle handle) override;

    RHICommandListHandle AcquireCommandList() override;
    IRHICommandList*     Lock(RHICommandListHandle handle) override;
    EngineResult         Submit(RHICommandListHandle handle, const RHISubmitInfo& sync) override;

    RHIAcquiredImage AcquireNextSwapchainImage(RHISwapchainHandle swapchain) override;
    RHISemaphore     GetRenderFinishedSemaphore(RHISwapchainHandle swapchain,
                                                uint32_t image_index) override;
    EngineResult Present(RHISwapchainHandle swapchain, uint32_t image_index) override;
    EngineResult WaitIdle() override;
    EngineResult ReadbackTexturePixel(RHITextureHandle texture, ERHIResourceState state,
                                      uint32_t x, uint32_t y, uint8_t out_rgba[4]) override;

    // --- internal API consumed by FVulkanCommandList ---
    VulkanSwapchainPayload* GetSwapchainPayload(RHISwapchainHandle h) { return swapchains_.Get(h); }
    VulkanBufferPayload*    GetBufferPayload(RHIBufferHandle h)       { return buffers_.Get(h); }
    VulkanPipelinePayload*  GetPipelinePayload(RHIPipelineHandle h)   { return pipelines_.Get(h); }
    VulkanTexturePayload*   GetTexturePayload(RHITextureHandle h)     { return textures_.Get(h); }
    VulkanSamplerPayload*   GetSamplerPayload(RHISamplerHandle h)     { return samplers_.Get(h); }

    // Static descriptor set for a (layout, texture, sampler) triple
    // (ADR-0024): allocated + written on first request, cache-hit afterwards.
    // Invalidated (deferred-freed) when the texture or sampler is destroyed.
    VkDescriptorSet GetOrCreateDescriptorSet(VkDescriptorSetLayout layout, uint32_t slot,
                                             RHITextureHandle texture, RHISamplerHandle sampler);

    VkDevice         Device()                const noexcept { return device_; }
    VkPhysicalDevice PhysicalDevice()        const noexcept { return physical_device_; }
    VkInstance       Instance()              const noexcept { return instance_; }
    VkQueue          GraphicsQueue()         const noexcept { return graphics_queue_; }
    uint32_t         GraphicsQueueFamily()   const noexcept { return graphics_queue_family_; }

    static constexpr uint32_t kMaxFramesInFlight = 2;

private:
    bool CreateInstance(const RHIDeviceCreateDesc& desc);
    bool CreateDebugMessenger();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateFrameResources();  // per-frame command pools + frame timeline

    // Returns UINT32_MAX if no matching memory type. memory_properties is a
    // mask of VkMemoryPropertyFlagBits (HOST_VISIBLE|HOST_COHERENT etc.).
    uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const;

    void DestroyBufferPayload(VulkanBufferPayload& p);
    void DestroyShaderPayload(VulkanShaderPayload& p);
    void DestroyPipelinePayload(VulkanPipelinePayload& p);
    void DestroySwapchainPayload(VulkanSwapchainPayload& p);
    void DestroyCommandListPayload(VulkanCommandListPayload& p);
    void DestroyTexturePayload(VulkanTexturePayload& p);
    void DestroySamplerPayload(VulkanSamplerPayload& p);

    // The frame value the current recording frame's boundary submit will
    // signal (ADR-0021). Deferred deletes are stamped with this.
    uint64_t CurrentFrameValue() const noexcept { return submitted_timeline_value_ + 1; }
    // Destroys every deferred payload whose stamp is <= completed.
    void     DrainDeferred(uint64_t completed);

    bool                     validation_enabled_   = false;
    VkInstance               instance_             = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_      = VK_NULL_HANDLE;
    VkPhysicalDevice         physical_device_      = VK_NULL_HANDLE;
    VkDevice                 device_               = VK_NULL_HANDLE;
    VkQueue                  graphics_queue_       = VK_NULL_HANDLE;
    uint32_t                 graphics_queue_family_ = UINT32_MAX;

    // --- Multi-frame state (ADR-0020) ---
    // One command pool per frame slot; the pool is bulk-reset at frame start
    // once the timeline wait proves the slot's previous frame completed.
    struct FFrameSlot {
        VkCommandPool                     pool = VK_NULL_HANDLE;
        std::vector<RHICommandListHandle> lists;  // recycled wrappers
        uint32_t                          used = 0;
    };
    FFrameSlot  frames_[kMaxFramesInFlight];
    uint32_t    current_slot_ = 0;

    VkSemaphore frame_timeline_            = VK_NULL_HANDLE;
    uint64_t    submitted_timeline_value_  = 0;  // highest value passed to Submit
    uint64_t    peak_frames_in_flight_     = 0;  // G8 instrument

    // --- Deferred-delete queues (ADR-0021) ---
    template <typename TPayload>
    struct FDeferred {
        uint64_t stamp;
        TPayload payload;
    };
    std::vector<FDeferred<VulkanBufferPayload>>   deferred_buffers_;
    std::vector<FDeferred<VulkanShaderPayload>>   deferred_shaders_;
    std::vector<FDeferred<VulkanPipelinePayload>> deferred_pipelines_;
    std::vector<FDeferred<VulkanTexturePayload>>  deferred_textures_;
    std::vector<FDeferred<VulkanSamplerPayload>>  deferred_samplers_;
    std::vector<FDeferred<VkDescriptorSet>>       deferred_descriptor_sets_;

    // --- Minimal descriptor path (ADR-0024) ---
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    struct FDescriptorSetKey {
        uint64_t layout;
        uint64_t texture;
        uint64_t sampler;
        bool operator<(const FDescriptorSetKey& o) const noexcept {
            if (layout != o.layout)   { return layout < o.layout; }
            if (texture != o.texture) { return texture < o.texture; }
            return sampler < o.sampler;
        }
    };
    std::map<FDescriptorSetKey, VkDescriptorSet> descriptor_set_cache_;
    // Deferred-frees every cached set referencing the given texture/sampler.
    void InvalidateDescriptorSets(uint64_t texture_key, uint64_t sampler_key);

    PFN_RHICreateSurface     create_surface_       = nullptr;
    void*                    create_surface_userdata_ = nullptr;

    TResourcePool<VulkanBufferPayload,      BufferTag>      buffers_;
    TResourcePool<VulkanShaderPayload,      ShaderTag>      shaders_;
    TResourcePool<VulkanPipelinePayload,    PipelineTag>    pipelines_;
    TResourcePool<VulkanSwapchainPayload,   SwapchainTag>   swapchains_;
    TResourcePool<VulkanCommandListPayload, CommandListTag> command_lists_;
    TResourcePool<VulkanTexturePayload,     TextureTag>     textures_;
    TResourcePool<VulkanSamplerPayload,     SamplerTag>     samplers_;
};

}  // namespace pe::vk
