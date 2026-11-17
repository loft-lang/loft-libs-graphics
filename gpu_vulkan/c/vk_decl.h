/* Copyright (c) 2026 Jurjen Stellingwerff
 * SPDX-License-Identifier: LGPL-3.0-or-later */
/* The part of Vulkan 1.3 (and of VK_KHR_surface, VK_KHR_xlib_surface, VK_KHR_swapchain,
 * VK_KHR_push_descriptor, VK_EXT_depth_clip_control and VK_EXT_line_rasterization) this backend
 * uses, declared here under Vulkan's own names: `libvulkan.so.1` is opened when the program runs,
 * so the backend builds with loft's fixed flags and no development package.
 *
 * Every struct and constant is checked against the Khronos header by c/check_decl.sh, which
 * compiles both and compares each size, each field's offset and each value.  Declare a struct
 * here one field to a line, as below, so that script reads it. */
#ifndef GV_VK_DECL_H
#define GV_VK_DECL_H
#include <stddef.h>
#include <stdint.h>

typedef uint32_t VkFlags;
typedef uint64_t VkFlags64;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef int32_t VkResult;

/* Dispatchable handles are pointers; the others are 64-bit numbers on every 64-bit target. */
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef uint64_t VkSemaphore;
typedef uint64_t VkFence;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkBuffer;
typedef uint64_t VkImage;
typedef uint64_t VkImageView;
typedef uint64_t VkShaderModule;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkSampler;
typedef uint64_t VkCommandPool;
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkSwapchainKHR;
typedef uint64_t VkPipelineCache;
typedef uint64_t VkRenderPass;
typedef uint64_t VkBufferView;

#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TIMEOUT 2
#define VK_INCOMPLETE 5
#define VK_SUBOPTIMAL_KHR 1000001003
#define VK_ERROR_OUT_OF_DATE_KHR (-1000001004)
#define VK_TRUE 1
#define VK_FALSE 0
#define VK_QUEUE_FAMILY_IGNORED (~0U)
#define VK_REMAINING_MIP_LEVELS (~0U)
#define VK_WHOLE_SIZE (~0ULL)
#define VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
#define VK_MAX_EXTENSION_NAME_SIZE 256
#define VK_MAX_MEMORY_TYPES 32
#define VK_MAX_MEMORY_HEAPS 16
#define VK_UUID_SIZE 16

/* ── structure types ── */
#define VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO 9
#define VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO 19
#define VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO 20
#define VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO 22
#define VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO 23
#define VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO 24
#define VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO 25
#define VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO 26
#define VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO 27
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 28
#define VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO 31
#define VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35
#define VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR 1000001000
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1000001001
#define VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000
#define VK_STRUCTURE_TYPE_RENDERING_INFO 1000044000
#define VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO 1000044001
#define VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO 1000044002
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES 1000044003
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 1000059000
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT 1000259000
#define VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT 1000259001
#define VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 1000314000
#define VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 1000314002
#define VK_STRUCTURE_TYPE_DEPENDENCY_INFO 1000314003
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES 1000314007
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT 1000355000
#define VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT 1000355001

