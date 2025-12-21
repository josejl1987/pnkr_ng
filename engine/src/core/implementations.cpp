// Centralized single-header implementations to avoid ODR bloat.



#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#define VMA_IMPLEMENTATION
#include  <vma/vk_mem_alloc.h>


#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

// This must exist in exactly ONE translation unit.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE