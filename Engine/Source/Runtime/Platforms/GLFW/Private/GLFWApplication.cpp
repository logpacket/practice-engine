// Pull in Vulkan headers via GLFW so glfwCreateWindowSurface is declared.
// This is private to ApplicationCore - the public PAL header (IPlatformApplication.h)
// still uses opaque void* for Vulkan types, preserving backend-neutrality.
#define GLFW_INCLUDE_VULKAN

#include "GLFWBackend.h"

#include <Core/Module.h>

#include <cstring>

// Enable per-platform GLFW native handle access for whichever OS we are building for.
// glfw3native.h pulls in OS headers conditionally based on these macros, so we
// include only the relevant set.
#if defined(_WIN32) || defined(_WIN64)
    #define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(__linux__)
    #define GLFW_EXPOSE_NATIVE_X11
    #define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

namespace pe::glfw_backend {

namespace {

void GlfwErrorCallback(int code, const char* description) {
    ENGINE_LOG_ERROR(LogGLFW, "GLFW error {}: {}", code, description != nullptr ? description : "(null)");
}

// ESC key closes the window (per Architecture.md §6.d "input: ESC quit only").
void GlfwKeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    const EKey k = TranslateGlfwKey(key);
    if (k == EKey::Escape && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

// Framebuffer resize -> dirty flag consumed by the frame loop (ADR-0026).
void GlfwFramebufferSizeCallback(GLFWwindow* window, int /*width*/, int /*height*/) {
    auto* wrapper = static_cast<FGLFWWindow*>(glfwGetWindowUserPointer(window));
    if (wrapper != nullptr) { wrapper->MarkResized(); }
}

class FGLFWApplication final : public IPlatformApplication {
public:
    EngineResult Initialize() override {
        glfwSetErrorCallback(GlfwErrorCallback);
        if (glfwInit() == GLFW_FALSE) {
            ENGINE_LOG_ERROR(LogGLFW, "glfwInit failed");
            return EngineResult::Fail(-1);
        }
        ENGINE_LOG_INFO(LogGLFW, "GLFW {} initialized (platform: {})",
                        glfwGetVersionString(), PlatformName(glfwGetPlatform()));
        if (glfwVulkanSupported() == GLFW_FALSE) {
            ENGINE_LOG_ERROR(LogGLFW, "GLFW reports no Vulkan loader available");
            glfwTerminate();
            return EngineResult::Fail(-2);
        }
        return EngineResult::Ok();
    }

    void Shutdown() override {
        glfwTerminate();
        ENGINE_LOG_INFO(LogGLFW, "GLFW terminated");
    }

    EngineResult CreateWindow(const FWindowDesc& desc, IWindow** out_window) override {
        if (out_window == nullptr) { return EngineResult::Fail(-1); }
        *out_window = nullptr;

        // Vulkan-aware: no GL context. Resizable since Stage 2 (ADR-0026).
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        GLFWwindow* handle = glfwCreateWindow(static_cast<int>(desc.width),
                                              static_cast<int>(desc.height),
                                              desc.title != nullptr ? desc.title : "practice-engine",
                                              nullptr, nullptr);
        if (handle == nullptr) {
            ENGINE_LOG_ERROR(LogGLFW, "glfwCreateWindow failed");
            return EngineResult::Fail(-2);
        }

        glfwSetKeyCallback(handle, GlfwKeyCallback);

        void* native_window  = ExtractNativeWindow(handle);
        void* native_display = ExtractNativeDisplay();

        // Placement-new into our own allocator-managed slot would belong here in a
        // stricter design; Stage 1 uses operator new for simplicity (single window).
        auto* window = new FGLFWWindow(handle, native_window, native_display);
        glfwSetWindowUserPointer(handle, window);
        glfwSetFramebufferSizeCallback(handle, GlfwFramebufferSizeCallback);
        *out_window  = window;
        ENGINE_LOG_INFO(LogGLFW, "Window created: {}x{} '{}'", desc.width, desc.height, desc.title);
        return EngineResult::Ok();
    }

    void DestroyWindow(IWindow* window) override {
        if (window == nullptr) { return; }
        // The IWindow destructor (FGLFWWindow's) calls glfwDestroyWindow.
        delete static_cast<FGLFWWindow*>(window);
    }

    void PumpEvents() override {
        glfwPollEvents();
    }

    EngineSpan<const char* const>
        GetRequiredGraphicsInstanceExtensions(EGraphicsBackend backend) const override {
        // GLFW only knows how to interoperate with Vulkan. Other backends:
        //   D3D12 / Metal need no instance extensions; future native PAL backends
        //   (Win32, Cocoa) will return the same empty span for those cases.
        if (backend != EGraphicsBackend::Vulkan) {
            return EngineSpan<const char* const>{nullptr, 0};
        }
        uint32_t   count = 0;
        const char** raw = glfwGetRequiredInstanceExtensions(&count);
        return EngineSpan<const char* const>{raw, count};
    }

    EngineResult CreateGraphicsSurface(EGraphicsBackend backend,
                                       void* backend_instance_opaque,
                                       IWindow* window,
                                       void** out_surface_opaque) override {
        if (window == nullptr || out_surface_opaque == nullptr) {
            return EngineResult::Fail(-1);
        }
        *out_surface_opaque = nullptr;

        if (backend != EGraphicsBackend::Vulkan) {
            ENGINE_LOG_ERROR(LogGLFW,
                "GLFW backend cannot create a surface for graphics backend {} - "
                "supported: Vulkan only",
                static_cast<int>(backend));
            return EngineResult::Fail(-2);
        }
        if (backend_instance_opaque == nullptr) {
            ENGINE_LOG_ERROR(LogGLFW, "Vulkan surface requested with null VkInstance");
            return EngineResult::Fail(-3);
        }

        auto* glfw_window = static_cast<FGLFWWindow*>(window);
        auto  instance    = static_cast<VkInstance>(backend_instance_opaque);
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        const VkResult r = glfwCreateWindowSurface(instance, glfw_window->RawHandle(),
                                                   nullptr, &surface);
        if (r != VK_SUCCESS) {
            ENGINE_LOG_ERROR(LogGLFW, "glfwCreateWindowSurface failed: VkResult {}",
                             static_cast<int>(r));
            return EngineResult::Fail(static_cast<int32_t>(r), /*facility=*/2);
        }
        *out_surface_opaque = surface;
        return EngineResult::Ok();
    }

private:
    static const char* PlatformName(int platform) noexcept {
        switch (platform) {
            case GLFW_PLATFORM_X11:     return "X11";
            case GLFW_PLATFORM_WAYLAND: return "Wayland";
            case GLFW_PLATFORM_WIN32:   return "Win32";
            case GLFW_PLATFORM_COCOA:   return "Cocoa";
            case GLFW_PLATFORM_NULL:    return "Null";
            default:                    return "Unknown";
        }
    }

    static void* ExtractNativeWindow(GLFWwindow* handle) noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return reinterpret_cast<void*>(glfwGetWin32Window(handle));
#elif defined(__APPLE__)
        return glfwGetCocoaWindow(handle);
#elif defined(__linux__)
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_X11:
                return reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(handle)));
            case GLFW_PLATFORM_WAYLAND:
                return glfwGetWaylandWindow(handle);
            default:
                return nullptr;
        }
#else
        (void)handle;
        return nullptr;
#endif
    }

    static void* ExtractNativeDisplay() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return GetModuleHandleW(nullptr);
