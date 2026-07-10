#!/usr/bin/env bash
# CMake/check_no_vulkan_includes.sh
#
# G3 gate - verifies that no Vulkan header is included from the modules that
# must stay backend-agnostic: Renderer, RenderGraph, Asset (Stage 2 extends
# the Stage 1 Renderer-only coverage). grep-based; can be upgraded to a
# clang -H precision check later.
#
# Usage:  ./CMake/check_no_vulkan_includes.sh
# Exit 0 if clean, 1 if violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHECK_DIRS=(
    "${REPO_ROOT}/Engine/Source/Runtime/Renderer"
    "${REPO_ROOT}/Engine/Source/Runtime/RenderGraph"
    "${REPO_ROOT}/Engine/Source/Runtime/Asset"
)

# Vulkan-related header patterns
PATTERNS=(
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*vulkan'
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*volk'
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*vk_'
)

VIOLATIONS=()
for dir in "${CHECK_DIRS[@]}"; do
    [ -d "${dir}" ] || continue  # module not created yet - skip
    while IFS= read -r -d '' file; do
        for pattern in "${PATTERNS[@]}"; do
            if grep -qE "${pattern}" "${file}"; then
                VIOLATIONS+=("${file}: $(grep -nE "${pattern}" "${file}" | head -1)")
            fi
        done
    done < <(find "${dir}" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)
done

if [ ${#VIOLATIONS[@]} -gt 0 ]; then
    echo "G3-FAIL: Vulkan header reference found in backend-agnostic module sources"
    for v in "${VIOLATIONS[@]}"; do
        echo "  ${v}"
    done
    exit 1
fi

echo "G3-OK: no Vulkan header references in Renderer/RenderGraph/Asset sources"
exit 0
