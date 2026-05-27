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
        default:                            return VK_FORMAT_UNDEFINED;
    }
}

// Stage 1: present mode is hardcoded to FIFO (Architecture.md §3.6 policy);
// MAILBOX/Immediate arrive with the Stage 2 swapchain rework.
// Shader stage conversion is inlined in CreateGraphicsPipeline (two stages only).

}  // namespace

// ----- Lifecycle -----

FVulkanDevice::FVulkanDevice(const RHIDeviceCreateDesc& desc, bool& out_failed) {
    out_failed = true;
    validation_enabled_      = desc.enable_validation;
    create_surface_          = desc.create_surface;
    create_surface_userdata_ = desc.create_surface_userdata;

    if (!CreateInstance(desc))    { return; }
    if (!CreateDebugMessenger())  { return; }
    if (!SelectPhysicalDevice())  { return; }
    if (!CreateLogicalDevice())   { return; }
    if (!CreateCommandPool())     { return; }

    out_failed = false;
    ENGINE_LOG_INFO(LogVulkanRHI, "FVulkanDevice ready (queue family {})", graphics_queue_family_);
}

FVulkanDevice::~FVulkanDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        // Drain pools (most resources should already have been Destroy()-ed by caller).
        command_lists_.ForEachLive([this](VulkanCommandListPayload& p) { DestroyCommandListPayload(p); });
        swapchains_.ForEachLive   ([this](VulkanSwapchainPayload& p)   { DestroySwapchainPayload(p); });
        pipelines_.ForEachLive    ([this](VulkanPipelinePayload& p)    { DestroyPipelinePayload(p); });
        shaders_.ForEachLive      ([this](VulkanShaderPayload& p)      { DestroyShaderPayload(p); });
        buffers_.ForEachLive      ([this](VulkanBufferPayload& p)      { DestroyBufferPayload(p); });

        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
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

        VkPhysicalDeviceVulkan13Features feat13{};
        feat13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 feat2{};
        feat2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        feat2.pNext = &feat13;
        vkGetPhysicalDeviceFeatures2(candidate, &feat2);
        if (feat13.dynamicRendering == VK_FALSE || feat13.synchronization2 == VK_FALSE) {
            ENGINE_LOG_INFO(LogVulkanRHI, "Skipping '{}' - dynamicRendering/synchronization2 unsupported",
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

    VkPhysicalDeviceVulkan13Features feat13{};
    feat13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
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

bool FVulkanDevice::CreateCommandPool() {
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = graphics_queue_family_;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    const VkResult r = vkCreateCommandPool(device_, &info, nullptr, &command_pool_);
    if (r != VK_SUCCESS) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkCreateCommandPool failed: VkResult {}", static_cast<int>(r));
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
    if (desc.size_bytes == 0) { return {0}; }

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
        return {0};
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
        return {0};
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

    VkFormat color_fmt = ToVkFormat(desc.color_attachment_format);
    VkPipelineRenderingCreateInfo render_info{};
    render_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    render_info.colorAttachmentCount    = 1;
    render_info.pColorAttachmentFormats = &color_fmt;

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
    gpci.pColorBlendState    = &blend;
    gpci.pDynamicState       = &dyn;
    gpci.layout              = payload.layout;

    PE_VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                          &payload.pipeline));

    return pipelines_.Insert(std::move(payload));
}

RHISwapchainHandle FVulkanDevice::CreateSwapchain(const RHISwapchainDesc& desc) {
    if (create_surface_ == nullptr) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "CreateSwapchain called but create_surface callback is null");
        return {0};
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
        return {0};
    }
    payload.surface = static_cast<VkSurfaceKHR>(surface_opaque);

    // 2. Verify present support for our graphics queue.
    VkBool32 present_supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, graphics_queue_family_,
                                         payload.surface, &present_supported);
    if (present_supported == VK_FALSE) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "Graphics queue family does not support present on this surface");
        vkDestroySurfaceKHR(instance_, payload.surface, nullptr);
        return {0};
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
        return {0};
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

    // 5. Sync objects. Stage 1 has 1 frame in flight.
    //   - image_available: 1 instance (consumed by submit before reuse, guarded by frame_done fence)
    //   - render_finished_per_image: N instances, indexed by acquired image to avoid
    //     racing with the presentation engine's hold of a previous image's semaphore.
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    PE_VK_CHECK(vkCreateSemaphore(device_, &sem_ci, nullptr, &payload.image_available));

    payload.render_finished_per_image.resize(got_count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < got_count; ++i) {
        PE_VK_CHECK(vkCreateSemaphore(device_, &sem_ci, nullptr, &payload.render_finished_per_image[i]));
    }

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // first AcquireNextSwapchainImage waits then resets
    PE_VK_CHECK(vkCreateFence(device_, &fci, nullptr, &payload.frame_done));

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

