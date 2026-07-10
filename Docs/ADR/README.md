# Architecture Decision Records

Each file in this directory captures one architecturally-significant decision: the context that forced it, what we chose, what we gave up, and alternatives we ruled out. ADRs are append-only — when a decision is superseded, write a new ADR that references the old one and mark the old one `Superseded by ADR-NNNN`. Never silently rewrite history here.

Format: [Michael Nygard short-form ADR](https://github.com/joelparkerhenderson/architecture-decision-record/tree/main/locales/en/templates/decision-record-template-by-michael-nygard).

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-modules-are-dynamically-loaded.md) | Modules are dynamically loaded shared libraries | Accepted |
| [0002](0002-rhi-is-a-dynamically-loaded-module.md) | RHI backend is a runtime-loaded module, not link-time | Accepted |
| [0003](0003-rhi-handle-model.md) | RHI uses typed-Tag handles, not smart pointers | Accepted |
| [0004](0004-render-graph-emits-barriers.md) | Render graph computes barriers, RHI emits them | Accepted (deferred to Stage 2) |
| [0005](0005-applicationcore-as-pal.md) | ApplicationCore is a PAL with swappable backends; GLFW is a backend, not the interface | Accepted |
| [0006](0006-shaders-glsl-stage-1-hlsl-later.md) | Shaders: GLSL+glslc for Stage 1, HLSL+DXC deferred | Accepted |
| [0007](0007-abi-strict-guards-staged-promotion.md) | ABI strict guards introduced in Stage 1, enforced in Stage 3 | Accepted |
| [0008](0008-stage1-rhi-minimum-surface.md) | Stage 1 RHI minimum surface (13+7 methods) | Accepted |
| [0009](0009-vulkan-1-3-baseline-no-fallback.md) | Vulkan 1.3 baseline, no driver-version fallback | Accepted |
| [0010](0010-pengine-umbrella-module.md) | `PEngine` umbrella module for external hosts | Superseded by [0014](0014-framework-philosophy-and-per-module-pch.md) |
| [0011](0011-per-image-render-finished-semaphore.md) | Per-image `render_finished` semaphore | Accepted (forced by validation in §6.e) |
| [0012](0012-swapchain-device-extension-gated.md) | `VK_KHR_swapchain` device extension gated on caller's surface-creation intent | Accepted |
| [0013](0013-surface-creation-via-callback.md) | Surface creation via callback in `RHIDeviceCreateDesc` | Accepted |
| [0014](0014-framework-philosophy-and-per-module-pch.md) | Framework philosophy + per-module PCH (supersedes 0010) | Accepted |
| [0015](0015-pal-graphics-backend-abstraction.md) | PAL graphics-interop methods take a backend enum, not Vulkan-named methods | Accepted |
| [0016](0016-engine-type-aliases-enforced.md) | Engine type aliases (`uint32`, `int64`, `usize`, …) enforced over raw stdint | Accepted |
| [0017](0017-runtime-loadable-pal-backend.md) | PAL backends are runtime-loadable (symmetric with RHI) | Superseded by [0018](0018-configure-time-pal-static-link.md) |
| [0018](0018-configure-time-pal-static-link.md) | PAL backends — configure-time STATIC link with interface/implementation separation | Accepted |
| [0019](0019-backend-group-directories.md) | Backend modules live in group directories (`Platforms/`, `RHIBackends/`) | Accepted |
| [0020](0020-timeline-semaphores-primary-sync.md) | Timeline semaphores are the primary CPU↔GPU sync; binary semaphores only for swapchain interop | Accepted (Stage 2) |
| [0021](0021-handle-generation-and-deferred-delete.md) | Handle generation counter + deferred-delete queue (handle widened 32→64-bit) | Accepted (Stage 2) |
| [0022](0022-resource-barrier-replaces-transition.md) | `ResourceBarrier(span)` + `ERHIResourceState` replaces Stage-1-only `TransitionTo*` | Accepted (Stage 2) |
| [0023](0023-render-graph-minimum-scope.md) | RenderGraph minimum scope — declarative pass I/O + topo schedule; no aliasing/async-compute/multi-queue | Accepted (Stage 2) |
| [0024](0024-minimal-texture-binding.md) | Minimal texture binding (single static descriptor set, combined image sampler); bindless deferred to Stage 5 | Accepted (Stage 2) |
| [0025](0025-asset-system-v1-scope.md) | Asset system v1 scope — synchronous, in-memory, no cooking/streaming/decoders | Accepted (Stage 2) |
| [0026](0026-swapchain-recreation-and-resize.md) | Swapchain recreation + `IWindow` resize signaling; Renderer owns acquire/present, RenderGraph owns barriers | Accepted (Stage 2) |
| [0027](0027-submit-sync-contract.md) | Submit sync contract — explicit `RHISubmitInfo`; binary semaphores supplied by Renderer only at the boundary submit | Accepted (Stage 2) |

## When to add an ADR

Add a new ADR when **any** of the following are true:

- The decision changes a public interface (engine module API, RHI surface, PAL contract).
- The decision is non-reversible without coordinated multi-module work.
- A future contributor would reasonably ask "why didn't they just do X instead?"
- A reviewer (architect/critic) raised it as a material trade-off during the design phase.

Do **not** add an ADR for: bug fixes, refactors that preserve behavior, choices that fall trivially out of a higher-level ADR.
