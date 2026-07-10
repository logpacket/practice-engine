// RHITypes.h - Stage 1 RHI value types: handles, enums, descriptors.
//
// Stage 1 surface only - sufficient to draw one triangle through dynamic rendering.
// Architecture.md §3.2-3.6 lists every field that should live here.
//
// Convention: engine code uses the integer aliases from <Core/Types.h>
// (uint32, int32, uint64, …) instead of raw stdint types. See ADR-0016 +
// CMake/check_engine_type_aliases.sh for the build-time gate.

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>

namespace pe {

// --- Handles ----------------------------------------------------------------
// Stage 2 (ADR-0021): 64-bit handle {index, generation}. The backend pool
// issues a per-slot generation; Get/Remove on a stale (destroyed) handle is a
// generation mismatch and a Debug FATAL, not a silent alias. Index 0 stays the
// invalid sentinel (a zero-initialized handle is never valid).
template <class Tag>
struct RHIHandle {
    uint32 index;
    uint32 generation;
    constexpr bool valid() const noexcept { return index != 0; }
    constexpr bool operator==(const RHIHandle&) const noexcept = default;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;
struct SwapchainTag;
struct CommandListTag;
struct TextureTag;
struct SamplerTag;

using RHIBufferHandle      = RHIHandle<BufferTag>;
using RHIShaderHandle      = RHIHandle<ShaderTag>;
using RHIPipelineHandle    = RHIHandle<PipelineTag>;
using RHISwapchainHandle   = RHIHandle<SwapchainTag>;
using RHICommandListHandle = RHIHandle<CommandListTag>;
using RHITextureHandle     = RHIHandle<TextureTag>;
using RHISamplerHandle     = RHIHandle<SamplerTag>;

// --- Submit synchronization (ADR-0020 / ADR-0027) ----------------------------

// Opaque reference to a backend binary semaphore (VkSemaphore on Vulkan).
// Callers never create these; they are obtained from the swapchain accessors
// (AcquireNextSwapchainImage / GetRenderFinishedSemaphore) and passed back
// through RHISubmitInfo on the boundary submit only.
struct RHISemaphore {
    uint64 opaque = 0;
    constexpr bool valid() const noexcept { return opaque != 0; }
};

// Explicit submit sync contract (ADR-0027). Interior (non-swapchain) submits
// pass empty wait/signal spans. The one boundary submit per frame that writes
// the swapchain waits image_available and signals render_finished, both
// supplied by the present owner (the Renderer).
//
// timeline_signal_value > 0 makes this submit signal the device frame timeline
// (ADR-0020). Exactly one submit per frame must signal, with a value that
// increases by 1 each frame - frame pacing and deferred-delete key off it.
struct RHISubmitInfo {
    EngineSpan<const RHISemaphore> wait                  = {nullptr, 0};
    EngineSpan<const RHISemaphore> signal                = {nullptr, 0};
    uint64                         timeline_signal_value = 0;
};

// Result of AcquireNextSwapchainImage. When needs_recreate is true the
// swapchain is out of date: no image was acquired, image_available is invalid,
// and the caller must RecreateSwapchain before rendering (§6.f).
struct RHIAcquiredImage {
    uint32       image_index    = 0;
    RHISemaphore image_available;
    bool         needs_recreate = false;
};

// --- Enums ------------------------------------------------------------------
enum class ERHIFormat : uint16 {
    Unknown = 0,
    R8G8B8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_UNORM,
    B8G8R8A8_SRGB,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
    D32_SFLOAT,  // depth (Stage 2)
};

// Backend-neutral resource states (ADR-0022). On Vulkan each maps to a
// (VkImageLayout, VkPipelineStageFlags2, VkAccessFlags2) triple; on D3D12 to
// D3D12_RESOURCE_STATES. Only the RenderGraph emits transitions (ADR-0023).
enum class ERHIResourceState : uint8 {
    Undefined = 0,
    RenderTarget,
    DepthAttachment,
    ShaderResource,
    CopySrc,
    CopyDst,
    Present,
};

enum class ERHITextureUsage : uint32 {
    None         = 0,
    RenderTarget = 1u << 0,
    DepthStencil = 1u << 1,
    Sampled      = 1u << 2,
    CopySrc      = 1u << 3,
    CopyDst      = 1u << 4,
};

constexpr ERHITextureUsage operator|(ERHITextureUsage a, ERHITextureUsage b) noexcept {
    return static_cast<ERHITextureUsage>(static_cast<uint32>(a) | static_cast<uint32>(b));
}
constexpr bool any(ERHITextureUsage a, ERHITextureUsage mask) noexcept {
    return (static_cast<uint32>(a) & static_cast<uint32>(mask)) != 0;
}

enum class ERHIFilter      : uint8 { Nearest = 0, Linear };
enum class ERHIAddressMode : uint8 { Repeat = 0, MirroredRepeat, ClampToEdge, ClampToBorder };
enum class ERHIMipmapMode  : uint8 { Nearest = 0, Linear };

enum class ERHIPresentMode : uint8 {
    FIFO = 0,        // V-Sync, guaranteed available (Stage 1 default)
    MAILBOX,         // tear-free + lower latency (Stage 2)
    Immediate,       // tearing OK (Stage 2)
};

enum class ERHIShaderStage : uint8 {
    Vertex = 0,
    Fragment,
    Compute,
};

enum class ERHIBufferUsage : uint32 {
    None        = 0,
    VertexBuffer = 1u << 0,
    IndexBuffer  = 1u << 1,
    UniformBuffer = 1u << 2,
    Storage      = 1u << 3,
};

constexpr ERHIBufferUsage operator|(ERHIBufferUsage a, ERHIBufferUsage b) noexcept {
    return static_cast<ERHIBufferUsage>(static_cast<uint32>(a) | static_cast<uint32>(b));
}
constexpr bool any(ERHIBufferUsage a, ERHIBufferUsage mask) noexcept {
    return (static_cast<uint32>(a) & static_cast<uint32>(mask)) != 0;
}

// --- Descriptors ------------------------------------------------------------

// Surface creation callback. Avoids coupling RHI to ApplicationCore: the caller
// (Launch) supplies a thunk that wraps IPlatformApplication::CreateGraphicsSurface
// for the relevant EGraphicsBackend. VulkanRHI calls this from CreateSwapchain
// to obtain a backend-native surface handle.
//
// userdata               : opaque pointer passed back unchanged
// backend_instance_opaque: the backend's native instance handle (VkInstance for Vulkan)
// out_surface_opaque     : on success, *out points to the backend's native surface
//                          (VkSurfaceKHR for Vulkan), cast to void*
using PFN_RHICreateSurface = EngineResult (*)(void* userdata,
                                              void* backend_instance_opaque,
                                              void** out_surface_opaque);

struct RHIDeviceCreateDesc {
    // Caller (Launch) collects required instance extensions from IPlatformApplication and
    // passes them here. VulkanRHI unions them with its own (debug_utils in Debug).
    EngineSpan<const char* const> required_instance_extensions;
    bool                          enable_validation = false;

