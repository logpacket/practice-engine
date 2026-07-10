#include "VulkanDevice.h"
#include "VulkanCommandList.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace pe::vk {

namespace {

constexpr const char kEngineName[]  = "practice-engine";
constexpr const char kAppName[]     = "practice-engine app";
constexpr uint32_t   kEngineVersion = VK_MAKE_VERSION(0, 1, 0);
constexpr uint32_t   kAppVersion    = VK_MAKE_VERSION(0, 1, 0);
constexpr const char kValidationLayerName[] = "VK_LAYER_KHRONOS_validation";

// Device extensions that are gated on whether the caller plans to create a
// swapchain. VK_KHR_swapchain requires VK_KHR_surface in the instance extension
// list - we only enable it when create_surface is supplied (i.e. someone is
// expected to make a surface).
constexpr const char* kSwapchainDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user_data*/) {
    const char* msg = (data != nullptr && data->pMessage != nullptr) ? data->pMessage : "(no message)";

    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "[VK_ERROR] {}", msg);
        ENGINE_FATAL("Vulkan validation ERROR; see log for details");
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        ENGINE_LOG_WARN(LogVulkanRHI, "[VK_WARNING] {}", msg);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) != 0) {
        ENGINE_LOG_INFO(LogVulkanRHI, "[VK_INFO] {}", msg);
    } else {
        ENGINE_LOG_TRACE(LogVulkanRHI, "[VK_VERBOSE] {}", msg);
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;
    return info;
}

bool ValidationLayerAvailable() {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers) {
        if (std::strcmp(l.layerName, kValidationLayerName) == 0) { return true; }
    }
    return false;
}

VkFormat ToVkFormat(ERHIFormat f) {
    switch (f) {
        case ERHIFormat::R8G8B8A8_UNORM:    return VK_FORMAT_R8G8B8A8_UNORM;
        case ERHIFormat::R8G8B8A8_SRGB:     return VK_FORMAT_R8G8B8A8_SRGB;
        case ERHIFormat::B8G8R8A8_UNORM:    return VK_FORMAT_B8G8R8A8_UNORM;
        case ERHIFormat::B8G8R8A8_SRGB:     return VK_FORMAT_B8G8R8A8_SRGB;
        case ERHIFormat::R32G32_SFLOAT:     return VK_FORMAT_R32G32_SFLOAT;
        case ERHIFormat::R32G32B32_SFLOAT:  return VK_FORMAT_R32G32B32_SFLOAT;
        case ERHIFormat::R32G32B32A32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case ERHIFormat::D32_SFLOAT:        return VK_FORMAT_D32_SFLOAT;
        default:                            return VK_FORMAT_UNDEFINED;
    }
}

VkImageAspectFlags AspectFromVkFormat(VkFormat f) {
    return f == VK_FORMAT_D32_SFLOAT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

// Stage 1: present mode is hardcoded to FIFO (Architecture.md §3.6 policy);
// MAILBOX/Immediate arrive with the Stage 2 swapchain rework.
// Shader stage conversion is inlined in CreateGraphicsPipeline (two stages only).

// RHISemaphore <-> VkSemaphore. VkSemaphore is a 64-bit non-dispatchable
// handle; the opaque field carries it across the ABI unchanged.
RHISemaphore WrapSemaphore(VkSemaphore s) noexcept {
    return RHISemaphore{reinterpret_cast<uint64_t>(s)};
}
VkSemaphore UnwrapSemaphore(RHISemaphore s) noexcept {
    return reinterpret_cast<VkSemaphore>(s.opaque);
}

}  // namespace

// ----- Lifecycle -----

FVulkanDevice::FVulkanDevice(const RHIDeviceCreateDesc& desc, bool& out_failed) {
    out_failed = true;
    validation_enabled_      = desc.enable_validation;
    create_surface_          = desc.create_surface;
    create_surface_userdata_ = desc.create_surface_userdata;

    if (!CreateInstance(desc))     { return; }
    if (!CreateDebugMessenger())   { return; }
    if (!SelectPhysicalDevice())   { return; }
    if (!CreateLogicalDevice())    { return; }
    if (!CreateFrameResources())   { return; }

    out_failed = false;
    ENGINE_LOG_INFO(LogVulkanRHI, "FVulkanDevice ready (queue family {})", graphics_queue_family_);
}

