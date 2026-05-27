// IPlatformApplication.h - PAL entry point.
//
// Architecture.md §3.8. The single interface every PAL backend (GLFW today;
// Win32/Wayland/Cocoa/SDL3 later) implements. Discovered via
// IModule::QueryInterface(IPlatformApplication::kInterfaceId) after Launch
// dlopens the ApplicationCore module.

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Module.h>
#include <ApplicationCore/IWindow.h>

namespace pe {

class IPlatformApplication {
public:
    static constexpr EngineInterfaceId kInterfaceId =
        MakeInterfaceId("pe::IPlatformApplication");

    // App-level setup/teardown. Caller (Launch) invokes Initialize once after
    // module load, and Shutdown once before unload.
    virtual EngineResult Initialize() = 0;
    virtual void         Shutdown()   = 0;

    // Window factory. Stage 1 creates one window. The returned IWindow is owned
    // by IPlatformApplication; release via DestroyWindow before Shutdown.
    virtual EngineResult CreateWindow(const FWindowDesc& desc, IWindow** out_window) = 0;
    virtual void         DestroyWindow(IWindow* window) = 0;

    // Drain OS event queue. Called once per frame from the engine loop.
    virtual void         PumpEvents() = 0;

    // Instance-extension query for the given graphics backend.
    //   Vulkan -> the OS-required VkInstance extensions (e.g. VK_KHR_surface +
    //             VK_KHR_wayland_surface). The PAL backend implementation knows
    //             which surface extension matches the active platform.
    //   D3D12  -> empty span (D3D12 does not use instance extensions).
    //   Metal  -> empty span.
    // Returns empty span if the PAL backend cannot interoperate with `backend`
    // on this platform. Returned span is owned by the PAL backend - valid until
    // Shutdown.
    virtual EngineSpan<const char* const>
        GetRequiredGraphicsInstanceExtensions(EGraphicsBackend backend) const = 0;

    // Creates a backend-native surface for the given window.
    //
    // backend                   : which RHI backend the caller is using
    // backend_instance_opaque   : per-backend root handle, e.g.
    //                               Vulkan -> VkInstance cast to void*
    //                               D3D12  -> nullptr (uses HWND directly)
    //                               Metal  -> nullptr
    // window                    : the IWindow returned from CreateWindow
    // out_surface_opaque        : on success, *out points to the per-backend
    //                             surface handle cast to void*, e.g.
    //                               Vulkan -> VkSurfaceKHR
    //                               D3D12  -> HWND (re-exposed for IDXGIFactory)
    //                               Metal  -> CAMetalLayer*
    //
    // The created surface is owned by the caller; destroy with the backend's
    // matching call (vkDestroySurfaceKHR for Vulkan etc.) before tearing down
    // the backend instance. Returns failure if the PAL backend cannot create
    // a surface for the requested backend on this platform.
    virtual EngineResult CreateGraphicsSurface(EGraphicsBackend backend,
                                               void* backend_instance_opaque,
                                               IWindow* window,
                                               void** out_surface_opaque) = 0;

protected:
    ~IPlatformApplication() = default;
};

}  // namespace pe
