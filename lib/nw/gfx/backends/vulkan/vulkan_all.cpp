#define VK_NO_PROTOTYPES

#define VOLK_IMPLEMENTATION
#include "volk.h"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"

#include "vulkan_context.cpp"
#include "vulkan_core.cpp"
#include "vulkan_native.cpp"
