// PlatformBackend.h - configure-time-selected PAL backend factory.
//
// The host calls pe::CreatePlatformApplication() to obtain the
// IPlatformApplication implementation. Exactly one backend's source file
// provides the definitions of these two functions; CMake selects which one is
// compiled in via the ENGINE_APP_BACKEND option (Stage 1 default: GLFWPlatform).
//
// This is configure-time selection, not runtime dlopen (ADR-0005 + ADR-0018).
// The host never has to know which backend it is using; it links the
// `Engine::Platform` ALIAS target, which the active backend declares.

#pragma once

#include <ApplicationCore/IPlatformApplication.h>

namespace pe {

// Construct the active PAL backend's IPlatformApplication. Heap-allocated;
// host owns. Pass the returned pointer to DestroyPlatformApplication on
// shutdown. Returns nullptr on failure (rare; allocation errors only).
IPlatformApplication* CreatePlatformApplication();

// Destroys an instance produced by CreatePlatformApplication. Idempotent on
// nullptr. Casts internally to the concrete backend type before delete.
void DestroyPlatformApplication(IPlatformApplication* pa);

}  // namespace pe
