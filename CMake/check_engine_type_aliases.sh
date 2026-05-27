#!/usr/bin/env bash
# CMake/check_engine_type_aliases.sh
#
# G6 gate - bans raw `stdint`-style integer types from engine code outside the
# documented backend-bridge files. Forces the use of the engine aliases
# (`uint32`, `int64`, `float32`, ...) defined in `Core/Public/Core/Types.h`.
#
# Why: a framework-style codebase wants one canonical spelling for its domain
# types. Mixed `uint32_t` / `uint32` invites inconsistency at every interface
# and obscures which side of an API boundary we are on. The PCH plus Core/Types.h
# already make the aliases available everywhere in `namespace pe`.
#
# Banned patterns (regex):
#     \b(std::)?(u?int(8|16|32|64)_t|size_t|ptrdiff_t)\b
#
# Allowlisted files (raw stdint types are part of the API contract these files
# implement; the engine aliases would be wrong here):
#   - Core/Public/Core/Types.h            : defines the aliases via std::uintN_t
#   - Core/Public/Core/EngineAbi.hpp      : the C ABI surface uses std:: types
#   - Core/Public/Core/MallocAllocator.h  : overrides IEngineAllocator's signature
#   - Core/Private/MallocAllocator.cpp    : same
#   - Core/Private/Paths.cpp              : Win32/POSIX OS calls take stdint types
#   - Core/Private/ModuleLoader.cpp       : same
#   - VulkanRHI/Private/Vulkan*.{h,cpp}   : Vulkan API takes uint32_t / VkBool32 / etc
#   - ApplicationCore/Private/GLFW/*.{h,cpp} : GLFW signatures use uint32_t
#   - any *PCH.h                          : PCH files include stdlib explicitly
#   - any Generated/                      : CMake-generated export headers
#
# Exit 0 if clean, 1 if violations.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Patterns to ban. Word-boundary on both sides to avoid catching uint32_total etc.
PATTERN='\b(std::)?(u?int(8|16|32|64)_t|size_t|ptrdiff_t)\b'

# Files / patterns that legitimately use raw stdint types.
EXCLUDE_REGEX='/Generated/|/Types\.h$|/EngineAbi\.hpp$|/MallocAllocator\.(h|cpp)$|/Paths\.cpp$|/ModuleLoader\.cpp$|/Vulkan[A-Za-z]*\.(h|cpp)$|/GLFW[A-Za-z]*\.(h|cpp)$|PCH\.h$'

# Directories to scan (engine source + samples).
SCAN_DIRS=(
    "${REPO_ROOT}/Engine/Source/Runtime"
    "${REPO_ROOT}/Samples"
)

VIOLATIONS=()
for dir in "${SCAN_DIRS[@]}"; do
    if [ ! -d "${dir}" ]; then
        continue
    fi
    while IFS= read -r -d '' file; do
        if [[ "${file}" =~ ${EXCLUDE_REGEX} ]]; then
            continue
        fi
        if grep -qE "${PATTERN}" "${file}"; then
            while IFS= read -r match; do
                VIOLATIONS+=("${file}:${match}")
            done < <(grep -nE "${PATTERN}" "${file}")
        fi
    done < <(find "${dir}" \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \
                            -o -name '*.h' -o -name '*.hpp' \) -print0)
done

if [ ${#VIOLATIONS[@]} -gt 0 ]; then
    echo "G6-FAIL: raw stdint/std::int*_t types found in engine code. Use the"
    echo "         aliases from <Core/Types.h> (uint32, int64, float32, ...) instead."
    echo ""
    for v in "${VIOLATIONS[@]}"; do
        echo "  ${v}"
    done
    echo ""
    echo "If a violation is intentional (backend/API bridge), add the file to"
    echo "EXCLUDE_REGEX in CMake/check_engine_type_aliases.sh with a comment."
    exit 1
fi

echo "G6-OK: engine code uses Types.h aliases consistently"
exit 0
