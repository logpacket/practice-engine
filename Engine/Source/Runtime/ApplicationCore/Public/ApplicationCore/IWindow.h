// IWindow.h - per-window state interface.
//
// Stage 1 (§6.d) is fixed-resolution and non-resizable; swapchain recreation is
// Stage 2 (Architecture.md §6.d "M2: window resize disabled"). Native handles are
// opaque void* so consumers (VulkanRHI swapchain creation in §6.e) do not need
// to know about GLFWwindow*.

#pragma once

#include <Core/CoreAPI.h>
#include <Core/Types.h>

namespace pe {

class CORE_API IWindow {
public:
    virtual bool      ShouldClose() const = 0;

    // Opaque OS handles. On Linux/Wayland: wl_surface*. On Linux/X11: xcb_window_t.
    // On Windows: HWND. The §6.e Vulkan surface creation path uses these.
    virtual void*     GetNativeWindowHandle()  const = 0;
    virtual void*     GetNativeDisplayHandle() const = 0;

    virtual uint32    GetWidth()  const = 0;
    virtual uint32    GetHeight() const = 0;

protected:
    ~IWindow() = default;  // owned by IPlatformApplication
};

struct FWindowDesc {
    const char* title  = "practice-engine";
    uint32      width  = 1280;
    uint32      height = 720;
};

}  // namespace pe