/* ── enumerants and flag bits ── */
#define VK_PHYSICAL_DEVICE_TYPE_OTHER 0
#define VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1
#define VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 2
#define VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU 3
#define VK_PHYSICAL_DEVICE_TYPE_CPU 4
#define VK_QUEUE_GRAPHICS_BIT 0x1
#define VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x1
#define VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x2
#define VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x4
#define VK_BUFFER_USAGE_TRANSFER_SRC_BIT 0x1
#define VK_BUFFER_USAGE_TRANSFER_DST_BIT 0x2
#define VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 0x10
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT 0x40
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 0x80
#define VK_SHARING_MODE_EXCLUSIVE 0
#define VK_IMAGE_TYPE_2D 1
#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_IMAGE_TILING_OPTIMAL 0
#define VK_SAMPLE_COUNT_1_BIT 0x1
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 0x1
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 0x2
#define VK_IMAGE_USAGE_SAMPLED_BIT 0x4
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 0x10
#define VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 0x20
#define VK_IMAGE_ASPECT_COLOR_BIT 0x1
#define VK_IMAGE_ASPECT_DEPTH_BIT 0x2
#define VK_FORMAT_UNDEFINED 0
#define VK_FORMAT_B8G8R8A8_UNORM 44
#define VK_FORMAT_B8G8R8A8_SRGB 50
#define VK_FORMAT_R8G8B8A8_UNORM 37
#define VK_FORMAT_R32_SFLOAT 100
#define VK_FORMAT_R32G32_SFLOAT 103
#define VK_FORMAT_R32G32B32_SFLOAT 106
#define VK_FORMAT_R32G32B32A32_SFLOAT 109
#define VK_FORMAT_D32_SFLOAT 126
#define VK_COLOR_SPACE_SRGB_NONLINEAR_KHR 0
#define VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT 0x1000
#define VK_FORMAT_FEATURE_BLIT_SRC_BIT 0x400
#define VK_FORMAT_FEATURE_BLIT_DST_BIT 0x800
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_LAYOUT_GENERAL 1
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 2
#define VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 3
#define VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL 5
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 7
#define VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL 1000241000
#define VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL 1000241001
#define VK_IMAGE_LAYOUT_PRESENT_SRC_KHR 1000001002
#define VK_COMPONENT_SWIZZLE_IDENTITY 0
#define VK_FILTER_NEAREST 0
#define VK_FILTER_LINEAR 1
#define VK_SAMPLER_MIPMAP_MODE_NEAREST 0
#define VK_SAMPLER_MIPMAP_MODE_LINEAR 1
#define VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE 2
#define VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK 0
#define VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT 0x2
#define VK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 0x1
#define VK_FENCE_CREATE_SIGNALED_BIT 0x1
#define VK_PIPELINE_STAGE_ALL_COMMANDS_BIT 0x10000
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 0x400
#define VK_PIPELINE_STAGE_TRANSFER_BIT 0x1000
#define VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT 0x10000ULL
#define VK_ACCESS_2_MEMORY_READ_BIT 0x8000ULL
#define VK_ACCESS_2_MEMORY_WRITE_BIT 0x10000ULL
#define VK_ATTACHMENT_LOAD_OP_LOAD 0
#define VK_ATTACHMENT_LOAD_OP_CLEAR 1
#define VK_ATTACHMENT_STORE_OP_STORE 0
#define VK_RESOLVE_MODE_NONE 0
#define VK_SHADER_STAGE_VERTEX_BIT 0x1
#define VK_SHADER_STAGE_FRAGMENT_BIT 0x10
#define VK_VERTEX_INPUT_RATE_VERTEX 0
#define VK_VERTEX_INPUT_RATE_INSTANCE 1
#define VK_PRIMITIVE_TOPOLOGY_POINT_LIST 0
#define VK_PRIMITIVE_TOPOLOGY_LINE_LIST 1
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 3
#define VK_POLYGON_MODE_FILL 0
#define VK_CULL_MODE_NONE 0
#define VK_CULL_MODE_FRONT_BIT 0x1
#define VK_CULL_MODE_BACK_BIT 0x2
#define VK_FRONT_FACE_COUNTER_CLOCKWISE 0
#define VK_FRONT_FACE_CLOCKWISE 1
#define VK_COMPARE_OP_LESS 1
#define VK_COMPARE_OP_ALWAYS 7
#define VK_BLEND_FACTOR_ZERO 0
#define VK_BLEND_FACTOR_ONE 1
#define VK_BLEND_FACTOR_SRC_ALPHA 6
#define VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA 7
#define VK_BLEND_FACTOR_DST_ALPHA 8
#define VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA 9
#define VK_BLEND_OP_ADD 0
#define VK_COLOR_COMPONENT_R_BIT 0x1
#define VK_COLOR_COMPONENT_G_BIT 0x2
#define VK_COLOR_COMPONENT_B_BIT 0x4
#define VK_COLOR_COMPONENT_A_BIT 0x8
#define VK_DYNAMIC_STATE_VIEWPORT 0
#define VK_DYNAMIC_STATE_SCISSOR 1
#define VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 1
#define VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER 6
#define VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR 0x1
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0
#define VK_INDEX_TYPE_UINT32 1
#define VK_PRESENT_MODE_IMMEDIATE_KHR 0
#define VK_PRESENT_MODE_MAILBOX_KHR 1
#define VK_PRESENT_MODE_FIFO_KHR 2
#define VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR 0x1
#define VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR 0x1
#define VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR 0x8
#define VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT 2

/* ── structures ── */
typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkExtent3D {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} VkExtent3D;

typedef struct VkOffset2D {
    int32_t x;
    int32_t y;
} VkOffset2D;

typedef struct VkOffset3D {
    int32_t x;
    int32_t y;
    int32_t z;
} VkOffset3D;

typedef struct VkRect2D {
    VkOffset2D offset;
    VkExtent2D extent;
} VkRect2D;

