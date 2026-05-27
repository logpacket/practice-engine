// app_smoke - PAL backend window-lifecycle verification.
//
// Constructs the active PAL backend via CreatePlatformApplication() (configure-time
// STATIC link, ADR-0018), opens a 1280x720 window, pumps events for ~1s
// (or until ESC / close button), then tears down. Proves the PAL surface is
// wired correctly and reports the right Vulkan instance extensions.

#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Paths.h>
#include <Core/Types.h>

#include <ApplicationCore/IPlatformApplication.h>
#include <ApplicationCore/IWindow.h>
#include <ApplicationCore/PlatformBackend.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace {

DECLARE_LOG_CATEGORY(LogAppSmoke)

constexpr int kAutoExitFrames = 60;  // ~1 second of polling at ~16ms per frame
constexpr int kFrameSleepMs   = 16;

}  // namespace

int main(int argc, char** argv) {
    const std::string log_path = (pe::FPaths::SavedDir() / "Logs" / "app_smoke.log").string();
    pe::log::Init(log_path.c_str());

    bool interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--interactive") == 0) { interactive = true; }
    }

    ENGINE_LOG_INFO(LogAppSmoke, "app_smoke starting (interactive={})", interactive);

    // PAL backend - configure-time STATIC link (ADR-0018). Whichever
    // Platforms/<Backend>/ was selected provides CreatePlatformApplication.
    pe::IPlatformApplication* pa = pe::CreatePlatformApplication();
    if (pa == nullptr) {
        ENGINE_LOG_ERROR(LogAppSmoke, "CreatePlatformApplication failed");
        pe::log::Shutdown();
        return 1;
    }

    if (!pa->Initialize().ok()) {
        ENGINE_LOG_ERROR(LogAppSmoke, "IPlatformApplication::Initialize failed");
        pe::DestroyPlatformApplication(pa);
        pe::log::Shutdown();
        return 2;
    }

    const auto vk_exts = pa->GetRequiredGraphicsInstanceExtensions(pe::EGraphicsBackend::Vulkan);
    ENGINE_LOG_INFO(LogAppSmoke, "Required Vulkan instance extensions ({}):", vk_exts.size);
    for (pe::uint64 i = 0; i < vk_exts.size; ++i) {
        ENGINE_LOG_INFO(LogAppSmoke, "  [{}] {}", i, vk_exts.data[i]);
    }

    pe::FWindowDesc desc{};
    desc.title  = "practice-engine app_smoke";
    desc.width  = 1280;
    desc.height = 720;

    pe::IWindow* window = nullptr;
    if (!pa->CreateWindow(desc, &window).ok() || window == nullptr) {
        ENGINE_LOG_ERROR(LogAppSmoke, "CreateWindow failed");
        pa->Shutdown();
        pe::DestroyPlatformApplication(pa);
        pe::log::Shutdown();
        return 3;
    }

    ENGINE_LOG_INFO(LogAppSmoke, "Window: {}x{}, native_window={}, native_display={}",
                    window->GetWidth(), window->GetHeight(),
                    window->GetNativeWindowHandle(), window->GetNativeDisplayHandle());

    int frame = 0;
    while (!window->ShouldClose()) {
        pa->PumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(kFrameSleepMs));
        ++frame;
        if (!interactive && frame >= kAutoExitFrames) {
            ENGINE_LOG_INFO(LogAppSmoke, "Auto-exit after {} frames", frame);
            break;
        }
    }

    ENGINE_LOG_INFO(LogAppSmoke, "Loop exited (frames={}, should_close={})",
                    frame, window->ShouldClose());

    pa->DestroyWindow(window);
    pa->Shutdown();
    pe::DestroyPlatformApplication(pa);

    ENGINE_LOG_INFO(LogAppSmoke, "app_smoke OK");
    pe::log::Shutdown();
    return 0;
}
