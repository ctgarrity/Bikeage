#include "Renderer.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include <algorithm>
#include <ranges>
#include <iostream>
#include <print>
#include <vulkan/vulkan_core.h>

void Renderer::init()
{
    init_sdl();
    create_instance();
    create_surface();
    pick_physical_device();
    create_device();
    create_swapchain();
    init_vma();
}

void Renderer::destroy()
{
    m_deletion_queue.flush();
}

void Renderer::run()
{
    // Main draw loop
}

void Renderer::init_sdl()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        std::cerr << "Error: SDL_Init(): " << SDL_GetError() << std::endl;
        return;
    }
    m_deletion_queue.push_function([]() { SDL_Quit(); });

    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    int requested_width = 1280;
    int requested_height = 800;
    m_window = SDL_CreateWindow("Bikeage Renderer", requested_width, requested_height, window_flags);
    if (!m_window)
    {
        std::cerr << "Error: SDL_CreateWindow(): " << SDL_GetError() << std::endl;
        return;
    }
    m_deletion_queue.push_function([this]() { SDL_DestroyWindow(m_window); });

    int width, height;
    SDL_GetWindowSize(m_window, &width, &height);
    m_window_extent.width = width;
    m_window_extent.height = height;
    std::println("Window size: Width {}, Height {}", m_window_extent.width, m_window_extent.height);
}

void Renderer::create_instance()
{
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        std::cerr << system_info_ret.error().message() << std::endl;
    }
    auto system_info = system_info_ret.value();
    std::println("Instance API: {}", system_info.instance_api_version);

    uint32_t sdl_extension_count = 0;
    const char* const* sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extension_count);

    vkb::InstanceBuilder instance_builder;
    auto instance_builder_return = instance_builder.set_app_name("Compute Shader Playground")
                                       .set_engine_name("Compute Shader Playground")
                                       .require_api_version(1, 4, 0)
                                       .enable_validation_layers()
                                       .use_default_debug_messenger()
                                       .enable_extensions(static_cast<size_t>(sdl_extension_count), sdl_extensions)
                                       .build();

    if (!instance_builder_return)
    {
        std::cerr << "Failed to create vkb instance: " << instance_builder_return.error().message() << std::endl;
    }

    m_instance = instance_builder_return.value();
    m_deletion_queue.push_function([this]() { vkb::destroy_instance(m_instance); });
}

void Renderer::create_surface()
{
    if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface))
    {
        std::cerr << "Error: SDL_Vulkan_CreateSurface(): " << SDL_GetError() << std::endl;
    }
    m_deletion_queue.push_function([this]() { SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr); });
}

void Renderer::pick_physical_device()
{
    VkPhysicalDeviceVulkan13Features features13 = {};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = true;

    vkb::PhysicalDeviceSelector selector{ m_instance };
    auto phys_ret = selector.set_surface(m_surface)
                        .prefer_gpu_device_type()
                        .set_required_features_12(features12)
                        .set_required_features_13(features13)
                        .select();
    if (!phys_ret)
    {
        std::cerr << "Failed to select Vulkan Physical Device. Error: " << phys_ret.error().message() << "\n";
        if (phys_ret.error() == vkb::PhysicalDeviceError::no_suitable_device)
        {
            const auto& detailed_reasons = phys_ret.detailed_failure_reasons();
            if (!detailed_reasons.empty())
            {
                std::cerr << "GPU Selection failure reasons:\n";
                for (const std::string& reason : detailed_reasons)
                {
                    std::cerr << reason << "\n";
                }
            }
        }
        return;
    }

    m_physical_device = phys_ret.value();
    if (!m_physical_device.enable_extension_if_present(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME))
    {
        std::cerr << VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME << " not present!" << std::endl;
    }
}

void Renderer::create_device()
{
    vkb::DeviceBuilder device_builder{ m_physical_device };
    auto dev_ret = device_builder.build();
    if (!dev_ret)
    {
        std::cerr << "Failed to create Vulkan device. Error: " << dev_ret.error().message() << std::endl;
    }
    m_device = dev_ret.value();
    m_deletion_queue.push_function([this]() { vkb::destroy_device(m_device); });
}

void Renderer::create_swapchain()
{
    vkb::SwapchainBuilder swapchain_builder{ m_device };
    auto swap_builder_ret =
        swapchain_builder.set_old_swapchain(m_swapchain_data.swapchain)
            .set_desired_min_image_count(3)
            .set_desired_extent(m_window_extent.width, m_window_extent.height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
            .build();

    if (!swap_builder_ret)
    {
        std::cerr << "Failed to create swapchain" << std::endl;
        return;
    }

    vkb::destroy_swapchain(m_swapchain_data.swapchain);
    m_swapchain_data.swapchain = swap_builder_ret.value();
    m_swapchain_data.swapchain_images = m_swapchain_data.swapchain.get_images().value();
    m_swapchain_data.swapchain_image_views = m_swapchain_data.swapchain.get_image_views().value();
}

void Renderer::init_vma()
{
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret)
    {
        std::cerr << system_info_ret.error().message() << std::endl;
    }
    auto system_info = system_info_ret.value();

    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = m_instance;
    alloc_info.physicalDevice = m_physical_device;
    alloc_info.device = m_device;
    alloc_info.vulkanApiVersion = system_info.instance_api_version;
    alloc_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&alloc_info, &m_vma_allocator));
    m_deletion_queue.push_function([this]() { vmaDestroyAllocator(m_vma_allocator); });
}

void Renderer::create_buffer()
{
    // Init
}

void Renderer::destroy_buffer()
{
    // Init
}

void Renderer::create_image(AllocatedImage& image, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage)
{
    VkExtent3D draw_image_extent = { extent.width, extent.height, 1 };
    image.image_extent = draw_image_extent;
    image.image_format = format;

    // VkImageUsageFlags image_usage = {};
    // image_usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // image_usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // image_usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    // image_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = nullptr;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = m_draw_image.image_format;
    image_info.extent = m_draw_image.image_extent;
    image_info.usage = usage;
    image_info.arrayLayers = 1;
    image_info.mipLevels = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(m_vma_allocator, &image_info, &alloc_info, &image.image, &image.allocation, nullptr));

    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.pNext = nullptr;
    image_view_info.image = image.image;
    image_view_info.format = image.image_format;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VK_CHECK(vkCreateImageView(m_device, &image_view_info, nullptr, &image.image_view));
}

void Renderer::destroy_image()
{
    // Init
}

void DeletionQueue::push_function(std::function<void()>&& func)
{
    deletion_queue.push_back(std::move(func));
}

void DeletionQueue::flush()
{
    for (auto& func : std::views::reverse(deletion_queue))
    {
        func();
    }
    deletion_queue.clear();
}
