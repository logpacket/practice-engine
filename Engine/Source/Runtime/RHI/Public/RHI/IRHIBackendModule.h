// IRHIBackendModule.h - the IModule sub-interface exposed by every RHI backend.
//
// Launch loads "VulkanRHI" (or future "D3D12RHI", "MetalRHI") via ModuleLoader,
// queries this interface, and calls CreateDevice. The IModule contract for
// QueryInterface uses kInterfaceId as the lookup key.

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Module.h>
#include <RHI/RHITypes.h>

namespace pe {

class IRHIDevice;

class IRHIBackendModule {
public:
    // Stable identity used in IModule::QueryInterface. Compile-time FNV-1a hash
    // of the qualified name - never changes once shipped.
    static constexpr EngineInterfaceId kInterfaceId = MakeInterfaceId("pe::IRHIBackendModule");

    // Returns nullptr device on failure (out_device left untouched in that case).
    // Result code carries the diagnostic; the backend should also log details.
    virtual EngineResult CreateDevice(const RHIDeviceCreateDesc& desc, IRHIDevice** out_device) = 0;

    // Destroys a device produced by CreateDevice. Idempotent on nullptr.
    virtual void DestroyDevice(IRHIDevice* device) = 0;

protected:
    ~IRHIBackendModule() = default;
};

}  // namespace pe
