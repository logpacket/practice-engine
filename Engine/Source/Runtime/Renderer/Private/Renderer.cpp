#include <Renderer/Renderer.h>

#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Paths.h>

#include <RHI/IRHICommandList.h>
#include <RHI/IRHIDevice.h>

#include <RenderGraph/RenderGraph.h>

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

// Offscreen scene-target formats (§6.d). Color matches the swapchain format
// so the scene pipeline serves both targets until the composite pipeline
// lands in §6.e.
constexpr ERHIFormat kOffscreenColorFormat = ERHIFormat::B8G8R8A8_SRGB;
constexpr ERHIFormat kOffscreenDepthFormat = ERHIFormat::D32_SFLOAT;

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

bool FRenderer::Init(IRHIDevice& device, IWindow& window,
                     ERHIPresentMode preferred_present_mode) {
    device_ = &device;
    window_ = &window;

    swapchain_width_  = window.GetWidth();
    swapchain_height_ = window.GetHeight();

    // 1. Swapchain
    RHISwapchainDesc sc_desc{};
    sc_desc.width                  = swapchain_width_;
    sc_desc.height                 = swapchain_height_;
    sc_desc.preferred_format       = ERHIFormat::B8G8R8A8_SRGB;
    sc_desc.preferred_present_mode = preferred_present_mode;  // §6.f
    sc_desc.image_count            = 3;  // spare image for MAILBOX / CPU run-ahead

    swapchain_ = device_->CreateSwapchain(sc_desc);
    if (!swapchain_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateSwapchain failed");
        return false;
    }

    // 2. Offscreen scene targets (§6.d)
    if (!CreateOffscreenTargets(swapchain_width_, swapchain_height_)) {
        return false;
    }

    // 3. Shaders - load from Binaries/<Platform>/<Config>/Shaders/Triangle.{vert,frag}.spv
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

    // 4. Scene pipeline: renders into the offscreen color+depth targets.
    constexpr std::array<RHIVertexAttribute, 2> attrs = {{
        {0, /*offset*/ 0,                 ERHIFormat::R32G32_SFLOAT},     // in_position
        {1, /*offset*/ sizeof(float) * 2, ERHIFormat::R32G32B32_SFLOAT},  // in_color
    }};

    RHIGraphicsPipelineDesc gp_desc{};
    gp_desc.vertex_shader           = vertex_shader_;
    gp_desc.fragment_shader         = fragment_shader_;
    gp_desc.vertex_stride           = sizeof(Vertex);
    gp_desc.vertex_attributes       = {attrs.data(), attrs.size()};
    gp_desc.color_attachment_format = kOffscreenColorFormat;
    gp_desc.depth_attachment_format = kOffscreenDepthFormat;

    scene_pipeline_ = device_->CreateGraphicsPipeline(gp_desc);
    if (!scene_pipeline_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateGraphicsPipeline (scene) failed");
        return false;
    }

    // 5. Composite pipeline (§6.e, ADR-0024): fullscreen triangle sampling
    //    the offscreen color into the swapchain. No vertex input; one
    //    combined-image-sampler at slot 0.
    auto cvs_bytes = LoadShaderBytes(FPaths::ShadersDir() / "Composite.vert.spv");
    auto cfs_bytes = LoadShaderBytes(FPaths::ShadersDir() / "Composite.frag.spv");
    if (cvs_bytes.empty() || cfs_bytes.empty()) {
        ENGINE_LOG_ERROR(LogRenderer, "Composite shader binaries missing");
        return false;
    }

    RHIShaderDesc cvs_desc{};
    cvs_desc.stage = ERHIShaderStage::Vertex;
    cvs_desc.spirv = {cvs_bytes.data(), cvs_bytes.size()};
    composite_vs_ = device_->CreateShader(cvs_desc);

    RHIShaderDesc cfs_desc{};
    cfs_desc.stage = ERHIShaderStage::Fragment;
    cfs_desc.spirv = {cfs_bytes.data(), cfs_bytes.size()};
    composite_fs_ = device_->CreateShader(cfs_desc);

    if (!composite_vs_.valid() || !composite_fs_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateShader (composite) failed");
        return false;
    }

    constexpr RHIDescriptorBinding kCompositeBindings[] = {
        {0, ERHIDescriptorKind::CombinedImageSampler},
    };
    RHIGraphicsPipelineDesc cp_desc{};
    cp_desc.vertex_shader           = composite_vs_;
    cp_desc.fragment_shader         = composite_fs_;
    cp_desc.vertex_stride           = 0;  // fullscreen triangle from gl_VertexIndex
    cp_desc.vertex_attributes       = {nullptr, 0};
    cp_desc.color_attachment_format = ERHIFormat::B8G8R8A8_SRGB;  // swapchain format
    cp_desc.descriptor_bindings     = {kCompositeBindings, 1};

    composite_pipeline_ = device_->CreateGraphicsPipeline(cp_desc);
    if (!composite_pipeline_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateGraphicsPipeline (composite) failed");
        return false;
    }

    sampler_ = device_->CreateSampler(RHISamplerDesc{});
    if (!sampler_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateSampler failed");
        return false;
    }

    // 6. Vertex buffer (host-visible, uploaded once at creation time)
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

bool FRenderer::CreateOffscreenTargets(uint32 width, uint32 height) {
    RHITextureDesc color_desc{};
    color_desc.width  = width;
    color_desc.height = height;
    color_desc.format = kOffscreenColorFormat;
    color_desc.usage  = ERHITextureUsage::RenderTarget | ERHITextureUsage::Sampled;
    offscreen_color_ = device_->CreateTexture(color_desc);

    RHITextureDesc depth_desc{};
    depth_desc.width  = width;
    depth_desc.height = height;
    depth_desc.format = kOffscreenDepthFormat;
    depth_desc.usage  = ERHITextureUsage::DepthStencil;
    offscreen_depth_ = device_->CreateTexture(depth_desc);

    if (!offscreen_color_.valid() || !offscreen_depth_.valid()) {
        ENGINE_LOG_ERROR(LogRenderer, "CreateTexture (offscreen targets) failed");
        return false;
    }
    return true;
}

void FRenderer::DestroyOffscreenTargets() {
    if (offscreen_color_.valid()) { device_->Destroy(offscreen_color_); offscreen_color_ = {}; }
    if (offscreen_depth_.valid()) { device_->Destroy(offscreen_depth_); offscreen_depth_ = {}; }
}

void FRenderer::ExecuteScenePassThunk(IRHICommandList& cmd, void* userdata) {
    static_cast<FRenderer*>(userdata)->RecordScenePass(cmd);
}

void FRenderer::ExecuteCompositePassThunk(IRHICommandList& cmd, void* userdata) {
    static_cast<FRenderer*>(userdata)->RecordCompositePass(cmd);
}

void FRenderer::RecordScenePass(IRHICommandList& cmd) {
    RHIRenderPassColorAttachment color_att{};
    color_att.texture       = offscreen_color_;
    color_att.clear.rgba[0] = 0.1f;
    color_att.clear.rgba[1] = 0.1f;
    color_att.clear.rgba[2] = 0.15f;
    color_att.clear.rgba[3] = 1.0f;

    RHIRenderPassBeginInfo pass{};
    pass.color_attachments  = {&color_att, 1};
    pass.depth.texture      = offscreen_depth_;
    pass.depth.clear_depth  = 1.0f;
    pass.render_area.width  = swapchain_width_;
    pass.render_area.height = swapchain_height_;

    cmd.BeginRenderPass(pass);

    RHIViewport vp{};
    vp.width  = static_cast<float>(swapchain_width_);
    vp.height = static_cast<float>(swapchain_height_);
    vp.max_depth = 1.0f;
    cmd.SetViewport(vp);

    RHIRect scissor{};
    scissor.width  = swapchain_width_;
    scissor.height = swapchain_height_;
    cmd.SetScissor(scissor);

    cmd.SetPipeline(scene_pipeline_);
    cmd.SetVertexBuffer(vertex_buffer_, 0);
    cmd.Draw(3, 0);

    cmd.EndRenderPass();
}

void FRenderer::RecordCompositePass(IRHICommandList& cmd) {
    RHIRenderPassColorAttachment color_att{};
    color_att.texture       = frame_backbuffer_;
    color_att.clear.rgba[0] = 0.1f;
    color_att.clear.rgba[1] = 0.1f;
    color_att.clear.rgba[2] = 0.15f;
    color_att.clear.rgba[3] = 1.0f;

    RHIRenderPassBeginInfo pass{};
    pass.color_attachments  = {&color_att, 1};
    pass.render_area.width  = swapchain_width_;
    pass.render_area.height = swapchain_height_;

    cmd.BeginRenderPass(pass);

    RHIViewport vp{};
    vp.width  = static_cast<float>(swapchain_width_);
    vp.height = static_cast<float>(swapchain_height_);
    vp.max_depth = 1.0f;
    cmd.SetViewport(vp);

    RHIRect scissor{};
    scissor.width  = swapchain_width_;
    scissor.height = swapchain_height_;
    cmd.SetScissor(scissor);

    // Fullscreen draw sampling the offscreen scene color (§6.e). The
    // descriptor set behind SetTexture is static (ADR-0024): written once per
    // (texture, sampler) pair, only bound here.
    cmd.SetPipeline(composite_pipeline_);
    cmd.SetTexture(0, offscreen_color_, sampler_);
    cmd.Draw(3, 0);

    cmd.EndRenderPass();
}

void FRenderer::RenderFrame() {
    RenderFrameInternal(nullptr);
}

bool FRenderer::RenderFrameWithReadback(uint32 x, uint32 y, uint8 out_rgba[4]) {
    FReadbackRequest req{};
    req.x        = x;
    req.y        = y;
    req.out_rgba = out_rgba;
    RenderFrameInternal(&req);
    return req.ok;
}

bool FRenderer::RecreateSizedResources() {
    const uint32 w = window_->GetWidth();
    const uint32 h = window_->GetHeight();
    if (w == 0 || h == 0) { return false; }  // minimized: wait for a real size

    if (!device_->RecreateSwapchain(swapchain_, w, h).ok()) { return false; }
    // The offscreen targets track the swapchain size; their destroy goes
    // through the deferred queue and the composite descriptor set is
    // invalidated with them (rewritten on next SetTexture).
    DestroyOffscreenTargets();
    if (!CreateOffscreenTargets(w, h)) { return false; }

    swapchain_width_  = w;
    swapchain_height_ = h;
    ENGINE_LOG_INFO(LogRenderer, "Recreated swapchain + offscreen targets ({}x{})", w, h);
    return true;
}

void FRenderer::ForceRecreate() {
    if (!initialized_) { return; }
    RecreateSizedResources();
}

void FRenderer::RenderFrameInternal(FReadbackRequest* readback) {
    if (!initialized_) { return; }

    // Resize signal (ADR-0026): recreate before acquiring.
    if (window_->ConsumeResized()) {
        if (!RecreateSizedResources()) { return; }
    }

    const RHIAcquiredImage acquired = device_->AcquireNextSwapchainImage(swapchain_);
    if (acquired.needs_recreate) {
        // Vulkan's out-of-date signal: recreate and skip this frame.
        RecreateSizedResources();
        return;
    }
    const uint32 image_index = acquired.image_index;
    frame_backbuffer_ = device_->GetSwapchainImageTexture(swapchain_, image_index);

    // Build the frame graph (ADR-0023): ScenePass renders the triangle into
    // the offscreen targets; CompositePass consumes the offscreen color and
    // writes the backbuffer. Barriers are computed by the graph (ADR-0022).
    graph_.Reset();

    const RGResourceAccess scene_writes[] = {
        {offscreen_color_, ERHIResourceState::RenderTarget},
        {offscreen_depth_, ERHIResourceState::DepthAttachment},
    };
    RGPassDesc scene{};
    scene.name     = "ScenePass";
    scene.writes   = {scene_writes, 2};
    scene.execute  = &FRenderer::ExecuteScenePassThunk;
    scene.userdata = this;
    graph_.AddPass(scene);

    const RGResourceAccess composite_reads[]  = {
        {offscreen_color_, ERHIResourceState::ShaderResource},
    };
    const RGResourceAccess composite_writes[] = {
        {frame_backbuffer_, ERHIResourceState::RenderTarget},
    };
    RGPassDesc composite{};
    composite.name     = "CompositePass";
    composite.reads    = {composite_reads, 1};
    composite.writes   = {composite_writes, 1};
    composite.execute  = &FRenderer::ExecuteCompositePassThunk;
    composite.userdata = this;
    graph_.AddPass(composite);

    graph_.SetFinalState(frame_backbuffer_, ERHIResourceState::Present);

    RHICommandListHandle cmd_h = device_->AcquireCommandList();
    IRHICommandList*     cmd   = device_->Lock(cmd_h);

    cmd->Begin();
    // Barrier introspection (G7 tool): log the inferred schedule once.
    graph_.Execute(*cmd, /*log_barriers=*/frame_number_ == 0);
    cmd->End();

    // Boundary submit (ADR-0027): the present owner supplies the swapchain's
    // binary semaphores and the frame's timeline value.
    const RHISemaphore render_finished =
        device_->GetRenderFinishedSemaphore(swapchain_, image_index);

    RHISubmitInfo submit{};
    submit.wait                  = {&acquired.image_available, 1};
    submit.signal                = {&render_finished, 1};
    submit.timeline_signal_value = ++frame_number_;

    device_->Submit(cmd_h, submit);

    // G12: read the composited pixel back after the boundary submit, before
    // present. The graph left the backbuffer in Present state; the readback
    // restores it, so present below stays valid.
    if (readback != nullptr) {
        readback->ok = device_->ReadbackTexturePixel(frame_backbuffer_,
                                                     ERHIResourceState::Present,
                                                     readback->x, readback->y,
                                                     readback->out_rgba).ok();
    }

    if (!device_->Present(swapchain_, image_index).ok()) {
        // Out-of-date at present: recreate; the next frame renders fresh.
        RecreateSizedResources();
    }
    frame_backbuffer_ = {};
}

void FRenderer::Shutdown() {
    if (device_ == nullptr) { return; }

    device_->WaitIdle();

    if (vertex_buffer_.valid())      { device_->Destroy(vertex_buffer_);      vertex_buffer_      = {}; }
    if (composite_pipeline_.valid()) { device_->Destroy(composite_pipeline_); composite_pipeline_ = {}; }
    if (composite_fs_.valid())       { device_->Destroy(composite_fs_);       composite_fs_       = {}; }
    if (composite_vs_.valid())       { device_->Destroy(composite_vs_);       composite_vs_       = {}; }
    if (sampler_.valid())            { device_->Destroy(sampler_);            sampler_            = {}; }
    if (scene_pipeline_.valid())     { device_->Destroy(scene_pipeline_);     scene_pipeline_     = {}; }
    if (fragment_shader_.valid())    { device_->Destroy(fragment_shader_);    fragment_shader_    = {}; }
    if (vertex_shader_.valid())      { device_->Destroy(vertex_shader_);      vertex_shader_      = {}; }
    DestroyOffscreenTargets();
    device_->WaitIdle();  // drain deferred deletes before the swapchain goes
    if (swapchain_.valid())       { device_->Destroy(swapchain_);       swapchain_       = {}; }

    initialized_ = false;
    device_ = nullptr;
    window_ = nullptr;
}

}  // namespace pe
