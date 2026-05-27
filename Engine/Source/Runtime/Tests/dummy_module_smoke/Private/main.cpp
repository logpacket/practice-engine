// dummy_module_smoke - G5 module-loader regression test.
//
// Boots Core, loads DummyModule via runtime dlopen, asserts GetName() == "Dummy",
// then unloads. Exits 0 on success, non-zero on any check failure.

#include <Core/Assert.h>
#include <Core/IEngineContext.h>
#include <Core/Logging.h>
#include <Core/MallocAllocator.h>
#include <Core/Module.h>
#include <Core/ModuleLoader.h>
#include <Core/Paths.h>

#include <cstring>
#include <string_view>

namespace {

DECLARE_LOG_CATEGORY(LogSmoke)

class FMinimalEngineContext final : public pe::IEngineContext {
public:
    explicit FMinimalEngineContext(pe::IEngineAllocator& alloc) noexcept : alloc_(alloc) {}
    pe::IEngineAllocator& GetAllocator() noexcept override { return alloc_; }
    const pe::FPaths&     GetPaths() const noexcept override { return paths_; }

private:
    pe::IEngineAllocator& alloc_;
    pe::FPaths            paths_;  // FPaths is a stateless static facade in Stage 1.
};

}  // namespace

int main() {
    const std::string log_path = (pe::FPaths::SavedDir() / "Logs" / "smoke.log").string();
    pe::log::Init(log_path.c_str());

    ENGINE_LOG_INFO(LogSmoke, "dummy_module_smoke starting");
    ENGINE_LOG_INFO(LogSmoke, "ExecutableDir: {}", pe::FPaths::ExecutableDir().string());
    ENGINE_LOG_INFO(LogSmoke, "EngineDir:     {}", pe::FPaths::EngineDir().string());

    pe::MallocAllocator       allocator;
    FMinimalEngineContext     ctx(allocator);

    pe::IModule* dummy = pe::ModuleLoader::LoadModule("DummyModule", allocator, &ctx);
    if (dummy == nullptr) {
        ENGINE_LOG_ERROR(LogSmoke, "LoadModule('DummyModule') failed");
        pe::log::Shutdown();
        return 1;
    }

    const pe::EngineStringView name = dummy->GetName();
    const std::string_view name_sv(name.data, static_cast<pe::usize>(name.size));
    ENGINE_LOG_INFO(LogSmoke, "Loaded module GetName() = '{}'", name_sv);

    if (name_sv != "Dummy") {
        ENGINE_LOG_ERROR(LogSmoke, "Expected name 'Dummy', got '{}'", name_sv);
        pe::ModuleLoader::UnloadModule(dummy);
        pe::log::Shutdown();
        return 2;
    }

    pe::ModuleLoader::UnloadModule(dummy);

    ENGINE_LOG_INFO(LogSmoke, "dummy_module_smoke OK");
    pe::log::Shutdown();
    return 0;
}