typedef struct VkApplicationInfo {
    int32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkExtensionProperties {
    char extensionName[VK_MAX_EXTENSION_NAME_SIZE];
    uint32_t specVersion;
} VkExtensionProperties;

typedef struct VkPhysicalDeviceLimits {
    uint32_t maxImageDimension1D;
    uint32_t maxImageDimension2D;
    uint32_t maxImageDimension3D;
    uint32_t maxImageDimensionCube;
    uint32_t maxImageArrayLayers;
    uint32_t maxTexelBufferElements;
    uint32_t maxUniformBufferRange;
    uint32_t maxStorageBufferRange;
    uint32_t maxPushConstantsSize;
    uint32_t maxMemoryAllocationCount;
    uint32_t maxSamplerAllocationCount;
    VkDeviceSize bufferImageGranularity;
    VkDeviceSize sparseAddressSpaceSize;
    uint32_t maxBoundDescriptorSets;
    uint32_t maxPerStageDescriptorSamplers;
    uint32_t maxPerStageDescriptorUniformBuffers;
    uint32_t maxPerStageDescriptorStorageBuffers;
    uint32_t maxPerStageDescriptorSampledImages;
    uint32_t maxPerStageDescriptorStorageImages;
    uint32_t maxPerStageDescriptorInputAttachments;
    uint32_t maxPerStageResources;
    uint32_t maxDescriptorSetSamplers;
    uint32_t maxDescriptorSetUniformBuffers;
    uint32_t maxDescriptorSetUniformBuffersDynamic;
    uint32_t maxDescriptorSetStorageBuffers;
    uint32_t maxDescriptorSetStorageBuffersDynamic;
    uint32_t maxDescriptorSetSampledImages;
    uint32_t maxDescriptorSetStorageImages;
    uint32_t maxDescriptorSetInputAttachments;
    uint32_t maxVertexInputAttributes;
    uint32_t maxVertexInputBindings;
    uint32_t maxVertexInputAttributeOffset;
    uint32_t maxVertexInputBindingStride;
    uint32_t maxVertexOutputComponents;
    uint32_t maxTessellationGenerationLevel;
    uint32_t maxTessellationPatchSize;
    uint32_t maxTessellationControlPerVertexInputComponents;
    uint32_t maxTessellationControlPerVertexOutputComponents;
    uint32_t maxTessellationControlPerPatchOutputComponents;
    uint32_t maxTessellationControlTotalOutputComponents;
    uint32_t maxTessellationEvaluationInputComponents;
    uint32_t maxTessellationEvaluationOutputComponents;
    uint32_t maxGeometryShaderInvocations;
    uint32_t maxGeometryInputComponents;
    uint32_t maxGeometryOutputComponents;
    uint32_t maxGeometryOutputVertices;
    uint32_t maxGeometryTotalOutputComponents;
    uint32_t maxFragmentInputComponents;
    uint32_t maxFragmentOutputAttachments;
    uint32_t maxFragmentDualSrcAttachments;
    uint32_t maxFragmentCombinedOutputResources;
    uint32_t maxComputeSharedMemorySize;
    uint32_t maxComputeWorkGroupCount[3];
    uint32_t maxComputeWorkGroupInvocations;
    uint32_t maxComputeWorkGroupSize[3];
    uint32_t subPixelPrecisionBits;
    uint32_t subTexelPrecisionBits;
    uint32_t mipmapPrecisionBits;
    uint32_t maxDrawIndexedIndexValue;
    uint32_t maxDrawIndirectCount;
    float maxSamplerLodBias;
    float maxSamplerAnisotropy;
    uint32_t maxViewports;
    uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    uint32_t viewportSubPixelBits;
    size_t minMemoryMapAlignment;
    VkDeviceSize minTexelBufferOffsetAlignment;
    VkDeviceSize minUniformBufferOffsetAlignment;
    VkDeviceSize minStorageBufferOffsetAlignment;
    int32_t minTexelOffset;
    uint32_t maxTexelOffset;
    int32_t minTexelGatherOffset;
    uint32_t maxTexelGatherOffset;
    float minInterpolationOffset;
    float maxInterpolationOffset;
    uint32_t subPixelInterpolationOffsetBits;
    uint32_t maxFramebufferWidth;
    uint32_t maxFramebufferHeight;
    uint32_t maxFramebufferLayers;
    VkFlags framebufferColorSampleCounts;
    VkFlags framebufferDepthSampleCounts;
    VkFlags framebufferStencilSampleCounts;
    VkFlags framebufferNoAttachmentsSampleCounts;
    uint32_t maxColorAttachments;
    VkFlags sampledImageColorSampleCounts;
    VkFlags sampledImageIntegerSampleCounts;
    VkFlags sampledImageDepthSampleCounts;
    VkFlags sampledImageStencilSampleCounts;
    VkFlags storageImageSampleCounts;
    uint32_t maxSampleMaskWords;
    VkBool32 timestampComputeAndGraphics;
    float timestampPeriod;
    uint32_t maxClipDistances;
    uint32_t maxCullDistances;
    uint32_t maxCombinedClipAndCullDistances;
    uint32_t discreteQueuePriorities;
    float pointSizeRange[2];
    float lineWidthRange[2];
    float pointSizeGranularity;
    float lineWidthGranularity;
    VkBool32 strictLines;
    VkBool32 standardSampleLocations;
    VkDeviceSize optimalBufferCopyOffsetAlignment;
    VkDeviceSize optimalBufferCopyRowPitchAlignment;
    VkDeviceSize nonCoherentAtomSize;
} VkPhysicalDeviceLimits;

typedef struct VkPhysicalDeviceSparseProperties {
    VkBool32 residencyStandard2DBlockShape;
    VkBool32 residencyStandard2DMultisampleBlockShape;
    VkBool32 residencyStandard3DBlockShape;
    VkBool32 residencyAlignedMipSize;
    VkBool32 residencyNonResidentStrict;
} VkPhysicalDeviceSparseProperties;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    int32_t deviceType;
    char deviceName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
} VkPhysicalDeviceProperties;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkMemoryType {
    VkFlags propertyFlags;
    uint32_t heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[VK_MAX_MEMORY_TYPES];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[VK_MAX_MEMORY_HEAPS];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkFormatProperties {
    VkFlags linearTilingFeatures;
    VkFlags optimalTilingFeatures;
    VkFlags bufferFeatures;
} VkFormatProperties;

/* The 55 VkBool32 of Vulkan 1.0's features: this backend asks for none of them by name. */
typedef struct VkPhysicalDeviceFeatures2 {
    int32_t sType;
    void *pNext;
    VkBool32 features[55];
} VkPhysicalDeviceFeatures2;

typedef struct VkPhysicalDeviceDynamicRenderingFeatures {
    int32_t sType;
    void *pNext;
    VkBool32 dynamicRendering;
} VkPhysicalDeviceDynamicRenderingFeatures;

typedef struct VkPhysicalDeviceSynchronization2Features {
    int32_t sType;
    void *pNext;
    VkBool32 synchronization2;
} VkPhysicalDeviceSynchronization2Features;

typedef struct VkPhysicalDeviceDepthClipControlFeaturesEXT {
    int32_t sType;
    void *pNext;
    VkBool32 depthClipControl;
} VkPhysicalDeviceDepthClipControlFeaturesEXT;

typedef struct VkPhysicalDeviceLineRasterizationFeaturesEXT {
    int32_t sType;
    void *pNext;
    VkBool32 rectangularLines;
    VkBool32 bresenhamLines;
    VkBool32 smoothLines;
    VkBool32 stippledRectangularLines;
    VkBool32 stippledBresenhamLines;
    VkBool32 stippledSmoothLines;
} VkPhysicalDeviceLineRasterizationFeaturesEXT;

typedef struct VkDeviceQueueCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkCommandPoolCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    int32_t sType;
    const void *pNext;
    VkCommandPool commandPool;
    int32_t level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    const void *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkFenceCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
} VkFenceCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
} VkSemaphoreCreateInfo;

