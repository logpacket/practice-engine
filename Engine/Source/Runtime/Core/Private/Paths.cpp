#include <Core/Assert.h>
#include <Core/Paths.h>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <climits>
#else
    #include <climits>
    #include <unistd.h>
#endif

namespace pe {

namespace {

std::filesystem::path ResolveExecutableDir() {
#if defined(_WIN32) || defined(_WIN64)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        ENGINE_FATAL("GetModuleFileNameW failed");
    }
    return std::filesystem::path(buf, buf + len).parent_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        ENGINE_FATAL("_NSGetExecutablePath buffer too small");
    }
    return std::filesystem::canonical(std::filesystem::path(buf)).parent_path();
#else
    char buf[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        ENGINE_FATAL("readlink /proc/self/exe failed");
    }
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
#endif
}

}  // namespace

const std::filesystem::path& FPaths::ExecutableDir() {
    static const std::filesystem::path value = ResolveExecutableDir();
    return value;
}

const std::filesystem::path& FPaths::EngineDir() {
    // Stage 1: project root = Binaries/<Platform>/<Config>/ -> ../../../
    static const std::filesystem::path value =
        ExecutableDir().parent_path().parent_path().parent_path();
    return value;
}

const std::filesystem::path& FPaths::BinariesDir() {
    return ExecutableDir();
}

const std::filesystem::path& FPaths::SavedDir() {
    static const std::filesystem::path value = EngineDir() / "Saved";
    return value;
}

const std::filesystem::path& FPaths::ShadersDir() {
    static const std::filesystem::path value = ExecutableDir() / "Shaders";
    return value;
}

}  // namespace pe
