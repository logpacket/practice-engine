// Renderer.h - builds the per-frame RenderGraph and owns the present cycle.
//
// MUST NOT include any Vulkan / volk header. Architecture.md §1.2 (G3 gate)
// forbids it; Renderer talks to IRHIDevice only. Stage 2 (ADR-0026): the
// Renderer owns acquire -> graph build -> submit -> present; the RenderGraph
// owns barrier scheduling.

#pragma once

#include <Core/CoreAPI.h>
#include <Core/Types.h>

#include <RHI/RHITypes.h>

#include <RenderGraph/RenderGraph.h>

namespace pe {

class IRHICommandList;
class IRHIDevice;
class IWindow;

class CORE_API FRenderer {
public:
    FRenderer();
    ~FRenderer();

    FRenderer(const FRenderer&)            = delete;
    FRenderer& operator=(const FRenderer&) = delete;

    // Set up GPU resources (swapchain, offscreen targets, shaders, pipeline,
    // vertex buffer). Returns false on any failure (errors are logged).
    bool Init(IRHIDevice& device, IWindow& window);

    // Render one frame: acquire, build the two-pass graph, submit, present.
    // No-op if Init failed.
    void RenderFrame();

    // Release GPU resources. Safe to call multiple times.
    void Shutdown();

private:
    // RenderGraph pass bodies (RGExecuteFn thunks; userdata == FRenderer*).
    static void ExecuteScenePassThunk(IRHICommandList& cmd, void* userdata);
    static void ExecuteCompositePassThunk(IRHICommandList& cmd, void* userdata);
    void RecordScenePass(IRHICommandList& cmd);
    void RecordCompositePass(IRHICommandList& cmd);

    bool CreateOffscreenTargets(uint32 width, uint32 height);
    void DestroyOffscreenTargets();

    IRHIDevice*        device_   = nullptr;
    IWindow*           window_   = nullptr;
    bool               initialized_ = false;

    RHISwapchainHandle swapchain_       = {};
    RHIShaderHandle    vertex_shader_   = {};
    RHIShaderHandle    fragment_shader_ = {};
    RHIPipelineHandle  scene_pipeline_  = {};
    RHIBufferHandle    vertex_buffer_   = {};

    // Offscreen scene targets (§6.d): ScenePass renders here; CompositePass
    // consumes the color target.
    RHITextureHandle   offscreen_color_ = {};
    RHITextureHandle   offscreen_depth_ = {};

    // Swapchain image texture of the frame currently being recorded; only
    // valid between acquire and present inside RenderFrame.
    RHITextureHandle   frame_backbuffer_ = {};

    FRenderGraph       graph_;

    uint32             swapchain_width_  = 0;
    uint32             swapchain_height_ = 0;

    // Monotonic frame counter; signaled on the frame timeline by each frame's
    // boundary submit (ADR-0020/0027).
    uint64             frame_number_ = 0;
};

}  // namespace pe
