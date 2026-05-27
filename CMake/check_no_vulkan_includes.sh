#!/usr/bin/env bash
# CMake/check_no_vulkan_includes.sh
#
# G3 gate - verifies that no Vulkan header is included from Renderer module sources.
# grep-based (Stage 1). Can be upgraded to clang -H precision check in Stage 2+.
#
# Usage:  ./CMake/check_no_vulkan_includes.sh
# Exit 0 if clean, 1 if violation.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RENDERER_DIR="${REPO_ROOT}/Engine/Source/Runtime/Renderer"

if [ ! -d "${RENDERER_DIR}" ]; then
    echo "G3: Renderer directory does not yet exist (${RENDERER_DIR}) - SKIP (pre-build stage)"
    exit 0
fi

# Vulkan-related header patterns
PATTERNS=(
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*vulkan'
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*volk'
    '#[[:space:]]*include[[:space:]]*[<"][^>"]*vk_'
)

VIOLATIONS=()
while IFS= read -r -d '' file; do
    for pattern in "${PATTERNS[@]}"; do
        if grep -qE "${pattern}" "${file}"; then
            VIOLATIONS+=("${file}: $(grep -nE "${pattern}" "${file}" | head -1)")
        fi
    done
done < <(find "${RENDERER_DIR}" \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)

if [ ${#VIOLATIONS[@]} -gt 0 ]; then
    echo "G3-FAIL: Vulkan header reference found in Renderer sources"
    for v in "${VIOLATIONS[@]}"; do
        echo "  ${v}"
    done
    exit 1
fi

echo "G3-OK: no Vulkan header references in Renderer sources"
exit 0
