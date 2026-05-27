// GLFWBackend.h - shared internals for the GLFW PAL backend.
//
// GLFW is the Stage 1 backend; external modules (Renderer/Launch/VulkanRHI)
// never reach this header (PRIVATE include path only).

#pragma once

#include <ApplicationCore/IPlatformApplication.h>
#include <ApplicationCore/IWindow.h>
#include <ApplicationCore/PlatformKey.h>

#include <Core/Assert.h>
#include <Core/Logging.h>

#include <GLFW/glfw3.h>

#include <vector>

namespace pe::glfw_backend {

DECLARE_LOG_CATEGORY(LogGLFW)

EKey TranslateGlfwKey(int glfw_key) noexcept;

class FGLFWWindow final : public IWindow {
public:
    FGLFWWindow(GLFWwindow* handle, void* native_window, void* native_display) noexcept;
    ~FGLFWWindow();

    bool   ShouldClose() const override;
    void*  GetNativeWindowHandle()  const override;
    void*  GetNativeDisplayHandle() const override;
    uint32 GetWidth()  const override;
    uint32 GetHeight() const override;

    GLFWwindow* RawHandle() const noexcept { return handle_; }

private:
    GLFWwindow* handle_;
    void*       native_window_;
    void*       native_display_;
};

}  // namespace pe::glfw_backend
