#include <RenderGraph/RenderGraph.h>

#include <Core/Assert.h>
#include <Core/Logging.h>

#include <RHI/IRHICommandList.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace pe {

DECLARE_LOG_CATEGORY(LogRenderGraph)

namespace {

// Stable key for barrier state tracking: index + generation identify exactly
// one resource incarnation.
uint64 TextureKey(RHITextureHandle h) noexcept {
    return (static_cast<uint64>(h.generation) << 32) | h.index;
}

const char* StateName(ERHIResourceState s) noexcept {
    switch (s) {
        case ERHIResourceState::Undefined:       return "Undefined";
        case ERHIResourceState::RenderTarget:    return "RenderTarget";
        case ERHIResourceState::DepthAttachment: return "DepthAttachment";
        case ERHIResourceState::ShaderResource:  return "ShaderResource";
        case ERHIResourceState::CopySrc:         return "CopySrc";
        case ERHIResourceState::CopyDst:         return "CopyDst";
        case ERHIResourceState::Present:         return "Present";
        default:                                 return "?";
    }
}

}  // namespace

FRenderGraph::FRenderGraph()  = default;
FRenderGraph::~FRenderGraph() = default;

void FRenderGraph::Reset() {
    passes_.clear();
    final_states_.clear();
}

void FRenderGraph::AddPass(const RGPassDesc& desc) {
    ENGINE_CHECK(desc.execute != nullptr);
    FPass pass;
    pass.name = desc.name;
    pass.reads.assign(desc.reads.data, desc.reads.data + desc.reads.size);
    pass.writes.assign(desc.writes.data, desc.writes.data + desc.writes.size);
    pass.execute  = desc.execute;
    pass.userdata = desc.userdata;
    passes_.push_back(std::move(pass));
}

void FRenderGraph::SetFinalState(RHITextureHandle texture, ERHIResourceState state) {
    final_states_.push_back({texture, state});
}

void FRenderGraph::Execute(IRHICommandList& cmd, bool log_barriers) {
    const usize pass_count = passes_.size();
    if (pass_count == 0) { return; }

    // --- Topological order (Kahn, declaration order as tie-break) ----------
    // Edges: writer -> later reader/writer (RAW, WAW) and reader -> later
    // writer (WAR), always from the earlier-declared pass, so declared order
    // is preserved wherever dependencies allow.
    std::vector<std::vector<usize>> edges(pass_count);
    std::vector<uint32>             indegree(pass_count, 0);

    const auto touches = [](const FPass& p, uint64 key, bool writes_only) {
        for (const RGResourceAccess& w : p.writes) {
            if (TextureKey(w.texture) == key) { return true; }
        }
        if (!writes_only) {
            for (const RGResourceAccess& r : p.reads) {
                if (TextureKey(r.texture) == key) { return true; }
            }
        }
        return false;
    };

    for (usize later = 0; later < pass_count; ++later) {
        for (usize earlier = 0; earlier < later; ++earlier) {
            bool dependent = false;
            // RAW / WAW: 'later' touches something 'earlier' writes.
            for (const RGResourceAccess& w : passes_[earlier].writes) {
                if (touches(passes_[later], TextureKey(w.texture), /*writes_only=*/false)) {
                    dependent = true;
                    break;
                }
            }
            // WAR: 'later' writes something 'earlier' reads.
            if (!dependent) {
                for (const RGResourceAccess& r : passes_[earlier].reads) {
                    if (touches(passes_[later], TextureKey(r.texture), /*writes_only=*/true)) {
                        dependent = true;
                        break;
                    }
                }
            }
            if (dependent) {
                edges[earlier].push_back(later);
                ++indegree[later];
            }
        }
    }

    std::vector<usize> order;
    order.reserve(pass_count);
    // Kahn's algorithm; scanning ready passes in index order keeps the
    // declaration order stable among independent passes.
    std::vector<bool> emitted(pass_count, false);
    for (usize emitted_count = 0; emitted_count < pass_count;) {
        bool progressed = false;
        for (usize i = 0; i < pass_count; ++i) {
            if (!emitted[i] && indegree[i] == 0) {
                emitted[i] = true;
                order.push_back(i);
                ++emitted_count;
                for (usize succ : edges[i]) { --indegree[succ]; }
                progressed = true;
            }
        }
        if (!progressed) {
            ENGINE_FATAL("RenderGraph: cyclic pass dependency");
        }
    }

    // --- Barrier computation + recording ------------------------------------
    // Every texture starts the frame Undefined (Stage 2 policy - contents are
    // never carried across frames; passes clear on load).
    std::unordered_map<uint64, ERHIResourceState> current_state;

    std::vector<RHIResourceBarrier> batch;
    const auto transition_to = [&](const char* pass_name, RHITextureHandle tex,
                                   ERHIResourceState desired) {
        const uint64            key    = TextureKey(tex);
        const auto              it     = current_state.find(key);
        const ERHIResourceState before = it == current_state.end()
                                             ? ERHIResourceState::Undefined : it->second;
        if (before == desired) { return; }
        batch.push_back({tex, before, desired});
        current_state[key] = desired;
        if (log_barriers) {
            ENGINE_LOG_INFO(LogRenderGraph, "[rendergraph] pass '{}': texture {} {} -> {}",
                            pass_name, tex.index, StateName(before), StateName(desired));
        }
    };

    for (usize idx : order) {
        const FPass& pass = passes_[idx];
        batch.clear();
        for (const RGResourceAccess& r : pass.reads)  { transition_to(pass.name, r.texture, r.state); }
        for (const RGResourceAccess& w : pass.writes) { transition_to(pass.name, w.texture, w.state); }
        if (!batch.empty()) {
            cmd.ResourceBarrier({batch.data(), batch.size()});
        }
        pass.execute(cmd, pass.userdata);
    }

    batch.clear();
    for (const RGResourceAccess& f : final_states_) {
        transition_to("<final>", f.texture, f.state);
    }
    if (!batch.empty()) {
        cmd.ResourceBarrier({batch.data(), batch.size()});
    }
}

}  // namespace pe
