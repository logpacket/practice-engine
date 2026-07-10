// VulkanResources.h - per-resource payload structs held by FVulkanDevice's pools.

#pragma once

#include "VulkanCommon.h"

#include <vector>

namespace pe::vk {

struct VulkanBufferPayload {
    VkBuffer       buffer     = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;
    uint64         size_bytes = 0;
};

struct VulkanShaderPayload {
    VkShaderModule module = VK_NULL_HANDLE;
    ERHIShaderStage stage  = ERHIShaderStage::Vertex;
};

struct VulkanTexturePayload {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;  // VK_NULL_HANDLE when external
    VkImageView    view   = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    VkExtent2D     extent = {0, 0};
    // External = borrowed swapchain image (ADR-0021): image and view are
    // owned by the swapchain payload; Destroy must free nothing GPU-side.
    bool           external = false;
};

struct VulkanSamplerPayload {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanPipelinePayload {
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout   = VK_NULL_HANDLE;
};

struct VulkanSwapchainPayload {
    VkSurfaceKHR              surface    = VK_NULL_HANDLE;
    VkSwapchainKHR            swapchain  = VK_NULL_HANDLE;
    VkFormat                  format     = VK_FORMAT_UNDEFINED;
    VkExtent2D                extent     = {0, 0};
    std::vector<VkImage>      images;        // borrowed from swapchain (do not destroy)
    std::vector<VkImageView>  image_views;   // owned by us
    // Borrowed-texture wrappers over images[] (ADR-0021/0026). Allocated at
    // swapchain create/recreate, removed (generation bump) on destroy/recreate.
    std::vector<RHITextureHandle> image_textures;

    // Stage 2 (ADR-0020): frame pacing is the device timeline semaphore; only
    // the swapchain handshake keeps binary semaphores.
    //   - image_available is PER-FRAME-SLOT: the timeline wait at frame start
    //     guarantees the slot's previous acquire semaphore was consumed.
    //   - render_finished is PER-IMAGE because the presentation engine may
    //     still hold the previous signal of the prior image when we want to
    //     signal the next one (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    std::vector<VkSemaphore>  image_available_per_slot;
    std::vector<VkSemaphore>  render_finished_per_image;
};

struct VulkanCommandListPayload {
    VkCommandBuffer cmd            = VK_NULL_HANDLE;
    class FVulkanCommandList* wrapper = nullptr;  // engine-side wrapper (owned by FVulkanDevice)
};

}  // namespace pe::vk