FVulkanDevice::~FVulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        // Reclaim everything still pending, then drain pools (most resources
        // should already have been Destroy()-ed by the caller).
        DrainDeferred(UINT64_MAX);
        command_lists_.ForEachLive([this](VulkanCommandListPayload& p) { DestroyCommandListPayload(p); });
        swapchains_.ForEachLive   ([this](VulkanSwapchainPayload& p)   { DestroySwapchainPayload(p); });
        pipelines_.ForEachLive    ([this](VulkanPipelinePayload& p)    { DestroyPipelinePayload(p); });
        shaders_.ForEachLive      ([this](VulkanShaderPayload& p)      { DestroyShaderPayload(p); });
        buffers_.ForEachLive      ([this](VulkanBufferPayload& p)      { DestroyBufferPayload(p); });
        textures_.ForEachLive     ([this](VulkanTexturePayload& p)     { DestroyTexturePayload(p); });
        samplers_.ForEachLive     ([this](VulkanSamplerPayload& p)     { DestroySamplerPayload(p); });

        for (FFrameSlot& slot : frames_) {
            if (slot.pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, slot.pool, nullptr);
                slot.pool = VK_NULL_HANDLE;
            }
            slot.lists.clear();
        }
        if (frame_timeline_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame_timeline_, nullptr);
            frame_timeline_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (debug_messenger_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

// ----- Init helpers -----

bool FVulkanDevice::CreateInstance(const RHIDeviceCreateDesc& desc) {
    if (volkInitialize() != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "volkInitialize failed - is the Vulkan loader installed?");
        return false;
    }

    VkApplicationInfo app_info{};
    app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName   = kAppName;
    app_info.applicationVersion = kAppVersion;
    app_info.pEngineName        = kEngineName;
    app_info.engineVersion      = kEngineVersion;
    app_info.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> instance_extensions;
    for (uint64_t i = 0; i < desc.required_instance_extensions.size; ++i) {
        instance_extensions.push_back(desc.required_instance_extensions.data[i]);
    }
    if (validation_enabled_) {
        instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (validation_enabled_) {
        if (ValidationLayerAvailable()) {
            layers.push_back(kValidationLayerName);
            ENGINE_LOG_INFO(LogVulkanRHI, "Validation layer enabled");
        } else {
            ENGINE_LOG_WARN(LogVulkanRHI,
                "Validation requested but {} not available; continuing without",
                kValidationLayerName);
            validation_enabled_ = false;
        }
    }

    VkInstanceCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo        = &app_info;
    info.enabledExtensionCount   = static_cast<uint32_t>(instance_extensions.size());
    info.ppEnabledExtensionNames = instance_extensions.data();
    info.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames     = layers.data();

    VkDebugUtilsMessengerCreateInfoEXT messenger_ci = MakeDebugMessengerCreateInfo();
    if (validation_enabled_) { info.pNext = &messenger_ci; }

    const VkResult r = vkCreateInstance(&info, nullptr, &instance_);
    if (r != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateInstance failed: VkResult {}", static_cast<int>(r));
        return false;
    }

    volkLoadInstance(instance_);
    ENGINE_LOG_INFO(LogVulkanRHI, "VkInstance created (API 1.3, {} extensions, {} layers)",
                    instance_extensions.size(), layers.size());
    return true;
}

bool FVulkanDevice::CreateDebugMessenger() {
    if (!validation_enabled_) { return true; }
    const VkDebugUtilsMessengerCreateInfoEXT info = MakeDebugMessengerCreateInfo();
    const VkResult r = vkCreateDebugUtilsMessengerEXT(instance_, &info, nullptr, &debug_messenger_);
    if (r != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateDebugUtilsMessengerEXT failed: VkResult {}",
                         static_cast<int>(r));
        return false;
    }
    return true;
}

bool FVulkanDevice::SelectPhysicalDevice() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    if (device_count == 0) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "No Vulkan physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);

        if (props.apiVersion < VK_API_VERSION_1_3) {
            ENGINE_LOG_INFO(LogVulkanRHI, "Skipping '{}' - apiVersion < 1.3", props.deviceName);
            continue;
        }

        VkPhysicalDeviceVulkan12Features feat12{};
        feat12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceVulkan13Features feat13{};
        feat13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        feat13.pNext = &feat12;
        VkPhysicalDeviceFeatures2 feat2{};
        feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feat2.pNext = &feat13;
        vkGetPhysicalDeviceFeatures2(candidate, &feat2);
        if (feat13.dynamicRendering == VK_FALSE || feat13.synchronization2 == VK_FALSE ||
            feat12.timelineSemaphore == VK_FALSE) {
            ENGINE_LOG_INFO(LogVulkanRHI,
                "Skipping '{}' - dynamicRendering/synchronization2/timelineSemaphore unsupported",
                props.deviceName);
            continue;
        }

        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qf_count, qfs.data());

        uint32_t graphics_family = UINT32_MAX;
        for (uint32_t i = 0; i < qf_count; ++i) {
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphics_family = i;
                break;
            }
        }
        if (graphics_family == UINT32_MAX) {
            ENGINE_LOG_INFO(LogVulkanRHI, "Skipping '{}' - no graphics queue family", props.deviceName);
            continue;
        }

        physical_device_       = candidate;
        graphics_queue_family_ = graphics_family;
        ENGINE_LOG_INFO(LogVulkanRHI, "Selected '{}' (apiVersion {}.{}.{}, queue family {})",
                        props.deviceName,
                        VK_API_VERSION_MAJOR(props.apiVersion),
                        VK_API_VERSION_MINOR(props.apiVersion),
                        VK_API_VERSION_PATCH(props.apiVersion),
                        graphics_family);
        return true;
    }

    ENGINE_FATAL("No Vulkan 1.3 device with dynamicRendering + sync2 + graphics queue found "
                 "(required by Architecture.md §6.c)");
    return false;
}