typedef struct VkSubmitInfo {
    int32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    const VkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkBufferCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkDeviceSize size;
    VkFlags usage;
    int32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkMemoryAllocateInfo {
    int32_t sType;
    const void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkImageCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    int32_t imageType;
    int32_t format;
    VkExtent3D extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    VkFlags samples;
    int32_t tiling;
    VkFlags usage;
    int32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    int32_t initialLayout;
} VkImageCreateInfo;

typedef struct VkComponentMapping {
    int32_t r;
    int32_t g;
    int32_t b;
    int32_t a;
} VkComponentMapping;

typedef struct VkImageSubresourceRange {
    VkFlags aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkImageSubresourceLayers {
    VkFlags aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceLayers;

typedef struct VkImageViewCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkImage image;
    int32_t viewType;
    int32_t format;
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkSamplerCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    int32_t magFilter;
    int32_t minFilter;
    int32_t mipmapMode;
    int32_t addressModeU;
    int32_t addressModeV;
    int32_t addressModeW;
    float mipLodBias;
    VkBool32 anisotropyEnable;
    float maxAnisotropy;
    VkBool32 compareEnable;
    int32_t compareOp;
    float minLod;
    float maxLod;
    int32_t borderColor;
    VkBool32 unnormalizedCoordinates;
} VkSamplerCreateInfo;

typedef struct VkMemoryBarrier2 {
    int32_t sType;
    const void *pNext;
    VkFlags64 srcStageMask;
    VkFlags64 srcAccessMask;
    VkFlags64 dstStageMask;
    VkFlags64 dstAccessMask;
} VkMemoryBarrier2;

typedef struct VkImageMemoryBarrier2 {
    int32_t sType;
    const void *pNext;
    VkFlags64 srcStageMask;
    VkFlags64 srcAccessMask;
    VkFlags64 dstStageMask;
    VkFlags64 dstAccessMask;
    int32_t oldLayout;
    int32_t newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkImage image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier2;

typedef struct VkDependencyInfo {
    int32_t sType;
    const void *pNext;
    VkFlags dependencyFlags;
    uint32_t memoryBarrierCount;
    const VkMemoryBarrier2 *pMemoryBarriers;
    uint32_t bufferMemoryBarrierCount;
    const void *pBufferMemoryBarriers;
    uint32_t imageMemoryBarrierCount;
    const VkImageMemoryBarrier2 *pImageMemoryBarriers;
} VkDependencyInfo;

typedef struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
} VkBufferCopy;

typedef struct VkBufferImageCopy {
    VkDeviceSize bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
} VkBufferImageCopy;

typedef struct VkImageBlit {
    VkImageSubresourceLayers srcSubresource;
    VkOffset3D srcOffsets[2];
    VkImageSubresourceLayers dstSubresource;
    VkOffset3D dstOffsets[2];
} VkImageBlit;

typedef union VkClearColorValue {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkClearDepthStencilValue {
    float depth;
    uint32_t stencil;
} VkClearDepthStencilValue;

typedef union VkClearValue {
    VkClearColorValue color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;

typedef struct VkRenderingAttachmentInfo {
    int32_t sType;
    const void *pNext;
    VkImageView imageView;
    int32_t imageLayout;
    int32_t resolveMode;
    VkImageView resolveImageView;
    int32_t resolveImageLayout;
    int32_t loadOp;
    int32_t storeOp;
    VkClearValue clearValue;
} VkRenderingAttachmentInfo;

typedef struct VkRenderingInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkRect2D renderArea;
    uint32_t layerCount;
    uint32_t viewMask;
    uint32_t colorAttachmentCount;
    const VkRenderingAttachmentInfo *pColorAttachments;
    const VkRenderingAttachmentInfo *pDepthAttachment;
    const VkRenderingAttachmentInfo *pStencilAttachment;
} VkRenderingInfo;

typedef struct VkPipelineRenderingCreateInfo {
    int32_t sType;
    const void *pNext;
    uint32_t viewMask;
    uint32_t colorAttachmentCount;
    const int32_t *pColorAttachmentFormats;
    int32_t depthAttachmentFormat;
    int32_t stencilAttachmentFormat;
} VkPipelineRenderingCreateInfo;

typedef struct VkShaderModuleCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    size_t codeSize;
    const uint32_t *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkPipelineShaderStageCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkFlags stage;
    VkShaderModule module;
    const char *pName;
    const void *pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;

typedef struct VkVertexInputBindingDescription {
    uint32_t binding;
    uint32_t stride;
    int32_t inputRate;
} VkVertexInputBindingDescription;

typedef struct VkVertexInputAttributeDescription {
    uint32_t location;
    uint32_t binding;
    int32_t format;
    uint32_t offset;
} VkVertexInputAttributeDescription;

typedef struct VkPipelineVertexInputStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription *pVertexBindingDescriptions;
    uint32_t vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription *pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;

typedef struct VkPipelineInputAssemblyStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    int32_t topology;
    VkBool32 primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;

typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

typedef struct VkPipelineViewportStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t viewportCount;
    const VkViewport *pViewports;
    uint32_t scissorCount;
    const VkRect2D *pScissors;
} VkPipelineViewportStateCreateInfo;

typedef struct VkPipelineViewportDepthClipControlCreateInfoEXT {
    int32_t sType;
    const void *pNext;
    VkBool32 negativeOneToOne;
} VkPipelineViewportDepthClipControlCreateInfoEXT;

typedef struct VkPipelineRasterizationStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 depthClampEnable;
    VkBool32 rasterizerDiscardEnable;
    int32_t polygonMode;
    VkFlags cullMode;
    int32_t frontFace;
    VkBool32 depthBiasEnable;
    float depthBiasConstantFactor;
    float depthBiasClamp;
    float depthBiasSlopeFactor;
    float lineWidth;
} VkPipelineRasterizationStateCreateInfo;

typedef struct VkPipelineRasterizationLineStateCreateInfoEXT {
    int32_t sType;
    const void *pNext;
    int32_t lineRasterizationMode;
    VkBool32 stippledLineEnable;
    uint32_t lineStippleFactor;
    uint16_t lineStipplePattern;
} VkPipelineRasterizationLineStateCreateInfoEXT;

typedef struct VkPipelineMultisampleStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkFlags rasterizationSamples;
    VkBool32 sampleShadingEnable;
    float minSampleShading;
    const uint32_t *pSampleMask;
    VkBool32 alphaToCoverageEnable;
    VkBool32 alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;

typedef struct VkStencilOpState {
    int32_t failOp;
    int32_t passOp;
    int32_t depthFailOp;
    int32_t compareOp;
    uint32_t compareMask;
    uint32_t writeMask;
    uint32_t reference;
} VkStencilOpState;

typedef struct VkPipelineDepthStencilStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 depthTestEnable;
    VkBool32 depthWriteEnable;
    int32_t depthCompareOp;
    VkBool32 depthBoundsTestEnable;
    VkBool32 stencilTestEnable;
    VkStencilOpState front;
    VkStencilOpState back;
    float minDepthBounds;
    float maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;

typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32 blendEnable;
    int32_t srcColorBlendFactor;
    int32_t dstColorBlendFactor;
    int32_t colorBlendOp;
    int32_t srcAlphaBlendFactor;
    int32_t dstAlphaBlendFactor;
    int32_t alphaBlendOp;
    VkFlags colorWriteMask;
} VkPipelineColorBlendAttachmentState;

