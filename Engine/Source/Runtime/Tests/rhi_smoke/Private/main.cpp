// rhi_smoke - §6.c verification.
//
// dlopens VulkanRHI, queries IRHIBackendModule, creates and destroys an
// IRHIDevice. The host (this exe) MUST NOT link Vulkan, volk, or VulkanRHI
// at link time - all Vulkan code lives behind the runtime module boundary.

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

#include <cstring>
#include <string>

namespace {

DECLARE_LOG_CATEGORY(LogRhiSmoke)

class FMinimalEngineContext final : public pe::IEngineContext {
public:
    explicit FMinimalEngineContext(pe::IEngineAllocator& alloc) noexcept : alloc_(alloc) {}
    pe::IEngineAllocator& GetAllocator() noexcept override { return alloc_; }
    const pe::FPaths&     GetPaths() const noexcept override { return paths_; }

private:
    pe::IEngineAllocator& alloc_;
    pe::FPaths            paths_;
};

}  // namespace

int main() {
    const std::string log_path = (pe::FPaths::SavedDir() / "Logs" / "rhi_smoke.log").string();
    pe::log::Init(log_path.c_str());

    ENGINE_LOG_INFO(LogRhiSmoke, "rhi_smoke starting");

    pe::MallocAllocator     allocator;
    FMinimalEngineContext   ctx(allocator);

    // Step 1: load VulkanRHI as a dynamic module.
    pe::IModule* vkrhi = pe::ModuleLoader::LoadModule("VulkanRHI", allocator, &ctx);
    if (vkrhi == nullptr) {
        ENGINE_LOG_ERROR(LogRhiSmoke, "LoadModule('VulkanRHI') failed");
        pe::log::Shutdown();
        return 1;
    }

    // Step 2: query the backend interface.
    auto* backend = static_cast<pe::IRHIBackendModule*>(
        vkrhi->QueryInterface(pe::IRHIBackendModule::kInterfaceId));
    if (backend == nullptr) {
        ENGINE_LOG_ERROR(LogRhiSmoke, "VulkanRHI does not implement IRHIBackendModule");
        pe::ModuleLoader::UnloadModule(vkrhi);
        pe::log::Shutdown();
        return 2;
    }
    ENGINE_LOG_INFO(LogRhiSmoke, "IRHIBackendModule acquired");

    // Step 3: create a device. §6.c has no surface so we pass zero instance extensions
    // (the backend adds VK_EXT_debug_utils on its own when validation is enabled).
    pe::RHIDeviceCreateDesc desc{};
    desc.required_instance_extensions = {nullptr, 0};
    desc.enable_validation            = true;

    pe::IRHIDevice* device = nullptr;
    const pe::EngineResult create_result = backend->CreateDevice(desc, &device);
    if (!create_result.ok() || device == nullptr) {
        ENGINE_LOG_ERROR(LogRhiSmoke, "CreateDevice failed: code {} facility {}",
                         create_result.code, create_result.facility);
        pe::ModuleLoader::UnloadModule(vkrhi);
        pe::log::Shutdown();
        return 3;
    }
    ENGINE_LOG_INFO(LogRhiSmoke, "IRHIDevice created");

    // Step 4: confirm WaitIdle path works (only fully-implemented method in §6.c).
    const pe::EngineResult wait_result = device->WaitIdle();
    if (!wait_result.ok()) {
        ENGINE_LOG_ERROR(LogRhiSmoke, "WaitIdle failed");
        backend->DestroyDevice(device);
        pe::ModuleLoader::UnloadModule(vkrhi);
        pe::log::Shutdown();
        return 4;
    }
    ENGINE_LOG_INFO(LogRhiSmoke, "WaitIdle OK");

    // Step 5: tear everything down.
    backend->DestroyDevice(device);
    pe::ModuleLoader::UnloadModule(vkrhi);

    ENGINE_LOG_INFO(LogRhiSmoke, "rhi_smoke OK");
    pe::log::Shutdown();
    return 0;
}