bool FVulkanDevice::CreateLogicalDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = graphics_queue_family_;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan12Features feat12{};
    feat12.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    feat12.timelineSemaphore = VK_TRUE;  // ADR-0020 (core in the 1.3 baseline)

    VkPhysicalDeviceVulkan13Features feat13{};
    feat13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    feat13.pNext            = &feat12;
    feat13.dynamicRendering = VK_TRUE;
    feat13.synchronization2 = VK_TRUE;

    VkDeviceCreateInfo info{};
    info.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.pNext                = &feat13;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos    = &qci;

    // Enable swapchain extension only when the caller indicated intent to swapchain.
    const bool want_swapchain = (create_surface_ != nullptr);
    if (want_swapchain) {
        info.enabledExtensionCount   = static_cast<uint32_t>(std::size(kSwapchainDeviceExtensions));
        info.ppEnabledExtensionNames = kSwapchainDeviceExtensions;
    }

    const VkResult r = vkCreateDevice(physical_device_, &info, nullptr, &device_);
    if (r != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateDevice failed: VkResult {}", static_cast<int>(r));
        return false;
    }

    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
    ENGINE_LOG_INFO(LogVulkanRHI, "VkDevice created (1 graphics queue, swapchain={})",
                    want_swapchain ? "enabled" : "disabled (headless)");
    return true;
}

bool FVulkanDevice::CreateFrameResources() {
    // One command pool per frame slot (ADR-0020): the pool is bulk-reset at
    // frame start after the timeline wait proves the slot's frame completed.
    // RESET_COMMAND_BUFFER_BIT stays so Begin() may also reset individually
    // (headless callers never advance the frame ring).
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = graphics_queue_family_;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (FFrameSlot& slot : frames_) {
        const VkResult r = vkCreateCommandPool(device_, &info, nullptr, &slot.pool);
        if (r != VK_SUCCESS) {
            ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateCommandPool failed: VkResult {}",
                             static_cast<int>(r));
            return false;
        }
    }

    VkSemaphoreTypeCreateInfo type_ci{};
    type_ci.sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type_ci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    type_ci.initialValue  = 0;

    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sem_ci.pNext = &type_ci;

    const VkResult r = vkCreateSemaphore(device_, &sem_ci, nullptr, &frame_timeline_);
    if (r != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateSemaphore (timeline) failed: VkResult {}",
                         static_cast<int>(r));
        return false;
    }
    return true;
}

uint32_t FVulkanDevice::FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) != 0 &&
            (mem.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

// ----- Resource creation -----

RHIBufferHandle FVulkanDevice::CreateBuffer(const RHIBufferDesc& desc) {
    if (desc.size_bytes == 0) { return {}; }

    VkBufferUsageFlags usage = 0;
    if (any(desc.usage, ERHIBufferUsage::VertexBuffer))  { usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; }
    if (any(desc.usage, ERHIBufferUsage::IndexBuffer))   { usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; }
    if (any(desc.usage, ERHIBufferUsage::UniformBuffer)) { usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; }
    if (any(desc.usage, ERHIBufferUsage::Storage))       { usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; }

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = desc.size_bytes;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VulkanBufferPayload payload;
    payload.size_bytes = desc.size_bytes;
    PE_VK_CHECK(vkCreateBuffer(device_, &bci, nullptr, &payload.buffer));

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, payload.buffer, &req);

    // Stage 1: host-visible + host-coherent memory for direct CPU upload.
    const uint32_t mem_type = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "No host-visible+coherent memory type for buffer");
        vkDestroyBuffer(device_, payload.buffer, nullptr);
        return {};
    }

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = mem_type;
    PE_VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &payload.memory));
    PE_VK_CHECK(vkBindBufferMemory(device_, payload.buffer, payload.memory, 0));

    if (desc.initial_data != nullptr && desc.initial_size > 0) {
        void* mapped = nullptr;
        PE_VK_CHECK(vkMapMemory(device_, payload.memory, 0, desc.initial_size, 0, &mapped));
        std::memcpy(mapped, desc.initial_data, static_cast<size_t>(desc.initial_size));
        vkUnmapMemory(device_, payload.memory);
    }

    return buffers_.Insert(std::move(payload));
}

