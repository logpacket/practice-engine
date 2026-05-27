#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Module.h>
#include <Core/ModuleLoader.h>
#include <Core/Paths.h>

#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace pe {

DECLARE_LOG_CATEGORY(LogModuleLoader)

namespace {

struct LoadedModule {
    IModule*          instance = nullptr;
    PFN_DestroyModule destroy_fn = nullptr;
    void*             os_handle = nullptr;
    std::string       symbol_destroy;  // retained for diagnostics
};

std::mutex                                  g_loaded_mutex;
std::unordered_map<IModule*, LoadedModule>  g_loaded;

#if defined(_WIN32) || defined(_WIN64)

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    int required = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                                       nullptr, 0);
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), required);
    return wide;
}

std::string FormatLastError(DWORD code) {
    LPSTR buf = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buf), 0, nullptr);
    std::string out;
    if (len > 0 && buf != nullptr) {
        out.assign(buf, len);
        LocalFree(buf);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
            out.pop_back();
        }
    } else {
        out = "(unknown error)";
    }
    return out;
}

void* OpenLibrary(const std::filesystem::path& full_path) {
    const std::wstring wpath = Utf8ToWide(full_path.string());
    HMODULE handle = LoadLibraryW(wpath.c_str());
    if (handle == nullptr) {
        ENGINE_LOG_ERROR(LogModuleLoader, "LoadLibraryW({}) failed: {}",
                         full_path.string(), FormatLastError(GetLastError()));
    }
    return handle;
}

void* ResolveSymbol(void* handle, const char* name) {
    FARPROC sym = GetProcAddress(static_cast<HMODULE>(handle), name);
    if (sym == nullptr) {
        ENGINE_LOG_ERROR(LogModuleLoader, "GetProcAddress({}) failed: {}",
                         name, FormatLastError(GetLastError()));
    }
    return reinterpret_cast<void*>(sym);
}

void CloseLibrary(void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); }

const char* PlatformPrefix() { return ""; }
const char* PlatformSuffix() { return ".dll"; }

#else  // POSIX

void* OpenLibrary(const std::filesystem::path& full_path) {
    void* handle = dlopen(full_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        ENGINE_LOG_ERROR(LogModuleLoader, "dlopen({}) failed: {}", full_path.string(), dlerror());
    }
    return handle;
}

void* ResolveSymbol(void* handle, const char* name) {
    dlerror();  // clear residual
    void* sym = dlsym(handle, name);
    const char* err = dlerror();
    if (err != nullptr) {
        ENGINE_LOG_ERROR(LogModuleLoader, "dlsym({}) failed: {}", name, err);
        return nullptr;
    }
    return sym;
}

void CloseLibrary(void* handle) { dlclose(handle); }

const char* PlatformPrefix() { return "lib"; }

#if defined(__APPLE__)
const char* PlatformSuffix() { return ".dylib"; }
#else
const char* PlatformSuffix() { return ".so"; }
#endif

#endif  // _WIN32

std::filesystem::path BuildModulePath(const char* module_name) {
    std::string filename;
    filename.reserve(64);
    filename.append(PlatformPrefix());
    filename.append(module_name);
    filename.append(PlatformSuffix());
    return FPaths::ExecutableDir() / filename;
}

}  // namespace

IModule* ModuleLoader::LoadModule(const char* module_name, IEngineAllocator& alloc, IEngineContext* ctx) {
    ENGINE_VERIFY(module_name != nullptr && *module_name != '\0');

    const std::filesystem::path full_path = BuildModulePath(module_name);
    ENGINE_LOG_INFO(LogModuleLoader, "Loading module '{}' from {}", module_name, full_path.string());

    void* handle = OpenLibrary(full_path);
    if (handle == nullptr) {
        return nullptr;
    }

    const std::string sym_create  = std::string("CreateModule_")  + module_name;
    const std::string sym_destroy = std::string("DestroyModule_") + module_name;

    auto* create_fn  = reinterpret_cast<PFN_CreateModule>(ResolveSymbol(handle, sym_create.c_str()));
    auto* destroy_fn = reinterpret_cast<PFN_DestroyModule>(ResolveSymbol(handle, sym_destroy.c_str()));
    if (create_fn == nullptr || destroy_fn == nullptr) {
        CloseLibrary(handle);
        return nullptr;
    }

    IModule* instance = create_fn(&alloc, ENGINE_ABI_VERSION);
    if (instance == nullptr) {
        ENGINE_LOG_ERROR(LogModuleLoader,
                         "CreateModule_{} returned nullptr (ABI mismatch or allocation failure)",
                         module_name);
        CloseLibrary(handle);
        return nullptr;
    }

    const EngineResult startup = instance->StartupModule(ctx);
    if (!startup.ok()) {
        ENGINE_LOG_ERROR(LogModuleLoader, "StartupModule of '{}' returned code {} (facility {})",
                         module_name, startup.code, startup.facility);
        destroy_fn(instance);
        CloseLibrary(handle);
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(g_loaded_mutex);
        g_loaded[instance] = LoadedModule{instance, destroy_fn, handle, sym_destroy};
    }

    ENGINE_LOG_INFO(LogModuleLoader, "Module '{}' loaded successfully", module_name);
    return instance;
}

void ModuleLoader::UnloadModule(IModule* m) {
    if (m == nullptr) {
        return;
    }

    LoadedModule entry;
    {
        std::lock_guard<std::mutex> lock(g_loaded_mutex);
        auto it = g_loaded.find(m);
        if (it == g_loaded.end()) {
            ENGINE_LOG_WARN(LogModuleLoader,
                            "UnloadModule called with IModule* not produced by LoadModule");
            return;
        }
        entry = it->second;
        g_loaded.erase(it);
    }

    const EngineStringView name = m->GetName();
    ENGINE_LOG_INFO(LogModuleLoader, "Unloading module '{}'",
                    std::string_view(name.data, static_cast<size_t>(name.size)));

    const EngineResult shutdown = m->ShutdownModule();
    if (!shutdown.ok()) {
        ENGINE_LOG_WARN(LogModuleLoader, "ShutdownModule returned code {} (facility {})",
                        shutdown.code, shutdown.facility);
    }

    entry.destroy_fn(m);
    CloseLibrary(entry.os_handle);
}

}  // namespace pe
