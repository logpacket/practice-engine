// LaunchEngineLoop.h - the single entry point a host executable calls.
//
// Architecture.md §6.f. Owns the FEngineContext, loads ApplicationCore + VulkanRHI,
// creates the window + device + renderer, runs the main loop until ShouldClose,
// then tears everything down in reverse order.

#pragma once

#include <Core/CoreAPI.h>

namespace pe {

// Returns the process exit code. Negative on bootstrap failure, 0 on clean exit.
CORE_API int LaunchEngineLoop(int argc, char** argv);

}  // namespace pe