RHIShaderHandle FVulkanDevice::CreateShader(const RHIShaderDesc& desc) {
    if (desc.spirv.data == nullptr || desc.spirv.size == 0) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "CreateShader: empty SPIR-V");
        return {};
    }
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = static_cast<size_t>(desc.spirv.size);
    info.pCode    = reinterpret_cast<const uint32_t*>(desc.spirv.data);

    VulkanShaderPayload payload;
    payload.stage = desc.stage;
    PE_VK_CHECK(vkCreateShaderModule(device_, &info, nullptr, &payload.module));
    return shaders_.Insert(std::move(payload));
}

RHIPipelineHandle FVulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    auto* vs = shaders_.Get(desc.vertex_shader);
    auto* fs = shaders_.Get(desc.fragment_shader);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->module;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = desc.vertex_stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs;
    attrs.reserve(desc.vertex_attributes.size);
    for (uint64_t i = 0; i < desc.vertex_attributes.size; ++i) {
        const auto& a = desc.vertex_attributes.data[i];
        VkVertexInputAttributeDescription v{};
        v.binding  = 0;
        v.location = a.location;
        v.format   = ToVkFormat(a.format);
        v.offset   = a.offset;
        attrs.push_back(v);
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = desc.vertex_stride > 0 ? 1u : 0u;
    vi.pVertexBindingDescriptions      = desc.vertex_stride > 0 ? &binding : nullptr;
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blend_att;

    const std::array<VkDynamicState, 2> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dyn.pDynamicStates    = dynamic_states.data();

    VulkanPipelinePayload payload;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PE_VK_CHECK(vkCreatePipelineLayout(device_, &plci, nullptr, &payload.layout));

    const bool has_depth = desc.depth_attachment_format != ERHIFormat::Unknown;

    VkFormat color_fmt = ToVkFormat(desc.color_attachment_format);
    VkPipelineRenderingCreateInfo render_info{};
    render_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    render_info.colorAttachmentCount    = 1;
    render_info.pColorAttachmentFormats = &color_fmt;
    if (has_depth) {
        render_info.depthAttachmentFormat = ToVkFormat(desc.depth_attachment_format);
    }

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.pNext               = &render_info;
    gpci.stageCount          = static_cast<uint32_t>(stages.size());
    gpci.pStages             = stages.data();
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = has_depth ? &ds : nullptr;
    gpci.pColorBlendState    = &blend;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = payload.layout;

    PE_VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                          &payload.pipeline));

    return pipelines_.Insert(std::move(payload));
}

RHITextureHandle FVulkanDevice::CreateTexture(const RHITextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0 || desc.format == ERHIFormat::Unknown) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "CreateTexture: invalid desc ({}x{})", desc.width, desc.height);
        return {};
    }

    VkImageUsageFlags usage = 0;
    if (any(desc.usage, ERHITextureUsage::RenderTarget)) { usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; }
    if (any(desc.usage, ERHITextureUsage::DepthStencil)) { usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT; }
    if (any(desc.usage, ERHITextureUsage::Sampled))      { usage |= VK_IMAGE_USAGE_SAMPLED_BIT; }
    if (any(desc.usage, ERHITextureUsage::CopySrc))      { usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; }
    if (any(desc.usage, ERHITextureUsage::CopyDst))      { usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; }

    VulkanTexturePayload payload;
    payload.format = ToVkFormat(desc.format);
    payload.extent = {desc.width, desc.height};

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = payload.format;
    ici.extent        = {desc.width, desc.height, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = usage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    PE_VK_CHECK(vkCreateImage(device_, &ici, nullptr, &payload.image));

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, payload.image, &req);
    const uint32_t mem_type = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "No device-local memory type for texture");
        vkDestroyImage(device_, payload.image, nullptr);
        return {};
    }
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = mem_type;
    PE_VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &payload.memory));
    PE_VK_CHECK(vkBindImageMemory(device_, payload.image, payload.memory, 0));

    VkImageViewCreateInfo ivci{};
    ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image                           = payload.image;
    ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format                          = payload.format;
    ivci.subresourceRange.aspectMask     = AspectFromVkFormat(payload.format);
    ivci.subresourceRange.levelCount     = 1;
    ivci.subresourceRange.layerCount     = 1;
    PE_VK_CHECK(vkCreateImageView(device_, &ivci, nullptr, &payload.view));

    return textures_.Insert(std::move(payload));
}

