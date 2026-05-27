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

    // Stage 1: 1 frame-in-flight. image_available is per-acquire (1 instance is fine
    // since the previous submit's wait has consumed it by the time the fence signals).
    // render_finished is PER-IMAGE because the presentation engine may still hold the
    // previous signal of the prior image when we want to signal the next image
    // (validation: VUID-vkQueueSubmit-pSignalSemaphores-00067).
    VkSemaphore               image_available = VK_NULL_HANDLE;
    std::vector<VkSemaphore>  render_finished_per_image;
    VkFence                   frame_done      = VK_NULL_HANDLE;

    uint32_t                  current_image_index = 0;
};

struct VulkanCommandListPayload {
    VkCommandBuffer cmd            = VK_NULL_HANDLE;
    class FVulkanCommandList* wrapper = nullptr;  // engine-side wrapper (owned by FVulkanDevice)
};

}  // namespace pe::vk