void FVulkanDevice::DestroySwapchainPayload(VulkanSwapchainPayload& p) {
    if (p.frame_done != VK_NULL_HANDLE) {
        vkDestroyFence(device_, p.frame_done, nullptr); p.frame_done = VK_NULL_HANDLE;
    }
    for (VkSemaphore s : p.render_finished_per_image) {
        if (s != VK_NULL_HANDLE) { vkDestroySemaphore(device_, s, nullptr); }
    }
    p.render_finished_per_image.clear();
    if (p.image_available != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, p.image_available, nullptr); p.image_available = VK_NULL_HANDLE;
    }
    for (VkImageView v : p.image_views) { if (v != VK_NULL_HANDLE) { vkDestroyImageView(device_, v, nullptr); } }
    p.image_views.clear();
    p.images.clear();
    if (p.swapchain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(device_, p.swapchain, nullptr); p.swapchain = VK_NULL_HANDLE; }
    if (p.surface != VK_NULL_HANDLE)   { vkDestroySurfaceKHR(instance_, p.surface, nullptr); p.surface = VK_NULL_HANDLE; }
}

void FVulkanDevice::DestroyCommandListPayload(VulkanCommandListPayload& p) {
    if (p.cmd != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device_, command_pool_, 1, &p.cmd);
        p.cmd = VK_NULL_HANDLE;
    }
    if (p.wrapper != nullptr) {
        delete p.wrapper;
        p.wrapper = nullptr;
    }
}

void FVulkanDevice::Destroy(RHIBufferHandle h)    { auto p = buffers_.Remove(h);    DestroyBufferPayload(p); }
void FVulkanDevice::Destroy(RHIShaderHandle h)    { auto p = shaders_.Remove(h);    DestroyShaderPayload(p); }
void FVulkanDevice::Destroy(RHIPipelineHandle h)  { auto p = pipelines_.Remove(h);  DestroyPipelinePayload(p); }
void FVulkanDevice::Destroy(RHISwapchainHandle h) { auto p = swapchains_.Remove(h); DestroySwapchainPayload(p); }

// ----- Command lists -----

RHICommandListHandle FVulkanDevice::AcquireCommandList() {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = command_pool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VulkanCommandListPayload payload;
    PE_VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &payload.cmd));
    payload.wrapper = new FVulkanCommandList(*this, payload.cmd);
    return command_lists_.Insert(std::move(payload));
}

IRHICommandList* FVulkanDevice::Lock(RHICommandListHandle h) {
    return command_lists_.Get(h)->wrapper;
}

EngineResult FVulkanDevice::Submit(RHICommandListHandle h) {
    auto* cl = command_lists_.Get(h);
    const RHISwapchainHandle sc_handle  = cl->wrapper->BoundSwapchain();
    const uint32_t           image_idx  = cl->wrapper->BoundImageIndex();
    if (!sc_handle.valid()) {
        ENGINE_LOG_ERROR(LogVulkanRHI,
            "Submit: command list has no bound swapchain (call TransitionToRenderTarget first)");
        return EngineResult::Fail(-1);
    }
    auto* sc_payload = swapchains_.Get(sc_handle);
    ENGINE_CHECK(image_idx < sc_payload->render_finished_per_image.size());

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &sc_payload->image_available;
    si.pWaitDstStageMask    = &wait_stage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cl->cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &sc_payload->render_finished_per_image[image_idx];

    PE_VK_CHECK(vkQueueSubmit(graphics_queue_, 1, &si, sc_payload->frame_done));
    return EngineResult::Ok();
}

uint32_t FVulkanDevice::AcquireNextSwapchainImage(RHISwapchainHandle h) {
    auto* p = swapchains_.Get(h);
    // Stage 1 single frame-in-flight: wait for the previous frame's submit to complete
    // before reusing the sync objects.
    vkWaitForFences(device_, 1, &p->frame_done, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &p->frame_done);

    uint32_t image_index = 0;
    PE_VK_CHECK(vkAcquireNextImageKHR(device_, p->swapchain, UINT64_MAX,
                                      p->image_available, VK_NULL_HANDLE, &image_index));
    p->current_image_index = image_index;
    return image_index;
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
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        ENGINE_LOG_ERROR(LogVulkanRHI, "vkQueuePresentKHR failed: VkResult {}", static_cast<int>(r));
        return EngineResult::Fail(static_cast<int32_t>(r));
    }
    return EngineResult::Ok();
}

EngineResult FVulkanDevice::WaitIdle() {
    if (device_ == VK_NULL_HANDLE) { return EngineResult::Fail(-1); }
    vkDeviceWaitIdle(device_);
    return EngineResult::Ok();
}

}  // namespace pe::vk
