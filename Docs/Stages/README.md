# Implementation Stages

Each stage doc describes one *executable* slice of the long-term Architecture.md vision: what gets built, what is intentionally omitted vs the goal, the verification gates that say it is done, and the simplifying decisions taken to fit the stage's scope.

The vision lives in [`../Architecture.md`](../Architecture.md). The reasoning behind each architectural choice lives in [`../ADR/`](../ADR/). Stage docs are the bridge — concrete step-by-step plans for *how* the vision lands incrementally.

## Index

| Stage | Title | Status | Headline goal |
|---|---|---|---|
| [1](Stage1.md) | Walking skeleton | Complete | Run `HelloTriangle`: dynamically loaded VulkanRHI draws a colored triangle through the RHI abstraction; Renderer reaches no Vulkan symbol or header |
| [2](Stage2.md) | RenderGraph + multi-frame | Complete | Auto-barrier RenderGraph, MAILBOX present, multi-frames-in-flight, deferred-delete, Texture/Sampler API, swapchain recreation on resize, Asset system v1 |
| 3 | ECS + gameplay + hot reload | Planned | Archetype ECS storage + Actor/Component public API, hot reload state machine, ABI strict-guard enforcement promotion, ShaderCompiler module, gameplay↔RHI isolation gate |
| 4 | Editor + PIE | Planned | ImGui editor, PIE (Play-In-Editor), multi-swapchain/offscreen RHI surface additions, reflection mechanism (UHT replacement) |
| 5 | D3D12 backend | Planned | D3D12RHI implementation, real-world validation of RHI Vulkan-ism removal, BindGroup model redesign (root signature alignment) |
| 6 | macOS support | Planned | Cocoa PAL backend, MoltenVK first try, MetalRHI if MoltenVK incompatible |
| 7 | Mobile + console | Planned | Mobile PAL (Android/iOS), console SDK PAL backends |

Stage acceptance is binary: every numbered gate in the stage doc must pass before declaring it done. Acceptance does not promise the stage is "polished" — only that the stated goal is met without regressing earlier stages' gates.