typedef struct VkPipelineColorBlendStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkBool32 logicOpEnable;
    int32_t logicOp;
    uint32_t attachmentCount;
    const VkPipelineColorBlendAttachmentState *pAttachments;
    float blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;

typedef struct VkPipelineDynamicStateCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t dynamicStateCount;
    const int32_t *pDynamicStates;
} VkPipelineDynamicStateCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t stageCount;
    const VkPipelineShaderStageCreateInfo *pStages;
    const VkPipelineVertexInputStateCreateInfo *pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo *pInputAssemblyState;
    const void *pTessellationState;
    const VkPipelineViewportStateCreateInfo *pViewportState;
    const VkPipelineRasterizationStateCreateInfo *pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo *pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo *pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo *pColorBlendState;
    const VkPipelineDynamicStateCreateInfo *pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkDescriptorSetLayoutBinding {
    uint32_t binding;
    int32_t descriptorType;
    uint32_t descriptorCount;
    VkFlags stageFlags;
    const VkSampler *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkPipelineLayoutCreateInfo {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t setLayoutCount;
    const VkDescriptorSetLayout *pSetLayouts;
    uint32_t pushConstantRangeCount;
    const void *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkDescriptorImageInfo {
    VkSampler sampler;
    VkImageView imageView;
    int32_t imageLayout;
} VkDescriptorImageInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkDescriptorBufferInfo;

typedef struct VkWriteDescriptorSet {
    int32_t sType;
    const void *pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    int32_t descriptorType;
    const VkDescriptorImageInfo *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
    const VkBufferView *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkXlibSurfaceCreateInfoKHR {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    void *dpy;
    unsigned long window;
} VkXlibSurfaceCreateInfoKHR;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    int32_t currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    int32_t format;
    int32_t colorSpace;
} VkSurfaceFormatKHR;

typedef struct VkSwapchainCreateInfoKHR {
    int32_t sType;
    const void *pNext;
    VkFlags flags;
    VkSurfaceKHR surface;
    uint32_t minImageCount;
    int32_t imageFormat;
    int32_t imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkFlags imageUsage;
    int32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    int32_t preTransform;
    int32_t compositeAlpha;
    int32_t presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkPresentInfoKHR {
    int32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    VkResult *pResults;
} VkPresentInfoKHR;

/* ── entry points: one table per device, filled through vkGetInstanceProcAddr ── */
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char *);

#define GV_INSTANCE_FUNCTIONS(F)                                                                  \
    F(VkResult, vkEnumeratePhysicalDevices, (VkInstance, uint32_t *, VkPhysicalDevice *))         \
    F(void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice, VkPhysicalDeviceProperties *))      \
    F(void, vkGetPhysicalDeviceQueueFamilyProperties,                                             \
      (VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *))                                  \
    F(void, vkGetPhysicalDeviceMemoryProperties,                                                  \
      (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *))                                     \
    F(void, vkGetPhysicalDeviceFormatProperties, (VkPhysicalDevice, int32_t, VkFormatProperties *)) \
    F(void, vkGetPhysicalDeviceFeatures2, (VkPhysicalDevice, VkPhysicalDeviceFeatures2 *))        \
    F(VkResult, vkEnumerateDeviceExtensionProperties,                                             \
      (VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *))                      \
    F(VkResult, vkCreateDevice,                                                                   \
      (VkPhysicalDevice, const VkDeviceCreateInfo *, const void *, VkDevice *))                   \
    F(PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice, const char *))                          \
    F(void, vkDestroyInstance, (VkInstance, const void *))                                        \
    F(VkResult, vkCreateXlibSurfaceKHR,                                                           \
      (VkInstance, const VkXlibSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *))             \
    F(void, vkDestroySurfaceKHR, (VkInstance, VkSurfaceKHR, const void *))                        \
    F(VkResult, vkGetPhysicalDeviceSurfaceSupportKHR,                                             \
      (VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *))                                     \
    F(VkResult, vkGetPhysicalDeviceSurfaceCapabilitiesKHR,                                        \
      (VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *))                               \
    F(VkResult, vkGetPhysicalDeviceSurfaceFormatsKHR,                                             \
      (VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *))                         \
    F(VkResult, vkGetPhysicalDeviceSurfacePresentModesKHR,                                        \
      (VkPhysicalDevice, VkSurfaceKHR, uint32_t *, int32_t *))

