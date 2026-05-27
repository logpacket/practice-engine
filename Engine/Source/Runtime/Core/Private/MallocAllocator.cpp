#include <Core/MallocAllocator.h>

#include <cstdlib>

#if defined(_WIN32) || defined(_WIN64)
    #include <malloc.h>
#endif

namespace pe {

void* MallocAllocator::Allocate(uint64_t size, uint32_t align) {
    if (size == 0) {
        return nullptr;
    }
#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc(static_cast<size_t>(size), static_cast<size_t>(align));
#else
    // C11 aligned_alloc requires size to be a multiple of align. Round up.
    const size_t rounded = (static_cast<size_t>(size) + align - 1) & ~(static_cast<size_t>(align) - 1);
    return std::aligned_alloc(static_cast<size_t>(align), rounded);
#endif
}

void MallocAllocator::Free(void* ptr, uint64_t /*size*/, uint32_t /*align*/) {
    if (ptr == nullptr) {
        return;
    }
#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

}  // namespace pe
