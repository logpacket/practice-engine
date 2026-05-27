// Paths.h - well-known engine directory resolution.
//
// All paths are resolved relative to the executable location (Binaries/<Platform>/<Config>/...).
// First call resolves and caches; subsequent calls are O(1).

#pragma once

#include <Core/CoreAPI.h>

#include <filesystem>

namespace pe {

class CORE_API FPaths {
public:
    // Directory containing the running executable (Binaries/<Platform>/<Config>/).
    static const std::filesystem::path& ExecutableDir();

    // Project root (parent of Binaries/<Platform>/, used to find Engine/, Saved/, ...).
    static const std::filesystem::path& EngineDir();

    // Binaries/<Platform>/<Config>/ - same as ExecutableDir for Stage 1.
    static const std::filesystem::path& BinariesDir();

    // Saved/ - runtime-generated state (logs, crashdumps, screenshots).
    static const std::filesystem::path& SavedDir();

    // Engine/Shaders/ - compiled shader binaries at Binaries/.../Shaders/ for Stage 1.
    static const std::filesystem::path& ShadersDir();
};

}  // namespace pe
