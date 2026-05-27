// IRHICommandList.h - Stage 1 command list interface.
//
// Architecture.md §3.4. The two TransitionTo* methods are intentionally
// backend-neutral names that map to VK_IMAGE_LAYOUT_{COLOR_ATTACHMENT_OPTIMAL,
// PRESENT_SRC_KHR} on Vulkan and to D3D12_RESOURCE_STATE_{RENDER_TARGET, PRESENT}
// on D3D12. They are Stage-1-only API; Stage 2 replaces them with
// ResourceBarrier(span) once RenderGraph arrives.

#pragma once

#include <Core/Types.h>
#include <RHI/RHITypes.h>

namespace pe {

class IRHICommandList {
public:
    virtual void Begin() = 0;
    virtual void End()   = 0;

    // Layout transitions for the swapchain color attachment. Stage 1 only.
    virtual void TransitionToRenderTarget(RHISwapchainHandle swapchain,
                                          uint32             swapchain_image_index) = 0;
    virtual void TransitionToPresent(RHISwapchainHandle swapchain,
                                     uint32             swapchain_image_index) = 0;

    virtual void BeginRenderPass(const RHIRenderPassBeginInfo& info) = 0;
    virtual void EndRenderPass() = 0;

    virtual void SetViewport(const RHIViewport& vp) = 0;
    virtual void SetScissor(const RHIRect& rect) = 0;

    virtual void SetPipeline(RHIPipelineHandle pipeline) = 0;
    virtual void SetVertexBuffer(RHIBufferHandle buffer, uint64 offset = 0) = 0;
    virtual void Draw(uint32 vertex_count, uint32 first_vertex = 0) = 0;

protected:
    ~IRHICommandList() = default;  // managed by IRHIDevice, never deleted via this pointer
};

}  // namespace pe
