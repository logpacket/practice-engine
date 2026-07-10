// render_smoke - Stage 2 G12 (and §6.f G10) verification.
//
// Boots the full stack (PAL window + dlopened VulkanRHI + Renderer), renders
// a few frames through the two-pass sampling graph, then reads one composited
// swapchain pixel back and asserts its RGBA against the expected value.
// Validation-clean alone cannot prove the sampling path (a black target is
// also validation-clean) - the pixel readback is the objective check.

#include <Core/Assert.h>
#include <Core/IEngineContext.h>
#include <Core/Logging.h>
#include <Core/MallocAllocator.h>
#include <Core/Module.h>
#include <Core/ModuleLoader.h>
#include <Core/Paths.h>

#include <RHI/IRHIBackendModule.h>
#include <RHI/IRHIDevice.h>
#include <RHI/RHITypes.h>

#include <ApplicationCore/IPlatformApplication.h>
#include <ApplicationCore/IWindow.h>
#include <ApplicationCore/PlatformBackend.h>

#include <Renderer/Renderer.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

DECLARE_LOG_CATEGORY(LogRenderSmoke)

class FMinimalEngineContext final : public pe::IEngineContext {
public:
    explicit FMinimalEngineContext(pe::IEngineAllocator& alloc) noexcept : alloc_(alloc) {}
    pe::IEngineAllocator& GetAllocator() noexcept override { return alloc_; }
    const pe::FPaths&     GetPaths() const noexcept override { return paths_; }

private:
    pe::IEngineAllocator& alloc_;
    pe::FPaths            paths_;
};

struct FSurfaceBridge {
    pe::IPlatformApplication* pa;
    pe::IWindow*              window;
};

pe::EngineResult SurfaceCreateThunk(void* userdata, void* vk_instance, void** out_surface) {
    auto* bridge = static_cast<FSurfaceBridge*>(userdata);
    return bridge->pa->CreateGraphicsSurface(pe::EGraphicsBackend::Vulkan, vk_instance,
                                             bridge->window, out_surface);
}

// sRGB encode as UNORM8, matching what the sRGB render target stores.
pe::uint8 SrgbEncode(pe::float32 linear) {
    const pe::float32 encoded =
        linear <= 0.0031308f ? linear * 12.92f
                             : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<pe::uint8>(std::lround(encoded * 255.0f));
}

bool WithinTolerance(pe::uint8 actual, pe::uint8 expected, pe::int32 tolerance) {
    return std::abs(static_cast<pe::int32>(actual) - static_cast<pe::int32>(expected)) <= tolerance;
}

}  // namespace

int main() {
    const std::string log_path = (pe::FPaths::SavedDir() / "Logs" / "render_smoke.log").string();
    pe::log::Init(log_path.c_str());
    ENGINE_LOG_INFO(LogRenderSmoke, "render_smoke starting");

    pe::MallocAllocator   allocator;
    FMinimalEngineContext ctx(allocator);

    pe::IPlatformApplication* pa = pe::CreatePlatformApplication();
    ENGINE_VERIFY(pa != nullptr);
    ENGINE_VERIFY(pa->Initialize().ok());

    pe::FWindowDesc wdesc{};
    wdesc.title  = "render_smoke";
    wdesc.width  = 640;
    wdesc.height = 480;
    pe::IWindow* window = nullptr;
    ENGINE_VERIFY(pa->CreateWindow(wdesc, &window).ok() && window != nullptr);

    const auto vk_exts = pa->GetRequiredGraphicsInstanceExtensions(pe::EGraphicsBackend::Vulkan);

    pe::IModule* vkrhi = pe::ModuleLoader::LoadModule("VulkanRHI", allocator, &ctx);
    ENGINE_VERIFY(vkrhi != nullptr);
    auto* backend = static_cast<pe::IRHIBackendModule*>(
        vkrhi->QueryInterface(pe::IRHIBackendModule::kInterfaceId));
    ENGINE_VERIFY(backend != nullptr);

    FSurfaceBridge bridge{pa, window};
    pe::RHIDeviceCreateDesc dev_desc{};
    dev_desc.required_instance_extensions = vk_exts;
    dev_desc.enable_validation            = true;
    dev_desc.create_surface               = &SurfaceCreateThunk;
    dev_desc.create_surface_userdata      = &bridge;

    pe::IRHIDevice* device = nullptr;
    ENGINE_VERIFY(backend->CreateDevice(dev_desc, &device).ok() && device != nullptr);

    {
        pe::FRenderer renderer;
        ENGINE_VERIFY(renderer.Init(*device, *window));

        // Warm up a few frames (also exercises the multi-frame ring).
        for (int i = 0; i < 5; ++i) {
            pa->PumpEvents();
            renderer.RenderFrame();
        }

        // G12: pixel (8,8) lies outside the triangle, so the composite must
        // show the ScenePass clear color (0.1, 0.1, 0.15) sampled from the
        // offscreen target. sRGB encode/decode round-trips within 1-2 LSB.
        pe::uint8 rgba[4] = {0, 0, 0, 0};
        ENGINE_VERIFY(renderer.RenderFrameWithReadback(8, 8, rgba));

        const pe::uint8 expected[4] = {SrgbEncode(0.1f), SrgbEncode(0.1f), SrgbEncode(0.15f), 255};
        ENGINE_LOG_INFO(LogRenderSmoke,
                        "G12 readback rgba=({},{},{},{}) expected=({},{},{},{})",
                        rgba[0], rgba[1], rgba[2], rgba[3],
                        expected[0], expected[1], expected[2], expected[3]);
        for (int c = 0; c < 4; ++c) {
            if (!WithinTolerance(rgba[c], expected[c], 4)) {
                ENGINE_LOG_ERROR(LogRenderSmoke, "G12 FAIL: channel {} = {} (expected {})",
                                 c, rgba[c], expected[c]);
                pe::log::Shutdown();
                return 10;
            }
        }
        ENGINE_LOG_INFO(LogRenderSmoke, "G12 OK: composited pixel matches the sampled scene");

        device->WaitIdle();
        renderer.Shutdown();
    }

    backend->DestroyDevice(device);
    pe::ModuleLoader::UnloadModule(vkrhi);
    pa->DestroyWindow(window);
    pa->Shutdown();
    pe::DestroyPlatformApplication(pa);

    ENGINE_LOG_INFO(LogRenderSmoke, "render_smoke OK");
    pe::log::Shutdown();
    std::printf("render_smoke OK\n");
    return 0;
}
