// DummyModule.h - G5 baseline asset declaration.
//
// Minimal IModule implementation used solely to exercise the ModuleLoader's
// dlopen / factory / startup / shutdown / dlclose cycle on every build.
// See Architecture.md §6.b for the G5 gate definition.

#pragma once

#include <Core/Module.h>

namespace pe {

class DummyModule final : public ModuleBase {
public:
    explicit DummyModule(IEngineAllocator* alloc) noexcept;

    EngineStringView GetName() const noexcept override;
    EngineResult     StartupModule(IEngineContext* ctx) override;
    EngineResult     ShutdownModule() override;
    void*            QueryInterface(EngineInterfaceId id) noexcept override;
};

}  // namespace pe
