// LaunchEngineLoop.cpp - the §6.f bootstrap sequence.
//
// Owns IEngineContext. PAL backend is configure-time STATIC-linked (ADR-0018)
// and constructed via pe::CreatePlatformApplication(). RHI backend is
// runtime-dlopen-loaded (ADR-0002). The surface-creation callback bridges
// PAL <-> VulkanRHI without coupling the two modules directly.

#include <Launch/LaunchEngineLoop.h>

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

#include <Asset/AssetSystem.h>
#include <Renderer/Renderer.h>

#include <string>

namespace pe {

DECLARE_LOG_CATEGORY(LogLaunch)

namespace {

// Concrete FEngineContext owned by LaunchEngineLoop on the stack.
class FEngineContext final : public IEngineContext {
public:
    explicit FEngineContext(IEngineAllocator& alloc) noexcept : alloc_(alloc) {}
    IEngineAllocator& GetAllocator() noexcept override { return alloc_; }
    const FPaths&     GetPaths() const noexcept override { return paths_; }

private:
    IEngineAllocator& alloc_;
    FPaths            paths_;
};

// Bridge userdata threaded through the surface-creation callback. Owned on
// LaunchEngineLoop's stack; lifetime contains the device's lifetime.
struct FSurfaceCreateBridge {
    IPlatformApplication* pa;
    IWindow*              window;
};

// Free function so it can decay to PFN_RHICreateSurface. Userdata carries
// the PA+window pair the closure needs.
EngineResult SurfaceCreateThunk(void* userdata, void* vk_instance, void** out_surface) {
    auto* bridge = static_cast<FSurfaceCreateBridge*>(userdata);
    return bridge->pa->CreateGraphicsSurface(EGraphicsBackend::Vulkan, vk_instance,
                                             bridge->window, out_surface);
}

}  // namespace

int LaunchEngineLoop(int /*argc*/, char** /*argv*/) {
    // 1. Core boot: spdlog + paths + allocator + context.
    const std::string log_path = (FPaths::SavedDir() / "Logs" / "engine.log").string();
    log::Init(log_path.c_str());

    MallocAllocator allocator;
    FEngineContext  ctx(allocator);

    ENGINE_LOG_INFO(LogLaunch, "LaunchEngineLoop starting");
    ENGINE_LOG_INFO(LogLaunch, "EngineDir: {}", FPaths::EngineDir().string());

    // 2. PAL backend - configure-time STATIC link (ADR-0018). CMake's
    //    ENGINE_APP_BACKEND option chose which Platforms/<Backend>/ source
    //    file's CreatePlatformApplication definition gets linked.
    IPlatformApplication* pa = CreatePlatformApplication();
    if (pa == nullptr) {
        ENGINE_LOG_ERROR(LogLaunch, "CreatePlatformApplication failed");
        log::Shutdown();
        return -1;
    }

    if (!pa->Initialize().ok()) {
        ENGINE_LOG_ERROR(LogLaunch, "IPlatformApplication::Initialize failed");
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -2;
    }

    // 3. Create window + collect Vulkan instance extensions from the PAL.
    FWindowDesc wdesc{};
    wdesc.title  = "practice-engine";
    wdesc.width  = 1280;
    wdesc.height = 720;

    IWindow* window = nullptr;
    if (!pa->CreateWindow(wdesc, &window).ok() || window == nullptr) {
        ENGINE_LOG_ERROR(LogLaunch, "CreateWindow failed");
        pa->Shutdown();
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -3;
    }
    const auto vk_exts = pa->GetRequiredGraphicsInstanceExtensions(EGraphicsBackend::Vulkan);

    // 4. Load VulkanRHI (runtime-dlopen, ADR-0002) + create device with
    //    surface callback.
    IModule* vkrhi_mod = ModuleLoader::LoadModule("VulkanRHI", allocator, &ctx);
    if (vkrhi_mod == nullptr) {
        ENGINE_LOG_ERROR(LogLaunch, "LoadModule('VulkanRHI') failed");
        pa->DestroyWindow(window);
        pa->Shutdown();
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -4;
    }
    auto* backend = static_cast<IRHIBackendModule*>(
        vkrhi_mod->QueryInterface(IRHIBackendModule::kInterfaceId));
    if (backend == nullptr) {
        ENGINE_LOG_ERROR(LogLaunch, "VulkanRHI does not expose IRHIBackendModule");
        ModuleLoader::UnloadModule(vkrhi_mod);
        pa->DestroyWindow(window);
        pa->Shutdown();
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -5;
    }

    FSurfaceCreateBridge bridge{pa, window};
    RHIDeviceCreateDesc  dev_desc{};
    dev_desc.required_instance_extensions = vk_exts;
    dev_desc.enable_validation            = true;
    dev_desc.create_surface               = &SurfaceCreateThunk;
    dev_desc.create_surface_userdata      = &bridge;

    IRHIDevice* device = nullptr;
    if (!backend->CreateDevice(dev_desc, &device).ok() || device == nullptr) {
        ENGINE_LOG_ERROR(LogLaunch, "CreateDevice failed");
        ModuleLoader::UnloadModule(vkrhi_mod);
        pa->DestroyWindow(window);
        pa->Shutdown();
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -6;
    }

    // 5. Asset system (ADR-0025) + Renderer init + main loop.
    FAssetSystem assets(allocator);
    FRenderer    renderer;
    if (!renderer.Init(*device, *window, assets)) {
        ENGINE_LOG_ERROR(LogLaunch, "Renderer init failed");
        backend->DestroyDevice(device);
        ModuleLoader::UnloadModule(vkrhi_mod);
        pa->DestroyWindow(window);
        pa->Shutdown();
        DestroyPlatformApplication(pa);
        log::Shutdown();
        return -7;
    }

    ENGINE_LOG_INFO(LogLaunch, "Entering main loop");
    while (!window->ShouldClose()) {
        pa->PumpEvents();
        renderer.RenderFrame();
    }
    ENGINE_LOG_INFO(LogLaunch, "Main loop exited; shutting down");

    // 6. Teardown (reverse order, WaitIdle first).
    device->WaitIdle();
    renderer.Shutdown();
    backend->DestroyDevice(device);
    ModuleLoader::UnloadModule(vkrhi_mod);
    pa->DestroyWindow(window);
    pa->Shutdown();
    DestroyPlatformApplication(pa);

    ENGINE_LOG_INFO(LogLaunch, "LaunchEngineLoop exited cleanly");
    log::Shutdown();
    return 0;
}

}  // namespace pe
