// Samples/HelloTriangle - tutorial sample showing how to host the engine.
//
// Does the bootstrap *inline* so a new user can read it as a complete
// copy-paste starting point. Uses individual per-module includes (Core/...,
// RHI/..., ApplicationCore/..., Renderer/..., Launch/...) so each line tells
// the reader which engine module owns the symbol on the next line.

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

#include <string>

namespace sample {

DECLARE_LOG_CATEGORY(LogSample)

// Minimal IEngineContext. The host owns it on the stack for the lifetime of
// every loaded module.
class FSampleContext final : public pe::IEngineContext {
public:
    explicit FSampleContext(pe::IEngineAllocator& alloc) noexcept : alloc_(alloc) {}
    pe::IEngineAllocator& GetAllocator() noexcept override { return alloc_; }
    const pe::FPaths&     GetPaths() const noexcept override { return paths_; }

private:
    pe::IEngineAllocator& alloc_;
    pe::FPaths            paths_;
};

// Surface creation bridge. The PAL (loaded as ApplicationCore at runtime) knows
// how to create a Vulkan surface for the chosen window; VulkanRHI owns the
// VkInstance. This thunk lets VulkanRHI ask the PAL for a surface without
// either module importing the other.
struct FSurfaceBridge {
    pe::IPlatformApplication* pa;
    pe::IWindow*              window;
};

pe::EngineResult SurfaceCreateThunk(void* userdata, void* vk_instance, void** out_surface) {
    auto* bridge = static_cast<FSurfaceBridge*>(userdata);
    return bridge->pa->CreateGraphicsSurface(pe::EGraphicsBackend::Vulkan, vk_instance,
                                             bridge->window, out_surface);
}

}  // namespace sample

int main(int /*argc*/, char** /*argv*/) {
    using namespace pe;

    // --- 1. Core boot: logging, allocator, context -------------------------
    const std::string log_path = (FPaths::SavedDir() / "Logs" / "hello_triangle.log").string();
    log::Init(log_path.c_str());

    MallocAllocator       allocator;
    sample::FSampleContext ctx(allocator);

    ENGINE_LOG_INFO(sample::LogSample, "HelloTriangle sample starting");
    ENGINE_LOG_INFO(sample::LogSample, "EngineDir: {}", FPaths::EngineDir().string());

    // --- 2. PAL backend (configure-time STATIC link, ADR-0018) --------------
    // ENGINE_APP_BACKEND chose at CMake time which Platforms/<Backend>/ source
    // file's CreatePlatformApplication definition gets linked into this binary.
    IPlatformApplication* pa = CreatePlatformApplication();
    ENGINE_VERIFY(pa != nullptr);

    ENGINE_VERIFY(pa->Initialize().ok());

    // --- 3. Window ----------------------------------------------------------
    FWindowDesc wdesc{};
    wdesc.title  = "HelloTriangle Sample";
    wdesc.width  = 1024;
    wdesc.height = 768;

    IWindow* window = nullptr;
    ENGINE_VERIFY(pa->CreateWindow(wdesc, &window).ok());
    ENGINE_VERIFY(window != nullptr);

    const auto vk_exts = pa->GetRequiredGraphicsInstanceExtensions(EGraphicsBackend::Vulkan);

    // --- 4. Load VulkanRHI + create device ----------------------------------
    IModule* vkrhi = ModuleLoader::LoadModule("VulkanRHI", allocator, &ctx);
    ENGINE_VERIFY(vkrhi != nullptr);

    auto* backend = static_cast<IRHIBackendModule*>(
        vkrhi->QueryInterface(IRHIBackendModule::kInterfaceId));
    ENGINE_VERIFY(backend != nullptr);

    sample::FSurfaceBridge bridge{pa, window};
    RHIDeviceCreateDesc    dev_desc{};
    dev_desc.required_instance_extensions = vk_exts;
    dev_desc.enable_validation            = true;
    dev_desc.create_surface               = &sample::SurfaceCreateThunk;
    dev_desc.create_surface_userdata      = &bridge;

    IRHIDevice* device = nullptr;
    ENGINE_VERIFY(backend->CreateDevice(dev_desc, &device).ok());
    ENGINE_VERIFY(device != nullptr);

    // --- 5. Renderer init + main loop --------------------------------------
    FRenderer renderer;
    ENGINE_VERIFY(renderer.Init(*device, *window));

    ENGINE_LOG_INFO(sample::LogSample, "Entering main loop (close window or press ESC to exit)");
    while (!window->ShouldClose()) {
        pa->PumpEvents();
        renderer.RenderFrame();
    }
    ENGINE_LOG_INFO(sample::LogSample, "Main loop exited; tearing down");

    // --- 6. Teardown (reverse order; WaitIdle first) -----------------------
    device->WaitIdle();
    renderer.Shutdown();
    backend->DestroyDevice(device);
    ModuleLoader::UnloadModule(vkrhi);
    pa->DestroyWindow(window);
    pa->Shutdown();
    DestroyPlatformApplication(pa);

    ENGINE_LOG_INFO(sample::LogSample, "HelloTriangle exited cleanly");
    log::Shutdown();
    return 0;
}
