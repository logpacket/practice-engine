// IRHICommandList.h - command list interface.
//
// Architecture.md §3.4. Stage 2 (ADR-0022): the Stage-1-only TransitionTo*
// pair is replaced by the general ResourceBarrier primitive. The RHI stays
// emit-only - it records exactly the transitions it is told, no state
// tracking; the RenderGraph computes the schedule (ADR-0004/0023).

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>
#include <RHI/RHITypes.h>

namespace pe {

class IRHICommandList {
public:
    virtual void Begin() = 0;
    virtual void End()   = 0;

    // Records the given layout/access transitions (ADR-0022). Only the
    // RenderGraph calls this (ADR-0023).
    virtual void ResourceBarrier(EngineSpan<const RHIResourceBarrier> barriers) = 0;

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