RHISamplerHandle FVulkanDevice::CreateSampler(const RHISamplerDesc& desc) {
    const auto to_filter = [](ERHIFilter f) {
        return f == ERHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    };
    const auto to_address = [](ERHIAddressMode m) {
        switch (m) {
            case ERHIAddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case ERHIAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case ERHIAddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            case ERHIAddressMode::ClampToEdge:
            default:                              return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    };

    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.minFilter    = to_filter(desc.min_filter);
    sci.magFilter    = to_filter(desc.mag_filter);
    sci.mipmapMode   = desc.mipmap_mode == ERHIMipmapMode::Linear
                           ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = to_address(desc.address_mode);
    sci.addressModeV = to_address(desc.address_mode);
    sci.addressModeW = to_address(desc.address_mode);
    sci.maxLod       = VK_LOD_CLAMP_NONE;

    VulkanSamplerPayload payload;
    PE_VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &payload.sampler));
    return samplers_.Insert(std::move(payload));
}

RHITextureHandle FVulkanDevice::GetSwapchainImageTexture(RHISwapchainHandle h, uint32_t image_index) {
    auto* p = swapchains_.Get(h);
    ENGINE_CHECK(image_index < p->image_textures.size());
    return p->image_textures[image_index];
}

RHISwapchainHandle FVulkanDevice::CreateSwapchain(const RHISwapchainDesc& desc) {
    if (create_surface_ == nullptr) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "CreateSwapchain called but create_surface callback is null");
        return {};
    }

    VulkanSwapchainPayload payload;
    payload.extent = {desc.width, desc.height};

    // 1. Create the surface via PAL callback.
    void* surface_opaque = nullptr;
    const EngineResult sr = create_surface_(create_surface_userdata_,
                                            reinterpret_cast<void*>(instance_),
                                            &surface_opaque);
    if (!sr.ok() || surface_opaque == nullptr) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "PAL create_surface failed: code {}", sr.code);
        return {};
    }
    payload.surface = static_cast<VkSurfaceKHR>(surface_opaque);

    // 2. Verify present support for our graphics queue.
    VkBool32 present_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, graphics_queue_family_,
                                         payload.surface, &present_supported);
    if (present_supported == VK_FALSE) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "Graphics queue family does not support present on this surface");
        vkDestroySurfaceKHR(instance_, payload.surface, nullptr);
        return {};
    }

    // 3. Surface capabilities + format/present mode selection.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, payload.surface, &caps);

    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, payload.surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, payload.surface, &fmt_count, formats.data());

    const VkFormat preferred = ToVkFormat(desc.preferred_format);
    VkSurfaceFormatKHR chosen{};
    chosen.format = VK_FORMAT_UNDEFINED;
    for (const auto& f : formats) {
        if (f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f; break;
        }
    }
    if (chosen.format == VK_FORMAT_UNDEFINED) {
        // Stage 1 sRGB policy (Architecture.md §3.6): accept either of the two
        // standard sRGB color formats. If the surface offers neither we FATAL,
        // because the design forbids non-sRGB swapchain in Stage 1.
        for (const auto& f : formats) {
            if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f; break;
            }
        }
    }
    if (chosen.format == VK_FORMAT_UNDEFINED) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "No sRGB swapchain format on this surface");
        vkDestroySurfaceKHR(instance_, payload.surface, nullptr);
        return {};
    }
    payload.format = chosen.format;

    // FIFO is guaranteed by the Vulkan spec to be supported on every surface.
    const VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

    uint32_t image_count = std::max(desc.image_count, caps.minImageCount);
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = payload.surface;
    sci.minImageCount    = image_count;
    sci.imageFormat      = chosen.format;
    sci.imageColorSpace  = chosen.colorSpace;
    sci.imageExtent      = payload.extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = present_mode;
    sci.clipped          = VK_TRUE;

    PE_VK_CHECK(vkCreateSwapchainKHR(device_, &sci, nullptr, &payload.swapchain));

    // 4. Retrieve images + create image views.
    uint32_t got_count = 0;
    vkGetSwapchainImagesKHR(device_, payload.swapchain, &got_count, nullptr);
    payload.images.resize(got_count);
    vkGetSwapchainImagesKHR(device_, payload.swapchain, &got_count, payload.images.data());
    payload.image_views.resize(got_count);
    for (uint32_t i = 0; i < got_count; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image                           = payload.images[i];
        ivci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format                          = chosen.format;
        ivci.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        ivci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel   = 0;
        ivci.subresourceRange.levelCount     = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount     = 1;
        PE_VK_CHECK(vkCreateImageView(device_, &ivci, nullptr, &payload.image_views[i]));
    }

    // 5. Borrowed-texture wrappers (ADR-0021/0026): one pool slot per image,
    //    allocated here (not per GetSwapchainImageTexture call). Image and
    //    view stay owned by this payload; the wrapper is external.
    payload.image_textures.resize(got_count);
    for (uint32_t i = 0; i < got_count; ++i) {
        VulkanTexturePayload wrapper;
        wrapper.image    = payload.images[i];
        wrapper.view     = payload.image_views[i];
        wrapper.format   = chosen.format;
        wrapper.extent   = payload.extent;
        wrapper.external = true;
        payload.image_textures[i] = textures_.Insert(std::move(wrapper));
    }

    // 6. Sync objects (ADR-0020): binary semaphores survive only at the
    //    swapchain boundary - image_available per frame slot (the timeline
    //    wait at frame start guarantees the slot's previous acquire semaphore
    //    was consumed), render_finished per image (the presentation engine may
    //    still hold the previous signal of the prior image).
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    payload.image_available_per_slot.resize(kMaxFramesInFlight, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        PE_VK_CHECK(vkCreateSemaphore(device_, &sem_ci, nullptr, &payload.image_available_per_slot[i]));
    }
    payload.render_finished_per_image.resize(got_count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < got_count; ++i) {
        PE_VK_CHECK(vkCreateSemaphore(device_, &sem_ci, nullptr, &payload.render_finished_per_image[i]));
    }

    ENGINE_LOG_INFO(LogVulkanRHI, "Swapchain created: {}x{}, {} images, format={}",
                    desc.width, desc.height, got_count, static_cast<int>(chosen.format));
    return swapchains_.Insert(std::move(payload));
}

