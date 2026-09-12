// The one translation unit that compiles the Vulkan Memory Allocator. Function pointers come
// from the Vulkan-HPP dispatchers at allocator creation, so no static Vulkan symbols are used.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wnullability-completeness"
#endif

#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
