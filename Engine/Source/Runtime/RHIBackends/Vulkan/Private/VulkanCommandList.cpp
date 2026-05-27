#include "VulkanCommandList.h"
#include "VulkanDevice.h"

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

void FVulkanCommandList::TransitionToRenderTarget(RHISwapchainHandle swapchain,
                                                  uint32_t           image_index) {
    bound_swapchain_   = swapchain;
    bound_image_index_ = image_index;

    auto* sc = device_.GetSwapchainPayload(swapchain);
    VkImage image = sc->images[image_index];

    VkImageMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                          = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd_, &dep);
}

void FVulkanCommandList::TransitionToPresent(RHISwapchainHandle swapchain,
                                             uint32_t           image_index) {
    auto* sc = device_.GetSwapchainPayload(swapchain);
    VkImage image = sc->images[image_index];

    VkImageMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                          = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(cmd_, &dep);
}

void FVulkanCommandList::BeginRenderPass(const RHIRenderPassBeginInfo& info) {
    auto* sc = device_.GetSwapchainPayload(info.swapchain);
    const uint32_t image_index = info.swapchain_image_index;

    VkRenderingAttachmentInfo color_att{};
    color_att.sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_att.imageView          = sc->image_views[image_index];
    color_att.imageLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp             = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp            = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clearValue.color   = {{info.clear_color.rgba[0], info.clear_color.rgba[1],
                                     info.clear_color.rgba[2], info.clear_color.rgba[3]}};

    VkRect2D render_area{};
    render_area.offset = {info.render_area.x, info.render_area.y};
    render_area.extent = {info.render_area.width, info.render_area.height};

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea           = render_area;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color_att;

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
