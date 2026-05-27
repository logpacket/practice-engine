// IRHIDevice.h - Stage 1 device interface.
//
// Architecture.md §3.3. The 13 methods enumerated there are the minimum surface
// to draw one triangle through dynamic rendering; AcquireNextSwapchainImage is
// added here because the frame loop in §6.e needs an explicit acquire call
// (the design listed it implicitly via "swapchain acquire" in the loop diagram).

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>
#include <RHI/RHITypes.h>

namespace pe {

class IRHICommandList;

class IRHIDevice {
public:
    // --- Resource creation ------------------------------------------------
    virtual RHIBufferHandle    CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual RHIShaderHandle    CreateShader(const RHIShaderDesc& desc) = 0;
    virtual RHIPipelineHandle  CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;
    virtual RHISwapchainHandle CreateSwapchain(const RHISwapchainDesc& desc) = 0;

    // --- Resource destruction ---------------------------------------------
    // Stage 1: caller must ensure no GPU work in flight references the handle
    // (typically by calling WaitIdle() before destroying anything). Stage 2
    // adds a deferred-delete queue.
    virtual void Destroy(RHIBufferHandle handle)    = 0;
    virtual void Destroy(RHIShaderHandle handle)    = 0;
    virtual void Destroy(RHIPipelineHandle handle)  = 0;
    virtual void Destroy(RHISwapchainHandle handle) = 0;

    // --- Command lists -----------------------------------------------------
    virtual RHICommandListHandle AcquireCommandList() = 0;
    virtual IRHICommandList*     Lock(RHICommandListHandle handle) = 0;
    virtual EngineResult         Submit(RHICommandListHandle handle) = 0;

    // --- Swapchain frame loop ---------------------------------------------
    // Returns the next swapchain image index to render into. Blocks until an
    // image is available. Stage 1 has no MAX_FRAMES_IN_FLIGHT > 1.
    virtual uint32       AcquireNextSwapchainImage(RHISwapchainHandle swapchain) = 0;
    virtual EngineResult Present(RHISwapchainHandle swapchain, uint32 image_index) = 0;

    // --- Sync --------------------------------------------------------------
    virtual EngineResult WaitIdle() = 0;

protected:
    ~IRHIDevice() = default;  // owned by VulkanRHI module; destroyed via IRHIBackendModule::DestroyDevice
};

}  // namespace pe
