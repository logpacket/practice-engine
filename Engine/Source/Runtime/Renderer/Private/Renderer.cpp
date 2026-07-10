#include <Renderer/Renderer.h>

#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Paths.h>

#include <RHI/IRHICommandList.h>
#include <RHI/IRHIDevice.h>

#include <ApplicationCore/IWindow.h>

#include <array>
#include <fstream>
#include <vector>

namespace pe {

DECLARE_LOG_CATEGORY(LogRenderer)

namespace {

struct Vertex {
    float pos[2];
    float color[3];
};

constexpr Vertex kTriangle[3] = {
    {{-0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}},  // bottom-left  (red)   - NDC y+ is down in Vulkan dynamic rendering default
    {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},  // bottom-right (green)
    {{ 0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}},  // top-middle   (blue)
};

std::vector<uint8> LoadShaderBytes(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        ENGINE_LOG_ERROR(LogRenderer, "Failed to open shader: {}", path.string());
        return {};
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8> bytes(static_cast<usize>(size));
    if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) {
        ENGINE_LOG_ERROR(LogRenderer, "Failed to read shader: {}", path.string());
        return {};
    }
    return bytes;
}

}  // namespace

FRenderer::FRenderer()  = default;
FRenderer::~FRenderer() { Shutdown(); }

bool FRenderer::Init(IRHIDevice& device, IWindow& window) {
    device_ = &device;
    window_ = &window;

    swapchain_width_  = window.GetWidth();
    swapchain_height_ = window.GetHeight();

    // 1. Swapchain
    RHISwapchainDesc sc_desc{};
    sc_desc.width                  = swapchain_width_;
    sc_desc.height                 = swapchain_height_;
    sc_desc.preferred_format       = ERHIFormat::B8G8R8A8_SRGB;
    sc_desc.preferred_present_mode = ERHIPresentMode::FIFO;
    sc_desc.image_count            = 2;

    swapchain_ = device_->CreateSwapchain(sc_desc);
    if (!swapchain_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateSwapchain failed");
        return false;
    }

    // 2. Shaders - load from Binaries/<Platform>/<Config>/Shaders/Triangle.{vert,frag}.spv
    const auto vs_path = FPaths::ShadersDir() / "Triangle.vert.spv";
    const auto fs_path = FPaths::ShadersDir() / "Triangle.frag.spv";

    auto vs_bytes = LoadShaderBytes(vs_path);
    auto fs_bytes = LoadShaderBytes(fs_path);
    if (vs_bytes.empty() || fs_bytes.empty()) {
        ENGINE_LOG_ERROR(LogRenderer, "Shader binaries missing - did the Shaders build step run?");
        return false;
    }

    RHIShaderDesc vs_desc{};
    vs_desc.stage      = ERHIShaderStage::Vertex;
    vs_desc.spirv      = {vs_bytes.data(), vs_bytes.size()};
    vs_desc.entry_point = "main";
    vertex_shader_ = device_->CreateShader(vs_desc);

    RHIShaderDesc fs_desc{};
    fs_desc.stage      = ERHIShaderStage::Fragment;
    fs_desc.spirv      = {fs_bytes.data(), fs_bytes.size()};
    fs_desc.entry_point = "main";
    fragment_shader_ = device_->CreateShader(fs_desc);

    if (!vertex_shader_.valid() || !fragment_shader_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateShader failed");
        return false;
    }

    // 3. Graphics pipeline (uses dynamic rendering, color attachment matches swapchain format)
    constexpr std::array<RHIVertexAttribute, 2> attrs = {{
        {0, /*offset*/ 0,                 ERHIFormat::R32G32_SFLOAT},     // in_position
        {1, /*offset*/ sizeof(float) * 2, ERHIFormat::R32G32B32_SFLOAT},  // in_color
    }};

    RHIGraphicsPipelineDesc gp_desc{};
    gp_desc.vertex_shader            = vertex_shader_;
    gp_desc.fragment_shader          = fragment_shader_;
    gp_desc.vertex_stride            = sizeof(Vertex);
    gp_desc.vertex_attributes        = {attrs.data(), attrs.size()};
    gp_desc.color_attachment_format  = ERHIFormat::B8G8R8A8_SRGB;

    pipeline_ = device_->CreateGraphicsPipeline(gp_desc);
    if (!pipeline_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateGraphicsPipeline failed");
        return false;
    }

    // 4. Vertex buffer (host-visible, uploaded once at creation time)
    RHIBufferDesc vb_desc{};
    vb_desc.size_bytes   = sizeof(kTriangle);
    vb_desc.usage        = ERHIBufferUsage::VertexBuffer;
    vb_desc.initial_data = kTriangle;
    vb_desc.initial_size = sizeof(kTriangle);

    vertex_buffer_ = device_->CreateBuffer(vb_desc);
    if (!vertex_buffer_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateBuffer (vertex) failed");
        return false;
    }

    initialized_ = true;
    ENGINE_LOG_INFO(LogRenderer, "FRenderer initialized ({}x{})", swapchain_width_, swapchain_height_);
    return true;
}

void FRenderer::RenderFrame() {
    if (!initialized_) { return; }

    const uint32 image_index = device_->AcquireNextSwapchainImage(swapchain_);

    RHICommandListHandle cmd_h = device_->AcquireCommandList();
    IRHICommandList*     cmd   = device_->Lock(cmd_h);

    cmd->Begin();
    cmd->TransitionToRenderTarget(swapchain_, image_index);

    RHIRenderPassBeginInfo pass{};
    pass.swapchain             = swapchain_;
    pass.swapchain_image_index = image_index;
    pass.clear_color.rgba[0]   = 0.1f;
    pass.clear_color.rgba[1]   = 0.1f;
    pass.clear_color.rgba[2]   = 0.15f;
    pass.clear_color.rgba[3]   = 1.0f;
    pass.render_area.x         = 0;
    pass.render_area.y         = 0;
    pass.render_area.width     = swapchain_width_;
    pass.render_area.height    = swapchain_height_;

    cmd->BeginRenderPass(pass);

    RHIViewport vp{};
    vp.x         = 0.0f;
    vp.y         = 0.0f;
    vp.width     = static_cast<float>(swapchain_width_);
    vp.height    = static_cast<float>(swapchain_height_);
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    cmd->SetViewport(vp);

    RHIRect scissor{};
    scissor.x      = 0;
    scissor.y      = 0;
    scissor.width  = swapchain_width_;
    scissor.height = swapchain_height_;
    cmd->SetScissor(scissor);

    cmd->SetPipeline(pipeline_);
    cmd->SetVertexBuffer(vertex_buffer_, 0);
    cmd->Draw(3, 0);

    cmd->EndRenderPass();
    cmd->TransitionToPresent(swapchain_, image_index);
    cmd->End();

    device_->Submit(cmd_h);
    device_->Present(swapchain_, image_index);
}

void FRenderer::Shutdown() {
    if (device_ == nullptr) { return; }

    device_->WaitIdle();

    if (vertex_buffer_.valid())   { device_->Destroy(vertex_buffer_);   vertex_buffer_   = {}; }
    if (pipeline_.valid())        { device_->Destroy(pipeline_);        pipeline_        = {}; }
    if (fragment_shader_.valid()) { device_->Destroy(fragment_shader_); fragment_shader_ = {}; }
    if (vertex_shader_.valid())   { device_->Destroy(vertex_shader_);   vertex_shader_   = {}; }
    if (swapchain_.valid())       { device_->Destroy(swapchain_);       swapchain_       = {}; }

    initialized_ = false;
    device_ = nullptr;
    window_ = nullptr;
}

}  // namespace pe
