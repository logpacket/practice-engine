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
// Stage 1: simple monotonically-issued index. No generation counter; Debug
// builds use a slot state enum inside VulkanRHI for use-after-destroy detection
// (Architecture.md §3.2). Stage 2 introduces generation + deferred-delete.
template <class Tag>
struct RHIHandle {
    uint32 index;
    constexpr bool valid() const noexcept { return index != 0; }
    constexpr bool operator==(const RHIHandle&) const noexcept = default;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;
struct SwapchainTag;
struct CommandListTag;

using RHIBufferHandle      = RHIHandle<BufferTag>;
using RHIShaderHandle      = RHIHandle<ShaderTag>;
using RHIPipelineHandle    = RHIHandle<PipelineTag>;
using RHISwapchainHandle   = RHIHandle<SwapchainTag>;
using RHICommandListHandle = RHIHandle<CommandListTag>;

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
};

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

struct RHIGraphicsPipelineDesc {
    RHIShaderHandle  vertex_shader;
    RHIShaderHandle  fragment_shader;
    uint32           vertex_stride;
    EngineSpan<const RHIVertexAttribute> vertex_attributes;
    ERHIFormat       color_attachment_format;  // matches swapchain format
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

struct RHIRenderPassBeginInfo {
    // Stage 1: a single color attachment, no depth.
    RHISwapchainHandle swapchain;
    uint32             swapchain_image_index;   // returned by acquire-next-image
    RHIClearColor      clear_color;
    RHIRect            render_area;
};

}  // namespace pe
