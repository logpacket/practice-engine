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
    virtual RHITextureHandle   CreateTexture(const RHITextureDesc& desc) = 0;
    virtual RHISamplerHandle   CreateSampler(const RHISamplerDesc& desc) = 0;

    // Borrowed texture wrapping the given swapchain image (ADR-0021/0026):
    // the wrapper slot is allocated at swapchain create/recreate, Destroy on
    // it is non-owning, and recreation bumps its generation - never cache the
    // returned handle across a RecreateSwapchain.
    virtual RHITextureHandle GetSwapchainImageTexture(RHISwapchainHandle swapchain,
                                                      uint32 image_index) = 0;

    // --- Resource destruction ---------------------------------------------
    // Deferred-delete (ADR-0021): Destroy enqueues the backend payload stamped
    // with the current frame value; it is reclaimed once the frame timeline
    // passes that value. No WaitIdle-before-destroy precondition remains.
    // Exception: Destroy(RHISwapchainHandle) is immediate and stalls the
    // device internally - swapchain teardown is a shutdown/recreate path, not
    // a steady-loop operation.
    virtual void Destroy(RHIBufferHandle handle)    = 0;
    virtual void Destroy(RHIShaderHandle handle)    = 0;
    virtual void Destroy(RHIPipelineHandle handle)  = 0;
    virtual void Destroy(RHISwapchainHandle handle) = 0;
    virtual void Destroy(RHITextureHandle handle)   = 0;
    virtual void Destroy(RHISamplerHandle handle)   = 0;

    // --- Command lists -----------------------------------------------------
    // AcquireCommandList returns a command list from the current frame slot's
    // ring; the handle is recycled MAX_FRAMES_IN_FLIGHT frames later and must
    // not be held across frames.
    virtual RHICommandListHandle AcquireCommandList() = 0;
    virtual IRHICommandList*     Lock(RHICommandListHandle handle) = 0;
    // Submit with the explicit sync contract (ADR-0027). See RHISubmitInfo.
    virtual EngineResult         Submit(RHICommandListHandle handle,
                                        const RHISubmitInfo& sync) = 0;

    // --- Swapchain frame loop ---------------------------------------------
    // Marks the start of a frame: waits until the frame timeline permits
    // reusing the next frame slot (ADR-0020), reclaims deferred deletes, then
    // acquires the next swapchain image. The returned image_available must be
    // passed as a wait semaphore on this frame's boundary submit.
    virtual RHIAcquiredImage AcquireNextSwapchainImage(RHISwapchainHandle swapchain) = 0;
    // The binary semaphore the boundary submit must signal and Present waits on.
    virtual RHISemaphore GetRenderFinishedSemaphore(RHISwapchainHandle swapchain,
                                                    uint32 image_index) = 0;
    virtual EngineResult Present(RHISwapchainHandle swapchain, uint32 image_index) = 0;

    // --- Sync --------------------------------------------------------------
    virtual EngineResult WaitIdle() = 0;

protected:
    ~IRHIDevice() = default;  // owned by VulkanRHI module; destroyed via IRHIBackendModule::DestroyDevice
};

}  // namespace pe
