#pragma once
#include "VkBootstrap.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vma/vk_mem_alloc.h"
#include <vector>
#include <functional>
#include <deque>

#define VK_CHECK(func)                                                                                                 \
    {                                                                                                                  \
        const VkResult result = func;                                                                                  \
        if (result != VK_SUCCESS)                                                                                      \
        {                                                                                                              \
            std::cerr << "Error calling function " << #func << "at " << __FILE__ << ":" << __LINE__ << ". Result is "  \
                      << string_VkResult(result) << std::endl;                                                         \
            assert(false);                                                                                             \
        }                                                                                                              \
    }

struct AllocatedImage
{
    VkImage image;
    VkImageView image_view;
    VmaAllocation allocation;
    VkExtent3D image_extent;
    VkFormat image_format;
};

struct AllocatedBuffer
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct SwapchainData
{
    vkb::Swapchain swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    AllocatedImage draw_image;
    bool resize_requested = false;
};

struct DescriptorData
{
    VkDescriptorSetLayout layout{ VK_NULL_HANDLE };
    AllocatedBuffer buffer;
    VkDeviceSize size;
    VkDeviceSize offset;
};

struct DeletionQueue
{
private:
    std::deque<std::function<void()>> deletion_queue;

public:
    void push_function(std::function<void()>&& func);
    void flush();
};
