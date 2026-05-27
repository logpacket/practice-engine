// GLFWPlatformPCH.h - GLFWPlatform module precompiled header (private).
//
// Pre-includes Core + the PAL public surface so the GLFW backend .cpp files
// compile fast. GLFW headers themselves stay out of the PCH because glfw3native.h
// pulls in OS headers (X11 / Wayland / Win32) conditionally on macros; keeping
// it per-source means the per-OS path lands in only the file that needs it.

#pragma once

#include <Core/CorePCH.h>

#include <ApplicationCore/PlatformKey.h>
#include <ApplicationCore/IWindow.h>
#include <ApplicationCore/IPlatformApplication.h>