#elif defined(__APPLE__)
        return nullptr;  // Cocoa surface creation does not require a display handle.
#elif defined(__linux__)
        switch (glfwGetPlatform()) {
            case GLFW_PLATFORM_X11:     return glfwGetX11Display();
            case GLFW_PLATFORM_WAYLAND: return glfwGetWaylandDisplay();
            default:                    return nullptr;
        }
#else
        return nullptr;
#endif
    }
};

}  // namespace

}  // namespace pe::glfw_backend

// ---- configure-time backend factory ----------------------------------------
// The PAL backend is selected at CMake configure time (ENGINE_APP_BACKEND) and
// STATIC-linked into the host. Only the selected backend's source provides the
// definitions below; the linker resolves directly without dlopen.
// See ADR-0018 (configure-time STATIC link with INTERFACE separation).

#include <ApplicationCore/PlatformBackend.h>

namespace pe {

IPlatformApplication* CreatePlatformApplication() {
    // Heap-allocated; host owns. Configure-time link means no ABI guards needed.
    return new ::pe::glfw_backend::FGLFWApplication();
}

void DestroyPlatformApplication(IPlatformApplication* pa) {
    if (pa == nullptr) { return; }
    delete static_cast<::pe::glfw_backend::FGLFWApplication*>(pa);
}

}  // namespace pe
