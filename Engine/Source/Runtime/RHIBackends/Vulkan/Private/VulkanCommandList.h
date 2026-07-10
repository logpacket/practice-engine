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
    void SetTexture(uint32_t slot, RHITextureHandle texture, RHISamplerHandle sampler) override;
    void SetVertexBuffer(RHIBufferHandle buffer, uint64_t offset) override;
    void Draw(uint32_t vertex_count, uint32_t first_vertex) override;

    VkCommandBuffer Raw() const noexcept { return cmd_; }

private:
    FVulkanDevice&      device_;
    VkCommandBuffer     cmd_;
    // Layouts of the currently bound pipeline; SetTexture binds against them.
    VkPipelineLayout      bound_layout_     = VK_NULL_HANDLE;
    VkDescriptorSetLayout bound_set_layout_ = VK_NULL_HANDLE;
};

}  // namespace pe::vk
