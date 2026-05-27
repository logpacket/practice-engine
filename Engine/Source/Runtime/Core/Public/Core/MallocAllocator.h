// MallocAllocator.h - default IEngineAllocator implementation backed by aligned malloc/free.
//
// Stage 1 use case: the host (Launch / smoke tests) instantiates one of these and
// passes it through IEngineContext to every module. The first concrete sandbox of
// the IEngineAllocator interface (per Architect M3 follow-up review).

#pragma once

#include <Core/CoreAPI.h>
#include <Core/EngineAbi.hpp>

namespace pe {

class CORE_API MallocAllocator final : public IEngineAllocator {
public:
    void* Allocate(uint64_t size, uint32_t align) override;
    void  Free(void* ptr, uint64_t size, uint32_t align) override;
};

}  // namespace pe
