// VulkanCommandList.h - IRHICommandList implementation backed by a VkCommandBuffer.

#pragma once

#include "VulkanCommon.h"

namespace pe::vk {

class FVulkanDevice;

class FVulkanCommandList final : public IRHICommandList {
public:
    FVulkanCommandList(FVulkanDevice& device, VkCommandBuffer cmd) noexcept;

    // IRHICommandList -----
    void Begin() override;
    void End()   override;

    void TransitionToRenderTarget(RHISwapchainHandle swapchain, uint32_t image_index) override;
    void TransitionToPresent(RHISwapchainHandle swapchain, uint32_t image_index) override;

    void BeginRenderPass(const RHIRenderPassBeginInfo& info) override;
    void EndRenderPass() override;

    void SetViewport(const RHIViewport& vp) override;
    void SetScissor(const RHIRect& rect) override;

    void SetPipeline(RHIPipelineHandle pipeline) override;
    void SetVertexBuffer(RHIBufferHandle buffer, uint64_t offset) override;
    void Draw(uint32_t vertex_count, uint32_t first_vertex) override;

    VkCommandBuffer    Raw()             const noexcept { return cmd_; }
    RHISwapchainHandle BoundSwapchain()  const noexcept { return bound_swapchain_; }
    uint32_t           BoundImageIndex() const noexcept { return bound_image_index_; }

private:
    FVulkanDevice&      device_;
    VkCommandBuffer     cmd_;
    // Set in TransitionToRenderTarget; consumed by Submit/Present so the device
    // knows which swapchain's sync objects to use.
    RHISwapchainHandle  bound_swapchain_   = {};
    uint32_t            bound_image_index_ = 0;
};

}  // namespace pe::vk
