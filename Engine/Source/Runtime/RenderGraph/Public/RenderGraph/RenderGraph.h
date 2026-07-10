// RenderGraph.h - declarative pass scheduling + automatic barrier emission.
//
// ADR-0004/0023: the graph sits above the RHI and is the ONLY caller of
// IRHICommandList::ResourceBarrier. Passes declare the textures they read and
// write with the ERHIResourceState they need; the graph topo-sorts passes,
// computes the before/after transitions, and records them before each pass.
//
// Stage 2 minimum scope (ADR-0023): no resource aliasing, no async compute,
// no multi-queue, no split barriers, no pass culling. Everything records into
// one command list on the single graphics queue.
//
// MUST NOT include any Vulkan / volk header (G2/G3 extended to this module).

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>

#include <RHI/RHITypes.h>

#include <RenderGraph/RenderGraphAPI.h>

#include <vector>

namespace pe {

class IRHICommandList;

// One declared access: the state the pass needs the texture in.
struct RGResourceAccess {
    RHITextureHandle  texture;
    ERHIResourceState state = ERHIResourceState::Undefined;
};

// Pass body: records its own render pass begin/end + draws into cmd. Barriers
// and ordering are the graph's job - the callback must not emit barriers.
using RGExecuteFn = void (*)(IRHICommandList& cmd, void* userdata);

struct RGPassDesc {
    const char*                        name     = "";
    EngineSpan<const RGResourceAccess> reads    = {nullptr, 0};
    EngineSpan<const RGResourceAccess> writes   = {nullptr, 0};
    RGExecuteFn                        execute  = nullptr;
    void*                              userdata = nullptr;
};

class RENDERGRAPH_API FRenderGraph {
public:
    FRenderGraph();
    ~FRenderGraph();

    FRenderGraph(const FRenderGraph&)            = delete;
    FRenderGraph& operator=(const FRenderGraph&) = delete;

    // Clears passes and final states; call at the start of each frame build.
    void Reset();

    void AddPass(const RGPassDesc& desc);

    // Declares the state a texture must end the frame in (e.g. Present for
    // the swapchain image). Emitted after the last pass that touches it.
    void SetFinalState(RHITextureHandle texture, ERHIResourceState state);

    // Topo-sorts the passes, computes the barrier schedule, and records
    // barriers + pass bodies into cmd. Every texture is assumed to start the
    // frame in Undefined (Stage 2 policy: all passes clear on load).
    // log_barriers prints the inferred schedule (the ADR-0004 introspection
    // tool; gate G7) - pass true on one frame only to avoid log spam.
    void Execute(IRHICommandList& cmd, bool log_barriers = false);

private:
    struct FPass {
        const char*                   name;
        std::vector<RGResourceAccess> reads;
        std::vector<RGResourceAccess> writes;
        RGExecuteFn                   execute;
        void*                         userdata;
    };

    std::vector<FPass>            passes_;
    std::vector<RGResourceAccess> final_states_;
};

}  // namespace pe