#define GV_DEVICE_FUNCTIONS(F)                                                                    \
    F(void, vkDestroyDevice, (VkDevice, const void *))                                            \
    F(void, vkGetDeviceQueue, (VkDevice, uint32_t, uint32_t, VkQueue *))                          \
    F(VkResult, vkDeviceWaitIdle, (VkDevice))                                                     \
    F(VkResult, vkQueueSubmit, (VkQueue, uint32_t, const VkSubmitInfo *, VkFence))                \
    F(VkResult, vkCreateCommandPool,                                                              \
      (VkDevice, const VkCommandPoolCreateInfo *, const void *, VkCommandPool *))                 \
    F(void, vkDestroyCommandPool, (VkDevice, VkCommandPool, const void *))                        \
    F(VkResult, vkAllocateCommandBuffers,                                                         \
      (VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *))                         \
    F(VkResult, vkBeginCommandBuffer, (VkCommandBuffer, const VkCommandBufferBeginInfo *))        \
    F(VkResult, vkEndCommandBuffer, (VkCommandBuffer))                                            \
    F(VkResult, vkResetCommandBuffer, (VkCommandBuffer, VkFlags))                                 \
    F(VkResult, vkCreateFence, (VkDevice, const VkFenceCreateInfo *, const void *, VkFence *))    \
    F(void, vkDestroyFence, (VkDevice, VkFence, const void *))                                    \
    F(VkResult, vkWaitForFences, (VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t))       \
    F(VkResult, vkResetFences, (VkDevice, uint32_t, const VkFence *))                             \
    F(VkResult, vkCreateSemaphore,                                                                \
      (VkDevice, const VkSemaphoreCreateInfo *, const void *, VkSemaphore *))                     \
    F(void, vkDestroySemaphore, (VkDevice, VkSemaphore, const void *))                            \
    F(VkResult, vkCreateBuffer, (VkDevice, const VkBufferCreateInfo *, const void *, VkBuffer *)) \
    F(void, vkDestroyBuffer, (VkDevice, VkBuffer, const void *))                                  \
    F(void, vkGetBufferMemoryRequirements, (VkDevice, VkBuffer, VkMemoryRequirements *))          \
    F(VkResult, vkBindBufferMemory, (VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize))           \
    F(VkResult, vkCreateImage, (VkDevice, const VkImageCreateInfo *, const void *, VkImage *))    \
    F(void, vkDestroyImage, (VkDevice, VkImage, const void *))                                    \
    F(void, vkGetImageMemoryRequirements, (VkDevice, VkImage, VkMemoryRequirements *))            \
    F(VkResult, vkBindImageMemory, (VkDevice, VkImage, VkDeviceMemory, VkDeviceSize))             \
    F(VkResult, vkAllocateMemory,                                                                 \
      (VkDevice, const VkMemoryAllocateInfo *, const void *, VkDeviceMemory *))                   \
    F(void, vkFreeMemory, (VkDevice, VkDeviceMemory, const void *))                               \
    F(VkResult, vkMapMemory, (VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags,      \
                              void **))                                                           \
    F(VkResult, vkCreateImageView,                                                                \
      (VkDevice, const VkImageViewCreateInfo *, const void *, VkImageView *))                     \
    F(void, vkDestroyImageView, (VkDevice, VkImageView, const void *))                            \
    F(VkResult, vkCreateSampler, (VkDevice, const VkSamplerCreateInfo *, const void *, VkSampler *)) \
    F(void, vkDestroySampler, (VkDevice, VkSampler, const void *))                                \
    F(VkResult, vkCreateShaderModule,                                                             \
      (VkDevice, const VkShaderModuleCreateInfo *, const void *, VkShaderModule *))               \
    F(void, vkDestroyShaderModule, (VkDevice, VkShaderModule, const void *))                      \
    F(VkResult, vkCreateDescriptorSetLayout,                                                      \
      (VkDevice, const VkDescriptorSetLayoutCreateInfo *, const void *, VkDescriptorSetLayout *)) \
    F(void, vkDestroyDescriptorSetLayout, (VkDevice, VkDescriptorSetLayout, const void *))        \
    F(VkResult, vkCreatePipelineLayout,                                                           \
      (VkDevice, const VkPipelineLayoutCreateInfo *, const void *, VkPipelineLayout *))           \
    F(void, vkDestroyPipelineLayout, (VkDevice, VkPipelineLayout, const void *))                  \
    F(VkResult, vkCreateGraphicsPipelines,                                                        \
      (VkDevice, VkPipelineCache, uint32_t, const VkGraphicsPipelineCreateInfo *, const void *,   \
       VkPipeline *))                                                                             \
    F(void, vkDestroyPipeline, (VkDevice, VkPipeline, const void *))                              \
    F(void, vkCmdPipelineBarrier2, (VkCommandBuffer, const VkDependencyInfo *))                   \
    F(void, vkCmdCopyBuffer, (VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *)) \
    F(void, vkCmdCopyBufferToImage, (VkCommandBuffer, VkBuffer, VkImage, int32_t, uint32_t,       \
                                     const VkBufferImageCopy *))                                  \
    F(void, vkCmdCopyImageToBuffer, (VkCommandBuffer, VkImage, int32_t, VkBuffer, uint32_t,       \
                                     const VkBufferImageCopy *))                                  \
    F(void, vkCmdBlitImage, (VkCommandBuffer, VkImage, int32_t, VkImage, int32_t, uint32_t,       \
                             const VkImageBlit *, int32_t))                                       \
    F(void, vkCmdClearColorImage, (VkCommandBuffer, VkImage, int32_t, const VkClearColorValue *,  \
                                   uint32_t, const VkImageSubresourceRange *))                    \
    F(void, vkCmdClearDepthStencilImage, (VkCommandBuffer, VkImage, int32_t,                      \
                                          const VkClearDepthStencilValue *, uint32_t,             \
                                          const VkImageSubresourceRange *))                       \
    F(void, vkCmdBeginRendering, (VkCommandBuffer, const VkRenderingInfo *))                      \
    F(void, vkCmdEndRendering, (VkCommandBuffer))                                                 \
    F(void, vkCmdBindPipeline, (VkCommandBuffer, int32_t, VkPipeline))                            \
    F(void, vkCmdSetViewport, (VkCommandBuffer, uint32_t, uint32_t, const VkViewport *))          \
    F(void, vkCmdSetScissor, (VkCommandBuffer, uint32_t, uint32_t, const VkRect2D *))             \
    F(void, vkCmdBindVertexBuffers, (VkCommandBuffer, uint32_t, uint32_t, const VkBuffer *,       \
                                     const VkDeviceSize *))                                       \
    F(void, vkCmdBindIndexBuffer, (VkCommandBuffer, VkBuffer, VkDeviceSize, int32_t))             \
    F(void, vkCmdPushDescriptorSetKHR, (VkCommandBuffer, int32_t, VkPipelineLayout, uint32_t,     \
                                        uint32_t, const VkWriteDescriptorSet *))                  \
    F(void, vkCmdDraw, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t))                 \
    F(void, vkCmdDrawIndexed, (VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)) \
    F(VkResult, vkCreateSwapchainKHR,                                                             \
      (VkDevice, const VkSwapchainCreateInfoKHR *, const void *, VkSwapchainKHR *))               \
    F(void, vkDestroySwapchainKHR, (VkDevice, VkSwapchainKHR, const void *))                      \
    F(VkResult, vkGetSwapchainImagesKHR, (VkDevice, VkSwapchainKHR, uint32_t *, VkImage *))       \
    F(VkResult, vkAcquireNextImageKHR,                                                            \
      (VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *))                     \
    F(VkResult, vkQueuePresentKHR, (VkQueue, const VkPresentInfoKHR *))

#endif
