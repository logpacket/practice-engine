// Module.h - the IModule interface, factory function contract, and DECLARE_ENGINE_MODULE macro.
//
// Every dynamic-loaded module .so/.dll exposes exactly two C symbols:
//   IModule* CreateModule_<Name>(IEngineAllocator* alloc, uint32 abi);
//   void     DestroyModule_<Name>(IModule* m);
//
// Stage 1 does NOT include: ELoadingPhase, ModuleDependency, dependency graph,
// hot-reload PreReload/PostReload hooks. All deferred to Stage 3 per Architecture.md §2.2.

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>

#include <memory>  // std::destroy_at
#include <new>     // placement new

namespace pe {

class IEngineContext;

class IModule {
public:
    // Stable module name. Lifetime tied to the module - safe to reference until UnloadModule.
    virtual EngineStringView GetName() const noexcept = 0;

    // Called once after construction. `ctx` may be nullptr for minimal test drivers.
    virtual EngineResult StartupModule(IEngineContext* ctx) = 0;

    // Called once before destruction. Must release every external reference the module holds
    // (delegates, system fn pointers, threads, RHI handles).
    virtual EngineResult ShutdownModule() = 0;

    // Returns a concrete interface implementation, or nullptr if the module does not provide it.
    // Caller is responsible for the static_cast based on the requested EngineInterfaceId.
    virtual void* QueryInterface(EngineInterfaceId id) noexcept = 0;

protected:
    ~IModule() = default;  // never deleted via this pointer; DestroyModule_<Name> handles teardown
};

extern "C" {

    // C-style factory signatures resolved by the loader via dlsym / GetProcAddress.
    // The exact symbols are named CreateModule_<Name> and DestroyModule_<Name>;
    // DECLARE_ENGINE_MODULE emits them.
    using PFN_CreateModule  = IModule* (*)(IEngineAllocator* alloc, uint32 abi);
    using PFN_DestroyModule = void     (*)(IModule* m);

}  // extern "C"

}  // namespace pe

// --- Export attribute ------------------------------------------------------
// Used by DECLARE_ENGINE_MODULE for the two C symbols. Independent from CORE_API
// because these are always exported (never imported by name; the loader resolves
// them via the OS dynamic loader API).
#if defined(_WIN32) || defined(_WIN64)
    #define ENGINE_DLL_EXPORT __declspec(dllexport)
#else
    #define ENGINE_DLL_EXPORT __attribute__((visibility("default")))
#endif

// --- DECLARE_ENGINE_MODULE -------------------------------------------------
// Place once per module .cpp. ModuleClass must:
//   - derive from pe::IModule
//   - provide a constructor accepting IEngineAllocator*
//   - expose IEngineAllocator* GetHostAllocator() noexcept (typically via pe::ModuleBase)
//
// The macro emits the two required C exports. The ABI version check ensures
// host/module compatibility before any virtual call is made.
#define DECLARE_ENGINE_MODULE(ModuleClass, ModuleName)                                                       \
    extern "C" ENGINE_DLL_EXPORT                                                                             \
    ::pe::IModule* CreateModule_##ModuleName(::pe::IEngineAllocator* alloc, ::pe::uint32 abi) {              \
        if (abi != ::pe::ENGINE_ABI_VERSION) return nullptr;                                                 \
        if (alloc == nullptr) return nullptr;                                                                \
        void* mem = alloc->Allocate(sizeof(ModuleClass), alignof(ModuleClass));                              \
        if (mem == nullptr) return nullptr;                                                                  \
        return new (mem) ModuleClass(alloc);                                                                 \
    }                                                                                                        \
                                                                                                             \
    extern "C" ENGINE_DLL_EXPORT                                                                             \
    void DestroyModule_##ModuleName(::pe::IModule* mod) {                                                    \
        if (mod == nullptr) return;                                                                          \
        auto* self = static_cast<ModuleClass*>(mod);                                                         \
        ::pe::IEngineAllocator* alloc = self->GetHostAllocator();                                            \
        std::destroy_at(self);                                                                               \
        alloc->Free(self, sizeof(ModuleClass), alignof(ModuleClass));                                        \
    }

namespace pe {

// Helper base class that stores the host allocator. Most modules can derive from this.
class ModuleBase : public IModule {
public:
    explicit ModuleBase(IEngineAllocator* host_alloc) noexcept : host_alloc_(host_alloc) {}

    IEngineAllocator* GetHostAllocator() noexcept { return host_alloc_; }

protected:
    // Match IModule's protected non-virtual destructor: lifetime is managed by
    // DECLARE_ENGINE_MODULE's DestroyModule_<Name> function (static_cast to the
    // concrete leaf type before destruction). Never deleted via this pointer.
    ~ModuleBase() = default;

private:
    IEngineAllocator* host_alloc_;
};

}  // namespace pe
