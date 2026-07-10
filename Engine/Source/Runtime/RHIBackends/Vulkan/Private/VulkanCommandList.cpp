#include "VulkanCommandList.h"
#include "VulkanDevice.h"

#include <vector>

namespace pe::vk {

FVulkanCommandList::FVulkanCommandList(FVulkanDevice& device, VkCommandBuffer cmd) noexcept
    : device_(device), cmd_(cmd) {}

void FVulkanCommandList::Begin() {
    vkResetCommandBuffer(cmd_, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    PE_VK_CHECK(vkBeginCommandBuffer(cmd_, &bi));
}

void FVulkanCommandList::End() {
    PE_VK_CHECK(vkEndCommandBuffer(cmd_));
}

namespace {

// ERHIResourceState -> (layout, stage, access) triple (ADR-0022). The same
// mapping serves as source scope (what to wait on) and destination scope
// (what to make available).
struct FStateInfo {
    VkImageLayout         layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2        access;
};

FStateInfo ToStateInfo(ERHIResourceState s) {
    switch (s) {
        case ERHIResourceState::RenderTarget:
            return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT};
        case ERHIResourceState::DepthAttachment:
            return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
        case ERHIResourceState::ShaderResource:
            return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case ERHIResourceState::CopySrc:
            return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT};
        case ERHIResourceState::CopyDst:
            return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT};
        case ERHIResourceState::Present:
            // Release to the presentation engine: no stage/access - the
            // binary semaphore handshake orders it (ADR-0027).
            return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
        case ERHIResourceState::Undefined:
        default:
            return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
    }
}

}  // namespace

void FVulkanCommandList::ResourceBarrier(EngineSpan<const RHIResourceBarrier> barriers) {
    if (barriers.size == 0) { return; }

    std::vector<VkImageMemoryBarrier2> image_barriers;
    image_barriers.reserve(barriers.size);
    for (uint64_t i = 0; i < barriers.size; ++i) {
        const RHIResourceBarrier& b   = barriers.data[i];
        auto*                     tex = device_.GetTexturePayload(b.texture);
        const FStateInfo          src = ToStateInfo(b.before);
        const FStateInfo          dst = ToStateInfo(b.after);

        VkImageMemoryBarrier2 barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask        = src.stage;
        barrier.srcAccessMask       = src.access;
        barrier.dstStageMask        = dst.stage;
        barrier.dstAccessMask       = dst.access;
        barrier.oldLayout           = src.layout;
        barrier.newLayout           = dst.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = tex->image;
        barrier.subresourceRange.aspectMask =
            tex->format == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        image_barriers.push_back(barrier);
    }

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(image_barriers.size());
    dep.pImageMemoryBarriers    = image_barriers.data();

    vkCmdPipelineBarrier2(cmd_, &dep);
}

void FVulkanCommandList::BeginRenderPass(const RHIRenderPassBeginInfo& info) {
    std::vector<VkRenderingAttachmentInfo> color_atts;
    color_atts.reserve(info.color_attachments.size);
    for (uint64_t i = 0; i < info.color_attachments.size; ++i) {
        const RHIRenderPassColorAttachment& att = info.color_attachments.data[i];
        auto* tex = device_.GetTexturePayload(att.texture);

        VkRenderingAttachmentInfo color_att{};
        color_att.sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_att.imageView        = tex->view;
        color_att.imageLayout      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_att.loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_att.storeOp          = VK_ATTACHMENT_STORE_OP_STORE;
        color_att.clearValue.color = {{att.clear.rgba[0], att.clear.rgba[1],
                                       att.clear.rgba[2], att.clear.rgba[3]}};
        color_atts.push_back(color_att);
    }

    VkRenderingAttachmentInfo depth_att{};
    if (info.depth.texture.valid()) {
        auto* tex = device_.GetTexturePayload(info.depth.texture);
        depth_att.sType                         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_att.imageView                     = tex->view;
        depth_att.imageLayout                   = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_att.loadOp                        = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_att.storeOp                       = VK_ATTACHMENT_STORE_OP_STORE;
        depth_att.clearValue.depthStencil.depth = info.depth.clear_depth;
    }

    VkRect2D render_area{};
    render_area.offset = {info.render_area.x, info.render_area.y};
    render_area.extent = {info.render_area.width, info.render_area.height};

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea           = render_area;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = static_cast<uint32_t>(color_atts.size());
    ri.pColorAttachments    = color_atts.data();
    ri.pDepthAttachment     = info.depth.texture.valid() ? &depth_att : nullptr;

    vkCmdBeginRendering(cmd_, &ri);
}

void FVulkanCommandList::EndRenderPass() {
    vkCmdEndRendering(cmd_);
}

void FVulkanCommandList::SetViewport(const RHIViewport& vp) {
    VkViewport v{};
    v.x        = vp.x;
    v.y        = vp.y;
    v.width    = vp.width;
    v.height   = vp.height;
    v.minDepth = vp.min_depth;
    v.maxDepth = vp.max_depth;
    vkCmdSetViewport(cmd_, 0, 1, &v);
}

void FVulkanCommandList::SetScissor(const RHIRect& rect) {
    VkRect2D r{};
    r.offset = {rect.x, rect.y};
    r.extent = {rect.width, rect.height};
    vkCmdSetScissor(cmd_, 0, 1, &r);
}

void FVulkanCommandList::SetPipeline(RHIPipelineHandle pipeline) {
    auto* p = device_.GetPipelinePayload(pipeline);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
    bound_layout_     = p->layout;
    bound_set_layout_ = p->set_layout;
}

void FVulkanCommandList::SetTexture(uint32_t slot, RHITextureHandle texture,
                                    RHISamplerHandle sampler) {
    ENGINE_CHECK(bound_set_layout_ != VK_NULL_HANDLE);  // pipeline must declare descriptor_bindings
    VkDescriptorSet set = device_.GetOrCreateDescriptorSet(bound_set_layout_, slot, texture, sampler);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, bound_layout_,
                            0, 1, &set, 0, nullptr);
}

void FVulkanCommandList::SetVertexBuffer(RHIBufferHandle buffer, uint64_t offset) {
    auto* b = device_.GetBufferPayload(buffer);
    VkDeviceSize ofs = offset;
    vkCmdBindVertexBuffers(cmd_, 0, 1, &b->buffer, &ofs);
}

void FVulkanCommandList::Draw(uint32_t vertex_count, uint32_t first_vertex) {
    vkCmdDraw(cmd_, vertex_count, 1, first_vertex, 0);
}

}  // namespace pe::vk
