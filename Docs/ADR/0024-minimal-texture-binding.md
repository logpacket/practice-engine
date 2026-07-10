# ADR-0024: Minimal texture binding — single static descriptor set, combined image sampler; bindless deferred to Stage 5

**Status:** Accepted (implemented in Stage 2 §6.e)

## Context

[ADR-0008](0008-stage1-rhi-minimum-surface.md) deferred BindGroup/descriptor machinery with the rule "methods get added when the first caller appears in Stage 2-3." Stage 2's CompositePass *is* that first caller: it samples the offscreen color texture, which on Vulkan requires a descriptor set (combined image sampler), a set layout, a descriptor pool, and a pipeline layout that references the set. The Stage 1 pipeline layout is empty and `RHIGraphicsPipelineDesc` has no field to declare sampled-texture slots.

The danger is scope. A full bindless model (`VK_EXT_descriptor_indexing`, D3D12 SM6.6 dynamic resources) is what [Architecture §4](../Architecture.md) wants eventually, but it presupposes a material system that does not exist until Stage 4+. Worse, a *per-frame-updated* descriptor set collides with multi-frame ([ADR-0020](0020-timeline-semaphores-primary-sync.md)): mutating a set that frame N still reads needs a descriptor ring, which is most of a descriptor allocator.

## Decision

Stage 2 surfaces the **minimum descriptor path** with a real caller:

- `RHIGraphicsPipelineDesc` gains a descriptor-layout field: `EngineSpan<const RHIDescriptorBinding>` where each binding is `{ slot, kind = CombinedImageSampler }`.
- `IRHICommandList::SetTexture(uint32 slot, RHITextureHandle, RHISamplerHandle)` binds one combined image sampler.
- **The descriptor set is static**: allocated and written when the offscreen target is (re)created, and only *bound* each frame — never updated per frame. Because the offscreen texture is stable between resizes, frame N and frame N+1 read the same set with no hazard, so no descriptor ring is needed.

Explicitly **deferred to Stage 5** (when the D3D12 backend forces root-signature alignment): bindless descriptor indexing, multiple/dynamic descriptor sets, uniform/storage buffer descriptors, per-draw descriptor updates, the full `BindGroup{Layout}` model.

## Consequences

**Positive:**
- Honors the ADR-0008 ethos — the descriptor surface has exactly one caller, so its shape is validated, and it is minimal.
- The static set sidesteps the descriptor-ring vs multi-frame collision entirely.
- D3D12/Metal need only implement a one-set, one-combined-image-sampler path for Stage 2.

**Negative:**
- Cannot express per-draw texture changes or >1 sampled texture per pass without extension — acceptable: no Stage 2 pass needs it.
- The eventual bindless redesign (Stage 5) will reshape this surface; bounded because there is one caller (CompositePass).

## Alternatives considered

- **Full bindless / `BindGroup{Layout}` now** — rejected. Presupposes a material system (Stage 4+); untested-interface lock-in ([ADR-0008](0008-stage1-rhi-minimum-surface.md)).
- **Per-frame descriptor updates with a ring** — rejected for Stage 2. Pulls in a descriptor allocator the static set makes unnecessary; the texture is stable between resizes.
- **Offscreen + `vkCmdBlitImage` to the swapchain (no sampling, no descriptors)** — rejected by the user's scope decision. It would leave the headline "Sampler API" with no caller, violating the ADR-0008 ethos.

## References

- `Engine/Source/Runtime/RHI/Public/RHI/RHITypes.h` — `RHIGraphicsPipelineDesc` descriptor-layout field, `RHIDescriptorBinding`
- `Engine/Source/Runtime/RHI/Public/RHI/IRHICommandList.h` — `SetTexture`
- [ADR-0008](0008-stage1-rhi-minimum-surface.md), [ADR-0020](0020-timeline-semaphores-primary-sync.md), [Architecture §4](../Architecture.md)
- Stage2.md §6.e, gate G12
