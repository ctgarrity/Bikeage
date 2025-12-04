#pragma once
#include "VkBootstrap.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vma/vk_mem_alloc.h"
#include "glm/glm.hpp"

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
    VkExtent2D swapchain_extent_2D;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    AllocatedImage draw_image;
    VkExtent2D draw_extent_2D;
    AllocatedImage depth_image;
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

struct FrameData
{
private:
    DeletionQueue deletion_queue;

public:
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;

    VkSemaphore acquire_semaphore;
    VkFence render_fence;

    void flush_frame_data()
    {
        deletion_queue.flush();
    }
};

struct Vertex
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUDrawPushConstants
{
    glm::mat4 world_matrix;
    VkDeviceAddress vertex_buffer;
};

struct ComputePushConstants
{
    glm::vec4 time;
    glm::vec4 color1 = {};
    glm::vec4 color2 = {};
    glm::vec4 cell_coords = {};
};

struct GPUMeshBuffers
{
    AllocatedBuffer index_buffer;
    AllocatedBuffer vertex_buffer;
    VkDeviceAddress vertex_buffer_address;
};

struct MousePos
{
    float x;
    float y;
};
