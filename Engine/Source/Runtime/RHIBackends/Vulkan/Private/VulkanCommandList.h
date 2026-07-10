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

    void ResourceBarrier(EngineSpan<const RHIResourceBarrier> barriers) override;

    void BeginRenderPass(const RHIRenderPassBeginInfo& info) override;
    void EndRenderPass() override;

    void SetViewport(const RHIViewport& vp) override;
    void SetScissor(const RHIRect& rect) override;

    void SetPipeline(RHIPipelineHandle pipeline) override;
    void SetVertexBuffer(RHIBufferHandle buffer, uint64_t offset) override;
    void Draw(uint32_t vertex_count, uint32_t first_vertex) override;

    VkCommandBuffer Raw() const noexcept { return cmd_; }

private:
    FVulkanDevice&      device_;
    VkCommandBuffer     cmd_;
};

}  // namespace pe::vk