    // Surface creation callback - required if you intend to call CreateSwapchain
    // on the returned device. May be left null for headless device creation
    // (as in the §6.c rhi_smoke test).
    PFN_RHICreateSurface create_surface          = nullptr;
    void*                create_surface_userdata = nullptr;
};

struct RHIBufferDesc {
    uint64          size_bytes;
    ERHIBufferUsage usage;
    // Optional initial-data upload performed at creation (host-visible memory).
    // Stage 1 has no MapBuffer in the public interface; the only way to populate
    // a buffer in §6.b-e is via this field (Architecture.md §6.e).
    const void*     initial_data    = nullptr;
    uint64          initial_size    = 0;
};

struct RHIShaderDesc {
    ERHIShaderStage          stage;
    EngineSpan<const uint8>  spirv;   // raw SPIR-V bytes
    const char*              entry_point = "main";
};

struct RHIVertexAttribute {
    uint32     location;
    uint32     offset;
    ERHIFormat format;
};

struct RHITextureDesc {
    uint32           width  = 0;
    uint32           height = 0;
    ERHIFormat       format = ERHIFormat::Unknown;
    ERHITextureUsage usage  = ERHITextureUsage::None;
};

struct RHISamplerDesc {
    ERHIFilter      min_filter   = ERHIFilter::Linear;
    ERHIFilter      mag_filter   = ERHIFilter::Linear;
    ERHIAddressMode address_mode = ERHIAddressMode::ClampToEdge;
    ERHIMipmapMode  mipmap_mode  = ERHIMipmapMode::Nearest;
};

// A layout/access transition (ADR-0022). The swapchain image is addressed as
// a borrowed texture via IRHIDevice::GetSwapchainImageTexture, so barriers
// operate uniformly over textures and swapchain images.
struct RHIResourceBarrier {
    RHITextureHandle  texture;
    ERHIResourceState before = ERHIResourceState::Undefined;
    ERHIResourceState after  = ERHIResourceState::Undefined;
};

struct RHIGraphicsPipelineDesc {
    RHIShaderHandle  vertex_shader;
    RHIShaderHandle  fragment_shader;
    uint32           vertex_stride;
    EngineSpan<const RHIVertexAttribute> vertex_attributes;
    ERHIFormat       color_attachment_format;  // matches the render target format
    // Unknown = no depth attachment; a depth format enables depth test+write.
    ERHIFormat       depth_attachment_format = ERHIFormat::Unknown;
};

struct RHISwapchainDesc {
    // Window dimensions in pixels (the swapchain matches if the surface allows).
    uint32          width  = 0;
    uint32          height = 0;
    ERHIFormat      preferred_format       = ERHIFormat::B8G8R8A8_SRGB;
    ERHIPresentMode preferred_present_mode = ERHIPresentMode::FIFO;
    uint32          image_count            = 2;
    // The backend obtains the surface via the create_surface callback stored on
    // the device at CreateDevice time. No native handles are passed here.
};

struct RHIRect {
    int32   x      = 0;
    int32   y      = 0;
    uint32  width  = 0;
    uint32  height = 0;
};

struct RHIViewport {
    float32 x        = 0.0f;
    float32 y        = 0.0f;
    float32 width    = 0.0f;
    float32 height   = 0.0f;
    float32 min_depth = 0.0f;
    float32 max_depth = 1.0f;
};

struct RHIClearColor {
    float32 rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

// Generalized render pass target (§6.c): texture attachments instead of a
// swapchain reference. A pass targeting the swapchain uses the borrowed
// texture from GetSwapchainImageTexture. Attachments are always cleared on
// load in Stage 2 (every pass fully overwrites its targets).
struct RHIRenderPassColorAttachment {
    RHITextureHandle texture;
    RHIClearColor    clear;
};

struct RHIRenderPassDepthAttachment {
    RHITextureHandle texture;       // invalid handle = no depth attachment
    float32          clear_depth = 1.0f;
};

struct RHIRenderPassBeginInfo {
    EngineSpan<const RHIRenderPassColorAttachment> color_attachments = {nullptr, 0};
    RHIRenderPassDepthAttachment                   depth;
    RHIRect                                        render_area;
};

}  // namespace pe
