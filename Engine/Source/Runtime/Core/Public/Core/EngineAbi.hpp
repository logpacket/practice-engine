// EngineAbi.hpp - ABI value types that cross module boundaries.
//
// Stage 1 policy: the strict no-STL / no-exception / no-RTTI #error guards are NOT enforced yet;
// they will be enabled in Stage 3 when gameplay modules arrive (see Architecture.md §2.1).
// In Stage 1 every module is built with the same compiler and CRT, so the ABI risk surface is ~0.
//
// The types here are still designed POD-style so they remain safe to cross module boundaries
// once the strict guards are flipped on.

#pragma once

#include <cstdint>

namespace pe {

// Bumped whenever this header changes in a way that affects binary layout or contract.
// Every module factory (CreateModule_*) checks this against its compile-time value
// and refuses to load on mismatch.
inline constexpr std::uint32_t ENGINE_ABI_VERSION = 1;

// --- Result -----------------------------------------------------------------
// A POD result code. `code == 0` is success. Negative codes are errors.
// `facility` identifies the subsystem that produced the code (Module loader, IO, RHI, ...).
struct EngineResult {
    std::int32_t  code;
    std::uint32_t facility;

    constexpr bool ok() const noexcept { return code == 0; }

    static constexpr EngineResult Ok() noexcept { return {0, 0}; }
    static constexpr EngineResult Fail(std::int32_t c, std::uint32_t f = 0) noexcept { return {c, f}; }
};

// --- Value types -----------------------------------------------------------
// Engine-owned POD equivalents of std::string_view / std::span. Safe to pass
// across module boundaries because they hold no allocator state.
struct EngineStringView {
    const char*   data;
    std::uint64_t size;
};

template <typename T>
struct EngineSpan {
    T*            data;
    std::uint64_t size;
};

// --- Allocator -------------------------------------------------------------
// All cross-module allocation goes through this interface. The host (executable / Launch)
// owns the concrete allocator and passes it to module factories.
class IEngineAllocator {
public:
    virtual void* Allocate(std::uint64_t size, std::uint32_t align) = 0;
    virtual void  Free(void* ptr, std::uint64_t size, std::uint32_t align)  = 0;

protected:
    ~IEngineAllocator() = default;  // not deleted via this pointer
};

// --- Graphics backend identifier --------------------------------------------
// Identifies which RHI backend a caller is interoperating with. Lives in Core
// (not in RHI) so the PAL can take it as a parameter without dragging in the
// RHI dependency.
enum class EGraphicsBackend : std::uint16_t {
    Unknown = 0,
    Vulkan  = 1,
    D3D12   = 2,  // Stage 5
    Metal   = 3,  // Stage 6
};

// --- Interface identity -----------------------------------------------------
// 64-bit FNV-1a hash of a qualified interface name. Used as the lookup key in
// IModule::QueryInterface. Stable across builds (does not depend on RTTI).
using EngineInterfaceId = std::uint64_t;

constexpr EngineInterfaceId MakeInterfaceId(const char* qualified_name) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    while (*qualified_name != '\0') {
        h ^= static_cast<std::uint8_t>(*qualified_name++);
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace pe
