// asset_smoke - Stage 2 §6.g / gate G13 verification.
//
// Loads a known on-disk file (a built shader binary) synchronously through
// FAssetSystem and verifies: the byte count matches the file size, the bytes
// match the file contents, the cache returns the identical blob on a second
// load, and an unknown path fails cleanly.

#include <Asset/AssetSystem.h>

#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/MallocAllocator.h>
#include <Core/Paths.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

DECLARE_LOG_CATEGORY(LogAssetSmoke)

}  // namespace

int main() {
    const std::string log_path = (pe::FPaths::SavedDir() / "Logs" / "asset_smoke.log").string();
    pe::log::Init(log_path.c_str());
    ENGINE_LOG_INFO(LogAssetSmoke, "asset_smoke starting");

    pe::MallocAllocator allocator;
    pe::FAssetSystem    assets(allocator);

    const std::filesystem::path shader_path = pe::FPaths::ShadersDir() / "Triangle.vert.spv";
    const std::string path_str = shader_path.string();

    // Ground truth read for comparison (the test may do direct I/O; the
    // Renderer may not - that is the G13 point).
    std::ifstream f(shader_path, std::ios::binary | std::ios::ate);
    ENGINE_VERIFY(f.good());
    const std::streamsize disk_size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> disk_bytes(static_cast<pe::usize>(disk_size));
    ENGINE_VERIFY(f.read(disk_bytes.data(), disk_size).good());

    // 1. Load through Asset: size + bytes match the file.
    const pe::FAssetBlob blob = assets.LoadBytes(path_str.c_str());
    ENGINE_VERIFY(blob.valid());
    ENGINE_VERIFY(blob.size == static_cast<pe::uint64>(disk_size));
    ENGINE_VERIFY(std::memcmp(blob.data, disk_bytes.data(), static_cast<pe::usize>(disk_size)) == 0);

    // 2. Cache: the same path returns the identical blob (same pointer).
    const pe::FAssetBlob again = assets.LoadBytes(path_str.c_str());
    ENGINE_VERIFY(again.data == blob.data && again.size == blob.size);

    // 3. Registry: id-based load resolves to the same cached blob.
    const pe::AssetId id = assets.RegisterPath(path_str.c_str());
    const pe::FAssetBlob by_id = assets.LoadBytes(id);
    ENGINE_VERIFY(by_id.data == blob.data);

    // 4. Unknown path fails cleanly (no crash, invalid blob).
    const pe::FAssetBlob missing = assets.LoadBytes("does/not/exist.bin");
    ENGINE_VERIFY(!missing.valid());

    ENGINE_LOG_INFO(LogAssetSmoke, "asset_smoke OK ({} bytes verified)", blob.size);
    pe::log::Shutdown();
    std::printf("asset_smoke OK\n");
    return 0;
}
