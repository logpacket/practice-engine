// AssetSystem.h - Asset v1: synchronous blob loading with an in-memory cache.
//
// ADR-0025 scope: LoadBytes(path) -> blob, a path -> AssetId registry, and a
// never-evicting cache. Absorbs the Renderer's ad-hoc shader ifstream. No
// cooking, no streaming, no decoders, no hot reload (Stage 3+). Blob memory
// is owned by this system through the engine allocator and stays valid until
// the system is destroyed.
//
// MUST NOT include any Vulkan / volk header (G2/G3 extended to this module).

#pragma once

#include <Core/EngineAbi.hpp>
#include <Core/Types.h>

#include <Asset/AssetAPI.h>

#include <string>
#include <unordered_map>

namespace pe {

// FNV-1a hash of the (verbatim) registered path string.
using AssetId = uint64;

struct FAssetBlob {
    const uint8* data = nullptr;
    uint64       size = 0;
    constexpr bool valid() const noexcept { return data != nullptr && size > 0; }
};

class ASSET_API FAssetSystem {
public:
    explicit FAssetSystem(IEngineAllocator& allocator);
    ~FAssetSystem();

    FAssetSystem(const FAssetSystem&)            = delete;
    FAssetSystem& operator=(const FAssetSystem&) = delete;

    // Registers (or re-derives) the AssetId for a path. Idempotent.
    AssetId RegisterPath(const char* path);

    // Synchronous load. The same path returns the cached blob (same pointer)
    // on every subsequent call. Returns an invalid blob on I/O failure.
    FAssetBlob LoadBytes(const char* path);

    // Cache-or-load by id; the id must have been registered via RegisterPath
    // (or a prior LoadBytes) so the path is known.
    FAssetBlob LoadBytes(AssetId id);

private:
    struct FLoadedAsset {
        uint8* data = nullptr;
        uint64 size = 0;
    };

    IEngineAllocator&                       allocator_;
    std::unordered_map<AssetId, std::string>  registry_;  // id -> path
    std::unordered_map<AssetId, FLoadedAsset> cache_;
};

}  // namespace pe
