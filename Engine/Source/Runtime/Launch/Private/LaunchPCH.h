// LaunchPCH.h - Launch module precompiled header (private).
//
// Launch is the bootstrap glue that touches almost every public engine surface.
// Its PCH pre-includes the full set so LaunchEngineLoop.cpp compiles fast.

#pragma once

#include <Core/CorePCH.h>

#include <RHI/RHITypes.h>
#include <RHI/IRHIDevice.h>
#include <RHI/IRHIBackendModule.h>

#include <ApplicationCore/IPlatformApplication.h>
#include <ApplicationCore/IWindow.h>
#include <ApplicationCore/PlatformBackend.h>

#include <Renderer/Renderer.h>

#include <Launch/LaunchAPI.h>
#include <Launch/LaunchEngineLoop.h>
