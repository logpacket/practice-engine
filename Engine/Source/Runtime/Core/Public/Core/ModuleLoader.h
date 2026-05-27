// ModuleLoader.h - cross-platform dlopen wrapper.
//
// Stage 1 contract (Architecture.md §2.3):
//   - LoadModule resolves CreateModule_<Name> / DestroyModule_<Name> from the .so/.dll
//     placed next to the host executable.
//   - No shadow-copy, no hot reload, no dependency graph, no manifest scanning.
//   - Caller supplies the IEngineAllocator passed to the module factory and the
//     IEngineContext threaded into StartupModule.
//
// Module file name resolution (Stage 1 convention):
//   Linux/macOS: lib<Name>.so / lib<Name>.dylib
//   Windows:     <Name>.dll
//
// All looked up from FPaths::ExecutableDir().

#pragma once

#include <Core/CoreAPI.h>
#include <Core/EngineAbi.hpp>

namespace pe {

class IModule;
class IEngineContext;

class CORE_API ModuleLoader {
public:
    // Loads `module_name` and calls its StartupModule(ctx). Returns nullptr on failure.
    // The returned IModule* is owned by the loader's bookkeeping; release via UnloadModule.
    static IModule* LoadModule(const char* module_name, IEngineAllocator& alloc, IEngineContext* ctx);

    // Calls ShutdownModule, then DestroyModule_<Name>, then closes the OS handle.
    // No-op if `m` is nullptr or was not produced by LoadModule.
    static void UnloadModule(IModule* m);
};

}  // namespace pe
