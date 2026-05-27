// VulkanRHIModule.cpp - module entry point for VulkanRHI.
//
// Implements IModule (lifecycle + QueryInterface) and IRHIBackendModule
// (CreateDevice/DestroyDevice). Exports the C factory pair via DECLARE_ENGINE_MODULE.

#include "VulkanCommon.h"
#include "VulkanDevice.h"

#include <cstring>

namespace pe::vk {

namespace {
constexpr const char kModuleName[] = "VulkanRHI";
}

class FVulkanRHIModule final : public ModuleBase, public IRHIBackendModule {
public:
    explicit FVulkanRHIModule(IEngineAllocator* alloc) noexcept : ModuleBase(alloc) {}

    // --- IModule ---------------------------------------------------------
    EngineStringView GetName() const noexcept override {
        return EngineStringView{kModuleName, std::strlen(kModuleName)};
    }

    EngineResult StartupModule(IEngineContext* /*ctx*/) override {
        ENGINE_LOG_INFO(LogVulkanRHI, "VulkanRHI StartupModule");
        return EngineResult::Ok();
    }

    EngineResult ShutdownModule() override {
        ENGINE_LOG_INFO(LogVulkanRHI, "VulkanRHI ShutdownModule");
        return EngineResult::Ok();
    }

    void* QueryInterface(EngineInterfaceId id) noexcept override {
        if (id == IRHIBackendModule::kInterfaceId) {
            return static_cast<IRHIBackendModule*>(this);
        }
        return nullptr;
    }

    // --- IRHIBackendModule ----------------------------------------------
    EngineResult CreateDevice(const RHIDeviceCreateDesc& desc, IRHIDevice** out_device) override {
        if (out_device == nullptr) {
            return EngineResult::Fail(-1);
        }

        IEngineAllocator* alloc = GetHostAllocator();
        void* mem = alloc->Allocate(sizeof(FVulkanDevice), alignof(FVulkanDevice));
        if (mem == nullptr) {
            return EngineResult::Fail(-2);
        }

        bool failed = true;
        auto* device = new (mem) FVulkanDevice(desc, failed);
        if (failed) {
            std::destroy_at(device);
            alloc->Free(mem, sizeof(FVulkanDevice), alignof(FVulkanDevice));
            return EngineResult::Fail(-3);
        }

        *out_device = device;
        return EngineResult::Ok();
    }

    void DestroyDevice(IRHIDevice* device) override {
        if (device == nullptr) {
            return;
        }
        auto* self = static_cast<FVulkanDevice*>(device);
        IEngineAllocator* alloc = GetHostAllocator();
        std::destroy_at(self);
        alloc->Free(self, sizeof(FVulkanDevice), alignof(FVulkanDevice));
    }
};

}  // namespace pe::vk

DECLARE_ENGINE_MODULE(pe::vk::FVulkanRHIModule, VulkanRHI)
