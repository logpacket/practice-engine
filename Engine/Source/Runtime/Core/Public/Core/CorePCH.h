// CorePCH.h - Core module precompiled header.
//
// Applied via target_precompile_headers in Engine/Source/Runtime/Core/CMakeLists.txt.
// Pre-includes the engine-facing headers and stdlib that Core's own .cpp files
// reach for repeatedly. Other modules may also include this when they want the
// full Core surface in one shot (e.g. their own PCH headers do this).

#pragma once

// Core generated export macro (CORE_API).
#include <Core/CoreAPI.h>

// Foundational ABI types + base utilities.
#include <Core/EngineAbi.hpp>
#include <Core/Types.h>
#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Paths.h>
#include <Core/IEngineContext.h>
#include <Core/MallocAllocator.h>
#include <Core/Module.h>
#include <Core/ModuleLoader.h>

// Stdlib hot-spots.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
