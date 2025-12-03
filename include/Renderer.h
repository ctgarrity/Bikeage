#pragma once
#include "Types.h"
#include "SDL3/SDL.h"
#include "VkBootstrap.h"
#include "vma/vk_mem_alloc.h"

class Renderer
{
public:
    void init();
    void destroy();
    void run();

private:
    VmaAllocator m_vma_allocator;
    DeletionQueue m_deletion_queue;

    SDL_Window* m_window = nullptr;
    VkExtent2D m_window_extent;
    vkb::Instance m_instance = {};
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice m_physical_device = {};
    vkb::Device m_device = {};
    SwapchainData m_swapchain_data;
    AllocatedImage m_draw_image;

    void init_sdl();
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_device();
    void create_swapchain();
    void init_vma();
    void create_buffer();
    void destroy_buffer();
    void create_image(AllocatedImage& image, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage);
    void destroy_image();
};