// ----- Destroy -----

void FVulkanDevice::DestroyBufferPayload(VulkanBufferPayload& p) {
    if (p.buffer != VK_NULL_HANDLE) { vkDestroyBuffer(device_, p.buffer, nullptr); p.buffer = VK_NULL_HANDLE; }
    if (p.memory != VK_NULL_HANDLE) { vkFreeMemory(device_, p.memory, nullptr); p.memory = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroyShaderPayload(VulkanShaderPayload& p) {
    if (p.module != VK_NULL_HANDLE) { vkDestroyShaderModule(device_, p.module, nullptr); p.module = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroyPipelinePayload(VulkanPipelinePayload& p) {
    if (p.pipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device_, p.pipeline, nullptr); p.pipeline = VK_NULL_HANDLE; }
    if (p.layout != VK_NULL_HANDLE)   { vkDestroyPipelineLayout(device_, p.layout, nullptr); p.layout = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroyTexturePayload(VulkanTexturePayload& p) {
    if (p.external) {
        // Borrowed swapchain image (ADR-0021): image and view are owned by the
        // swapchain payload - free nothing here.
        p = VulkanTexturePayload{};
        return;
    }
    if (p.view != VK_NULL_HANDLE)   { vkDestroyImageView(device_, p.view, nullptr); p.view = VK_NULL_HANDLE; }
    if (p.image != VK_NULL_HANDLE)  { vkDestroyImage(device_, p.image, nullptr); p.image = VK_NULL_HANDLE; }
    if (p.memory != VK_NULL_HANDLE) { vkFreeMemory(device_, p.memory, nullptr); p.memory = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroySamplerPayload(VulkanSamplerPayload& p) {
    if (p.sampler != VK_NULL_HANDLE) { vkDestroySampler(device_, p.sampler, nullptr); p.sampler = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroySwapchainPayload(VulkanSwapchainPayload& p) {
    // Remove the borrowed-texture wrappers first: bumps their generation so a
    // cached handle from before destroy/recreate is a Debug FATAL (ADR-0026).
    for (RHITextureHandle t : p.image_textures) {
        auto wrapper = textures_.Remove(t);
        DestroyTexturePayload(wrapper);  // external: no-op on GPU objects
    }
    p.image_textures.clear();

    for (VkSemaphore s : p.render_finished_per_image) {
        if (s != VK_NULL_HANDLE) { vkDestroySemaphore(device_, s, nullptr); }
    }
    p.render_finished_per_image.clear();
    for (VkSemaphore s : p.image_available_per_slot) {
        if (s != VK_NULL_HANDLE) { vkDestroySemaphore(device_, s, nullptr); }
    }
    p.image_available_per_slot.clear();
    for (VkImageView v : p.image_views) { if (v != VK_NULL_HANDLE) { vkDestroyImageView(device_, v, nullptr); } }
    p.image_views.clear();
    p.images.clear();
    if (p.swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, p.swapchain, nullptr); p.swapchain = VK_NULL_HANDLE; }
    if (p.surface != VK_NULL_HANDLE)   { vkDestroySurfaceKHR(instance_, p.surface, nullptr); p.surface = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroyCommandListPayload(VulkanCommandListPayload& p) {
    // The VkCommandBuffer is owned by its frame slot's pool and freed with it.
    p.cmd = VK_NULL_HANDLE;
    if (p.wrapper != nullptr) {
        delete p.wrapper;
        p.wrapper = nullptr;
    }
}

// Deferred-delete (ADR-0021): the payload is reclaimed once the frame timeline
// passes the frame value current at Destroy time.
void FVulkanDevice::Destroy(RHIBufferHandle h) {
    deferred_buffers_.push_back({CurrentFrameValue(), buffers_.Remove(h)});
}
void FVulkanDevice::Destroy(RHIShaderHandle h) {
    deferred_shaders_.push_back({CurrentFrameValue(), shaders_.Remove(h)});
}
void FVulkanDevice::Destroy(RHIPipelineHandle h) {
    deferred_pipelines_.push_back({CurrentFrameValue(), pipelines_.Remove(h)});
}
void FVulkanDevice::Destroy(RHISwapchainHandle h) {
    // Swapchain teardown is a shutdown/recreate path, never steady-loop:
    // stall so the presentation engine and in-flight frames release the images.
    vkDeviceWaitIdle(device_);
    auto p = swapchains_.Remove(h);
    DestroySwapchainPayload(p);
}
void FVulkanDevice::Destroy(RHITextureHandle h) {
    deferred_textures_.push_back({CurrentFrameValue(), textures_.Remove(h)});
}
void FVulkanDevice::Destroy(RHISamplerHandle h) {
    deferred_samplers_.push_back({CurrentFrameValue(), samplers_.Remove(h)});
}

void FVulkanDevice::DrainDeferred(uint64_t completed) {
    const auto drain = [completed](auto& queue, auto&& destroy_fn) {
        auto it = queue.begin();
        while (it != queue.end()) {
            if (it->stamp <= completed) {
                destroy_fn(it->payload);
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    };
    drain(deferred_buffers_,   [this](VulkanBufferPayload& p)   { DestroyBufferPayload(p); });
    drain(deferred_shaders_,   [this](VulkanShaderPayload& p)   { DestroyShaderPayload(p); });
    drain(deferred_pipelines_, [this](VulkanPipelinePayload& p) { DestroyPipelinePayload(p); });
    drain(deferred_textures_,  [this](VulkanTexturePayload& p)  { DestroyTexturePayload(p); });
    drain(deferred_samplers_,  [this](VulkanSamplerPayload& p)  { DestroySamplerPayload(p); });
}

// ----- Command lists -----

RHICommandListHandle FVulkanDevice::AcquireCommandList() {
    FFrameSlot& slot = frames_[current_slot_];

    // Recycle a wrapper recorded MAX_FRAMES_IN_FLIGHT frames ago; the pool
    // reset at frame start returned its buffer to the initial state.
    if (slot.used < slot.lists.size()) {
        return slot.lists[slot.used++];
    }

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = slot.pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VulkanCommandListPayload payload;
    PE_VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &payload.cmd));
    payload.wrapper = new FVulkanCommandList(*this, payload.cmd);

    const RHICommandListHandle handle = command_lists_.Insert(std::move(payload));
    slot.lists.push_back(handle);
    slot.used++;
    return handle;
}

IRHICommandList* FVulkanDevice::Lock(RHICommandListHandle h) {
    return command_lists_.Get(h)->wrapper;
}

EngineResult FVulkanDevice::Submit(RHICommandListHandle h, const RHISubmitInfo& sync) {
    auto* cl = command_lists_.Get(h);

    // sync2 submit: binary waits/signals from the caller (boundary submit
    // only), plus the frame timeline when timeline_signal_value > 0.
    std::vector<VkSemaphoreSubmitInfo> waits;
    waits.reserve(sync.wait.size);
    for (uint64_t i = 0; i < sync.wait.size; ++i) {
        VkSemaphoreSubmitInfo wi{};
        wi.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        wi.semaphore = UnwrapSemaphore(sync.wait.data[i]);
        wi.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        waits.push_back(wi);
    }

    std::vector<VkSemaphoreSubmitInfo> signals;
    signals.reserve(sync.signal.size + 1);
    for (uint64_t i = 0; i < sync.signal.size; ++i) {
        VkSemaphoreSubmitInfo si{};
        si.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        si.semaphore = UnwrapSemaphore(sync.signal.data[i]);
        si.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(si);
    }
    if (sync.timeline_signal_value > 0) {
        VkSemaphoreSubmitInfo ti{};
        ti.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        ti.semaphore = frame_timeline_;
        ti.value     = sync.timeline_signal_value;
        ti.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(ti);
    }

    VkCommandBufferSubmitInfo cbi{};
    cbi.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbi.commandBuffer = cl->cmd;

    VkSubmitInfo2 si2{};
    si2.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si2.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
    si2.pWaitSemaphoreInfos      = waits.data();
    si2.commandBufferInfoCount   = 1;
    si2.pCommandBufferInfos      = &cbi;
    si2.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
    si2.pSignalSemaphoreInfos    = signals.data();

    PE_VK_CHECK(vkQueueSubmit2(graphics_queue_, 1, &si2, VK_NULL_HANDLE));

    if (sync.timeline_signal_value > 0) {
        submitted_timeline_value_ = std::max(submitted_timeline_value_, sync.timeline_signal_value);

        // G8 instrument: frames submitted but not yet signaled on the timeline.
        uint64_t completed = 0;
        vkGetSemaphoreCounterValue(device_, frame_timeline_, &completed);
        const uint64_t outstanding =
            sync.timeline_signal_value > completed ? sync.timeline_signal_value - completed : 0;
        if (outstanding > peak_frames_in_flight_) {
            peak_frames_in_flight_ = outstanding;
            ENGINE_LOG_INFO(LogVulkanRHI, "[frames_in_flight] peak={}", peak_frames_in_flight_);
        }
    }
    return EngineResult::Ok();
}

// ----- Swapchain frame loop -----

RHIAcquiredImage FVulkanDevice::AcquireNextSwapchainImage(RHISwapchainHandle h) {
    auto* p = swapchains_.Get(h);

    // Frame pacing (ADR-0020): the frame about to record will signal
    // V = submitted+1 and reuses the slot last used by frame V - MFIF. Wait
    // until the timeline reaches that value - never vkDeviceWaitIdle.
    const uint64_t next_value = CurrentFrameValue();
    if (next_value > kMaxFramesInFlight) {
        const uint64_t wait_value = next_value - kMaxFramesInFlight;
        VkSemaphoreWaitInfo wi{};
        wi.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.semaphoreCount = 1;
        wi.pSemaphores    = &frame_timeline_;
        wi.pValues        = &wait_value;
        PE_VK_CHECK(vkWaitSemaphores(device_, &wi, UINT64_MAX));
    }

    // Reclaim deferred deletes the GPU has provably passed.
    uint64_t completed = 0;
    vkGetSemaphoreCounterValue(device_, frame_timeline_, &completed);
    DrainDeferred(completed);

    // G8 instrument: frames in flight = the frame the CPU is now starting
    // (value V) minus what the GPU has signaled. V - completed == 2 means the
    // CPU records frame V while frame V-1 still runs on the GPU.
    const uint64_t in_flight = next_value > completed ? next_value - completed : 0;
    if (in_flight > peak_frames_in_flight_) {
        peak_frames_in_flight_ = in_flight;
        ENGINE_LOG_INFO(LogVulkanRHI, "[frames_in_flight] peak={}", peak_frames_in_flight_);
    }

    // Enter the frame slot: bulk-reset its command pool for re-recording.
    current_slot_ = static_cast<uint32_t>(next_value % kMaxFramesInFlight);
    FFrameSlot& slot = frames_[current_slot_];
    PE_VK_CHECK(vkResetCommandPool(device_, slot.pool, 0));
    slot.used = 0;

    RHIAcquiredImage out{};
    VkSemaphore image_available = p->image_available_per_slot[current_slot_];

    uint32_t image_index = 0;
    const VkResult r = vkAcquireNextImageKHR(device_, p->swapchain, UINT64_MAX,
                                             image_available, VK_NULL_HANDLE, &image_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        out.needs_recreate = true;
        return out;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkAcquireNextImageKHR failed: VkResult {}",
                         static_cast<int>(r));
        ENGINE_FATAL("Swapchain acquire failed; see log for details");
    }

    out.image_index     = image_index;
    out.image_available = WrapSemaphore(image_available);
    return out;
}

RHISemaphore FVulkanDevice::GetRenderFinishedSemaphore(RHISwapchainHandle h, uint32_t image_index) {
    auto* p = swapchains_.Get(h);
    ENGINE_CHECK(image_index < p->render_finished_per_image.size());
    return WrapSemaphore(p->render_finished_per_image[image_index]);
}

EngineResult FVulkanDevice::Present(RHISwapchainHandle h, uint32_t image_index) {
    auto* p = swapchains_.Get(h);
    ENGINE_CHECK(image_index < p->render_finished_per_image.size());
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &p->render_finished_per_image[image_index];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &p->swapchain;
    pi.pImageIndices      = &image_index;

    const VkResult r = vkQueuePresentKHR(graphics_queue_, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        // Caller reacts by recreating (§6.f); not a fatal error.
        return EngineResult::Fail(static_cast<int32_t>(r));
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkQueuePresentKHR failed: VkResult {}", static_cast<int>(r));
        return EngineResult::Fail(static_cast<int32_t>(r));
    }
    return EngineResult::Ok();
}

EngineResult FVulkanDevice::WaitIdle() {
    if (device_ == VK_NULL_HANDLE) { return EngineResult::Fail(-1); }
    vkDeviceWaitIdle(device_);
    // Idle means every submitted frame completed: reclaim everything pending.
    DrainDeferred(UINT64_MAX);
    return EngineResult::Ok();
}

}  // namespace pe::vk
