#include "GLFWBackend.h"

namespace pe::glfw_backend {

EKey TranslateGlfwKey(int glfw_key) noexcept {
    switch (glfw_key) {
        case GLFW_KEY_ESCAPE: return EKey::Escape;
        default:              return EKey::Unknown;
    }
}

FGLFWWindow::FGLFWWindow(GLFWwindow* handle, void* native_window, void* native_display) noexcept
    : handle_(handle), native_window_(native_window), native_display_(native_display) {}

FGLFWWindow::~FGLFWWindow() {
    if (handle_ != nullptr) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
    }
}

bool FGLFWWindow::ShouldClose() const {
    return handle_ == nullptr ? true : (glfwWindowShouldClose(handle_) != 0);
}

void* FGLFWWindow::GetNativeWindowHandle() const {
    return native_window_;
}

void* FGLFWWindow::GetNativeDisplayHandle() const {
    return native_display_;
}

uint32 FGLFWWindow::GetWidth() const {
    int w = 0, h = 0;
    if (handle_ != nullptr) { glfwGetWindowSize(handle_, &w, &h); }
    return static_cast<uint32>(w);
}

uint32 FGLFWWindow::GetHeight() const {
    int w = 0, h = 0;
    if (handle_ != nullptr) { glfwGetWindowSize(handle_, &w, &h); }
    return static_cast<uint32>(h);
}

bool FGLFWWindow::ConsumeResized() {
    const bool was = resized_;
    resized_ = false;
    return was;
}

}  // namespace pe::glfw_backend
