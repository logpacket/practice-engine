// IEngineContext.h - context handed to a module at Startup.
//
// Stage 1 exposes only the allocator and paths. The host (Launch) owns the
// concrete implementation (FEngineContext) and its lifetime strictly contains
// every loaded module.
//
// Stage 1 modules may receive nullptr context if loaded by a minimal test driver
// (e.g. dummy_module_smoke); modules must tolerate a nullptr context as long as
// they declare no requirement on its services.

#pragma once

#include <Core/EngineAbi.hpp>

namespace pe {

class FPaths;

class IEngineContext {
public:
    virtual IEngineAllocator& GetAllocator() noexcept = 0;
    virtual const FPaths&     GetPaths() const noexcept = 0;

protected:
    ~IEngineContext() = default;
};

}  // namespace pe
