#pragma once
#include "Types.h"
#include "SDL3/SDL.h"
#include "VkBootstrap.h"
#include "vma/vk_mem_alloc.h"
#include <vulkan/vulkan_core.h>
#include <array>

class Renderer
{
public:
    void init();
    void destroy();
    void run();

private:
    static constexpr unsigned int FRAMES_IN_FLIGHT = 2;

    VmaAllocator m_vma_allocator;
    DeletionQueue m_deletion_queue;

    SDL_Window* m_window = nullptr;
    VkExtent2D m_window_extent;
    vkb::Instance m_instance = {};
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    vkb::PhysicalDevice m_physical_device = {};
    vkb::Device m_device = {};
    SwapchainData m_swapchain_data;

    std::array<FrameData, FRAMES_IN_FLIGHT> m_frame_data;
    std::vector<VkSemaphore> m_submit_semaphores;
    uint32_t m_frame_index = 0;

    VkPipeline m_triangle_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_triangle_pipeline_layout = VK_NULL_HANDLE;
    GPUDrawPushConstants m_rectangle_push_constants;
    GPUMeshBuffers m_rectangle;

    VkFence m_imm_fence = VK_NULL_HANDLE;
    VkCommandBuffer m_imm_command_buffer = VK_NULL_HANDLE;
    VkCommandPool m_imm_command_pool = VK_NULL_HANDLE;

    void init_sdl();
    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_device();
    void create_swapchain();
    void recreate_swapchain();
    void init_vma();

    AllocatedBuffer create_buffer(size_t alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);
    void destroy_buffer(AllocatedBuffer& buffer);
    void create_draw_image();
    void create_depth_image();
    void destroy_image(AllocatedImage& img);

    void init_imgui();
    void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view);

    void create_command_buffers();
    void init_sync_structures();
    // void init_descriptors();
    void init_triangle_pipeline();
    void draw_triangle(VkCommandBuffer cmd);
    void draw_frame();

    GPUMeshBuffers gpu_mesh_upload(std::span<uint32_t> indices, std::span<Vertex> vertices);
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
    void init_default_data();
    FrameData& get_current_frame()
    {
        return m_frame_data[m_frame_index % FRAMES_IN_FLIGHT];
    };
};
