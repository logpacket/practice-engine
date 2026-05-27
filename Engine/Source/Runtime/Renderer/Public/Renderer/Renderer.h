// Renderer.h - Stage 1 minimal renderer: draws one colored triangle through RHI.
//
// MUST NOT include any Vulkan / volk header. Architecture.md §1.2 (G3 gate) forbids
// it; Renderer talks to IRHIDevice only.

#pragma once

#include <Core/CoreAPI.h>
#include <Core/Types.h>

#include <RHI/RHITypes.h>

namespace pe {

class IRHIDevice;
class IWindow;

class CORE_API FRenderer {
public:
    FRenderer();
    ~FRenderer();

    FRenderer(const FRenderer&)            = delete;
    FRenderer& operator=(const FRenderer&) = delete;

    // Set up GPU resources (swapchain, shaders, pipeline, vertex buffer).
    // Returns false on any failure (errors are logged before returning).
    bool Init(IRHIDevice& device, IWindow& window);

    // Render one frame. No-op if Init failed.
    void RenderFrame();

    // Release GPU resources. Safe to call multiple times.
    void Shutdown();

private:
    IRHIDevice*        device_   = nullptr;
    IWindow*           window_   = nullptr;
    bool               initialized_ = false;

    RHISwapchainHandle swapchain_       = {0};
    RHIShaderHandle    vertex_shader_   = {0};
    RHIShaderHandle    fragment_shader_ = {0};
    RHIPipelineHandle  pipeline_        = {0};
    RHIBufferHandle    vertex_buffer_   = {0};

    uint32             swapchain_width_  = 0;
    uint32             swapchain_height_ = 0;
};

}  // namespace pe
