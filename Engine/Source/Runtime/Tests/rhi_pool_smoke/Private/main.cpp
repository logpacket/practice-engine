// rhi_pool_smoke - Stage 2 §6.a verification (ADR-0021).
//
// Instantiates TResourcePool host-side (no Vulkan, no device) and verifies the
// handle generation counter: Insert/Get/Remove round-trip, and - in Debug - a
// stale (generation-mismatched) handle is rejected with a fatal assert. The
// fatal path is exercised in a forked child process that must die by signal.

#include <Core/Assert.h>
#include <Core/Types.h>

#include "VulkanResourcePool.h"

#include <cstdio>

#if !defined(_WIN32)
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace {

struct TestPayload {
    pe::int32 value = 0;
};

struct TestTag;
using TestPool   = pe::vk::TResourcePool<TestPayload, TestTag>;
using TestHandle = pe::RHIHandle<TestTag>;

#if defined(ENGINE_BUILD_DEBUG) && !defined(_WIN32)
// Runs fn in a forked child (stderr silenced) and expects the engine assert to
// kill it by signal. Returns true when the child died by signal.
template <typename F>
bool DiesByAssert(F&& fn) {
    const pid_t pid = fork();
    if (pid == 0) {
        // Child: the assert output is expected noise - drop it.
        std::freopen("/dev/null", "w", stderr);
        fn();
        _exit(0);  // reached only if the assert did NOT fire
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status);
}
#endif

}  // namespace

int main() {
    TestPool pool;

    // --- Round-trip: Insert / Get / Remove ---------------------------------
    const TestHandle h1 = pool.Insert(TestPayload{11});
    const TestHandle h2 = pool.Insert(TestPayload{22});
    ENGINE_VERIFY(h1.valid());
    ENGINE_VERIFY(h2.valid());
    ENGINE_VERIFY(!(h1 == h2));
    ENGINE_VERIFY(pool.Get(h1)->value == 11);
    ENGINE_VERIFY(pool.Get(h2)->value == 22);
    ENGINE_VERIFY(pool.LiveCount() == 2);

    const TestPayload taken = pool.Remove(h1);
    ENGINE_VERIFY(taken.value == 11);
    ENGINE_VERIFY(pool.LiveCount() == 1);

    // --- Re-allocate after destroy ------------------------------------------
    // §6.a: Destroyed slots are not recycled (that is §6.b deferred-delete
    // reclamation), so the new resource must land on a different handle.
    const TestHandle h3 = pool.Insert(TestPayload{33});
    ENGINE_VERIFY(h3.valid());
    ENGINE_VERIFY(!(h3 == h1));
    ENGINE_VERIFY(pool.Get(h3)->value == 33);
    ENGINE_VERIFY(pool.LiveCount() == 2);

#if defined(ENGINE_BUILD_DEBUG) && !defined(_WIN32)
    // --- Stale handle after Remove: generation was bumped -> Debug FATAL ----
    ENGINE_VERIFY(DiesByAssert([&] { (void)pool.Get(h1); }));

    // --- Forged generation on a *live* slot: the aliasing case --------------
    const TestHandle forged{h3.index, h3.generation - 1};
    ENGINE_VERIFY(DiesByAssert([&] { (void)pool.Get(forged); }));

    std::printf("rhi_pool_smoke OK (stale-handle death tests ran)\n");
#else
    std::printf("rhi_pool_smoke OK (death tests skipped: Release or Windows)\n");
#endif
    return 0;
}
