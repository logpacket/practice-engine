// VulkanCommon.h - VulkanRHI-internal forward declarations and helpers.

#pragma once

#include <Core/Assert.h>
#include <Core/Logging.h>
#include <Core/Module.h>
#include <RHI/IRHIBackendModule.h>
#include <RHI/IRHICommandList.h>
#include <RHI/IRHIDevice.h>
#include <RHI/RHITypes.h>

// volk provides every Vulkan symbol via runtime dlsym of the loader. We never
// link the Vulkan loader at link time; only volk's wrapper functions are called.
#include <volk.h>

#include <cstdint>
#include <string_view>

namespace pe::vk {

DECLARE_LOG_CATEGORY(LogVulkanRHI)

// VK_CHECK halts on any non-success VkResult. Used at startup paths where
// failure is unrecoverable.
#define PE_VK_CHECK(expr)                                                                \
    do {                                                                                 \
        const VkResult vk_result_ = (expr);                                              \
        if (vk_result_ != VK_SUCCESS) {                                                  \
            ENGINE_LOG_ERROR(LogVulkanRHI, "{} failed: VkResult {}", #expr, static_cast<int>(vk_result_)); \
            ENGINE_FATAL("Vulkan call failed; see log for details");                     \
        }                                                                                \
    } while (0)

}  // namespace pe::vk
