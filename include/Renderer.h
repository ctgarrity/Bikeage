#pragma once
#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"
#include "SDL3/SDL.h"
#include "VkBootstrap.h"
#include "vma/vk_mem_alloc.h"

#include <deque>
#include <functional>

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

class Renderer
{
public:
    void init();
    void destroy();
    void run();

private:
    SDL_Window* m_window = nullptr;
    VkExtent2D m_window_extent;
    vkb::Instance m_instance = {};
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice m_physical_device = {};
    vkb::Device m_device = {};
    VmaAllocator m_vma_allocator;

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
    SwapchainData m_swapchain_data;

    VmaAllocator m_allocator = {};
    struct DescriptorData
    {
        VkDescriptorSetLayout layout{ VK_NULL_HANDLE };
        AllocatedBuffer buffer;
        VkDeviceSize size;
        VkDeviceSize offset;
    };

    void init_sdl();
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_device();
    void create_swapchain();
    void init_vma();
    void create_buffer();
    void destroy_buffer();
    void create_image();
    void destroy_image();

    struct DeletionQueue
    {
    private:
        std::deque<std::function<void()>> deletion_queue;

    public:
        void push_function(std::function<void()>&& func);
        void flush();
    };
    DeletionQueue m_deletion_queue;
};
