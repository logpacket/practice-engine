// VulkanRHIPCH.h - VulkanRHI module precompiled header (private).
//
// Pre-includes volk + Core + RHI interfaces + the internal helpers every
// VulkanRHI .cpp reaches for. Speeds up incremental builds substantially
// because Vulkan + volk + Core PCH together are ~50k lines pre-parsed.

#pragma once

#include <Core/CorePCH.h>

#include <RHI/RHITypes.h>
#include <RHI/IRHICommandList.h>
#include <RHI/IRHIDevice.h>
#include <RHI/IRHIBackendModule.h>

#include <volk.h>

#include "VulkanCommon.h"
#include "VulkanResourcePool.h"
#include "VulkanResources.h"

#include <algorithm>
#include <array>
