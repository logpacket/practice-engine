#include <DummyModule/DummyModule.h>

#include <cstdio>
#include <cstring>

namespace pe {

namespace {
constexpr const char kName[] = "Dummy";
}

DummyModule::DummyModule(IEngineAllocator* alloc) noexcept : ModuleBase(alloc) {}

EngineStringView DummyModule::GetName() const noexcept {
    return EngineStringView{kName, std::strlen(kName)};
}

EngineResult DummyModule::StartupModule(IEngineContext* /*ctx*/) {
    // Stage 1: DummyModule writes directly to stdout (no spdlog dependency) so it
    // exercises only the cross-module IModule contract, not the cross-module logger.
    std::printf("[DummyModule] StartupModule\n");
    std::fflush(stdout);
    return EngineResult::Ok();
}

EngineResult DummyModule::ShutdownModule() {
    std::printf("[DummyModule] ShutdownModule\n");
    std::fflush(stdout);
    return EngineResult::Ok();
}

void* DummyModule::QueryInterface(EngineInterfaceId /*id*/) noexcept {
    return nullptr;  // Dummy provides no extra interfaces.
}

}  // namespace pe

// The macro's second arg must match the library name on disk (libDummyModule.so),
// since the loader resolves CreateModule_<lib>/DestroyModule_<lib> by that name.
// GetName() above returns "Dummy" - the *logical* module identity, independent
// from the library file name.
DECLARE_ENGINE_MODULE(pe::DummyModule, DummyModule)
