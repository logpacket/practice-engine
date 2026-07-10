#include <Asset/AssetSystem.h>

#include <Core/Assert.h>
#include <Core/Logging.h>

#include <fstream>

namespace pe {

DECLARE_LOG_CATEGORY(LogAsset)

namespace {

AssetId HashPath(const char* path) noexcept {
    // FNV-1a, same construction as MakeInterfaceId (EngineAbi.hpp).
    uint64 h = 0xcbf29ce484222325ULL;
    while (*path != '\0') {
        h ^= static_cast<uint8>(*path++);
        h *= 0x100000001b3ULL;
    }
    return h;
}

}  // namespace

FAssetSystem::FAssetSystem(IEngineAllocator& allocator) : allocator_(allocator) {}

FAssetSystem::~FAssetSystem() {
    for (auto& [id, asset] : cache_) {
        if (asset.data != nullptr) {
            allocator_.Free(asset.data, asset.size, /*align=*/16);
        }
    }
    cache_.clear();
    registry_.clear();
}

AssetId FAssetSystem::RegisterPath(const char* path) {
    ENGINE_CHECK(path != nullptr);
    const AssetId id = HashPath(path);
    registry_.try_emplace(id, path);
    return id;
}

FAssetBlob FAssetSystem::LoadBytes(const char* path) {
    return LoadBytes(RegisterPath(path));
}

FAssetBlob FAssetSystem::LoadBytes(AssetId id) {
    if (const auto cached = cache_.find(id); cached != cache_.end()) {
        return {cached->second.data, cached->second.size};
    }

    const auto reg = registry_.find(id);
    if (reg == registry_.end()) {
        ENGINE_LOG_ERROR(LogAsset, "LoadBytes: unregistered AssetId {:#x}", id);
        return {};
    }
    const std::string& path = reg->second;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        ENGINE_LOG_ERROR(LogAsset, "Failed to open asset: {}", path);
        return {};
    }
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) {
        ENGINE_LOG_ERROR(LogAsset, "Empty asset: {}", path);
        return {};
    }

    auto* data = static_cast<uint8*>(
        allocator_.Allocate(static_cast<uint64>(size), /*align=*/16));
    if (data == nullptr) {
        ENGINE_LOG_ERROR(LogAsset, "Allocation failed for asset: {} ({} bytes)", path, size);
        return {};
    }
    if (!f.read(reinterpret_cast<char*>(data), size)) {
        ENGINE_LOG_ERROR(LogAsset, "Failed to read asset: {}", path);
        allocator_.Free(data, static_cast<uint64>(size), /*align=*/16);
        return {};
    }

    const FLoadedAsset asset{data, static_cast<uint64>(size)};
    cache_.emplace(id, asset);
    ENGINE_LOG_INFO(LogAsset, "Loaded asset {} ({} bytes)", path, size);
    return {asset.data, asset.size};
}

}  // namespace pe
