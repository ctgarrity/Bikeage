#include "Renderer.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include <algorithm>
#include <ranges>
#include <iostream>
#include <print>
#include <vulkan/vulkan_core.h>
#include "Types.h"
#include "VkBootstrap.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_vulkan.h"
#include "Initializers.h"
#include "Utilities.h"
#include <glm/gtx/transform.hpp>
#include "PipelineBuilder.h"

void Renderer::init()
{
    init_sdl();
    create_instance();
    create_surface();
    pick_physical_device();
    create_device();
    create_swapchain();
    init_vma();
    create_draw_image();
    create_depth_image();
    create_command_buffers();
    init_sync_structures();
    init_triangle_pipeline();
    init_imgui();
    init_default_data();
}

void Renderer::destroy()
{
    VK_CHECK(vkDeviceWaitIdle(m_device));
    m_swapchain_data.swapchain.destroy_image_views(m_swapchain_data.swapchain_image_views);
    vkb::destroy_swapchain(m_swapchain_data.swapchain);
    destroy_image(m_swapchain_data.draw_image);
    destroy_image(m_swapchain_data.depth_image);
    for (auto& frame : m_frame_data)
    {
        frame.flush_frame_data();
    }
    m_deletion_queue.flush();
}

void Renderer::run()
{
    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_window))
                done = true;
            if (event.window.type == SDL_EVENT_WINDOW_RESIZED)
            {
                // Use pending resize flag and only set resize after mouse release
                m_swapchain_data.resize_requested = true;
            }
        }

        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        if (m_swapchain_data.resize_requested)
        {
            int width, height;
            SDL_GetWindowSize(m_window, &width, &height);
            m_window_extent.width = width;
            m_window_extent.height = height;
            recreate_swapchain();
            m_swapchain_data.resize_requested = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        bool show_demo = true;
        ImGui::ShowDemoWindow(&show_demo);

        ImGui::Render();
        draw_frame();
    }
}

void Renderer::init_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL3_InitForVulkan(m_window);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_instance;
    init_info.PhysicalDevice = m_physical_device;
    init_info.Device = m_device;
    init_info.QueueFamily = m_device.get_queue_index(vkb::QueueType::graphics).value();
    init_info.Queue = m_device.get_queue(vkb::QueueType::graphics).value();
    // init_info.PipelineCache = YOUR_PIPELINE_CACHE; // optional
    // init_info.DescriptorPool = YOUR_DESCRIPTOR_POOL; // see below Todo: Check if the DescriptorPoolSize is correct
    init_info.DescriptorPoolSize =
        IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE; // (Optional) Set to create internal descriptor pool instead
                                                           // of using DescriptorPool
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.MinImageCount = (uint32_t)m_swapchain_data.swapchain_images.size();
    init_info.ImageCount = (uint32_t)m_swapchain_data.swapchain_images.size();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = true;
    // init_info.Allocator = YOUR_ALLOCATOR; // optional
    // init_info.CheckVkResultFn = check_vk_result; // optional

    VkPipelineRenderingCreateInfo pipeline_rendering_create_info = {};
    pipeline_rendering_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipeline_rendering_create_info.pNext = nullptr;
    pipeline_rendering_create_info.colorAttachmentCount = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats = &m_swapchain_data.swapchain.image_format;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;

    ImGui_ImplVulkan_Init(&init_info);
    // (this gets a bit more complicated, see example app for full reference)
    // ImGui_ImplVulkan_CreateFontsTexture(get_current_frame().main_command_buffer);
    // (your code submit a queue)
    // ImGui_ImplVulkan_DestroyFontUploadObjects();
    m_deletion_queue.push_function(
        [&]()
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
        });
}

void Renderer::draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view)
{
    VkRenderingAttachmentInfo color_attachment =
        init::color_attachment_info(target_image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo render_info = init::rendering_info(m_swapchain_data.swapchain.extent, &color_attachment, nullptr);

    vkCmdBeginRendering(cmd, &render_info);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
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
    m_swapchain_data.swapchain_extent_2D = m_window_extent;
}

void Renderer::recreate_swapchain()
{
    vkDeviceWaitIdle(m_device);

    m_swapchain_data.swapchain.destroy_image_views(m_swapchain_data.swapchain_image_views);
    destroy_image(m_swapchain_data.draw_image);
    destroy_image(m_swapchain_data.depth_image);
    create_swapchain();
    create_draw_image();
    create_depth_image();
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

AllocatedBuffer Renderer::create_buffer(size_t alloc_size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.pNext = nullptr;
    buffer_info.size = alloc_size;
    buffer_info.usage = usage;

    VmaAllocationCreateInfo vma_alloc_info = {};
    vma_alloc_info.usage = memory_usage;
    vma_alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    AllocatedBuffer new_buffer = {};
    VK_CHECK(vmaCreateBuffer(
        m_vma_allocator, &buffer_info, &vma_alloc_info, &new_buffer.buffer, &new_buffer.allocation, &new_buffer.info));
    return new_buffer;
}

void Renderer::destroy_buffer(AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(m_vma_allocator, buffer.buffer, buffer.allocation);
}

void Renderer::create_draw_image()
{
    VkExtent3D draw_image_extent = { m_swapchain_data.swapchain_extent_2D.width,
                                     m_swapchain_data.swapchain_extent_2D.height,
                                     1 };

    m_swapchain_data.draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_swapchain_data.draw_image.image_extent = draw_image_extent;

    VkImageUsageFlags draw_image_usages = {};
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_STORAGE_BIT;
    draw_image_usages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    m_swapchain_data.draw_extent_2D = m_swapchain_data.swapchain_extent_2D;

    VkImageCreateInfo render_img_info =
        init::image_create_info(m_swapchain_data.draw_image.image_format, draw_image_usages, draw_image_extent);

    VmaAllocationCreateInfo render_img_alloc_info = {};
    render_img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    render_img_alloc_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(m_vma_allocator,
                   &render_img_info,
                   &render_img_alloc_info,
                   &m_swapchain_data.draw_image.image,
                   &m_swapchain_data.draw_image.allocation,
                   nullptr);

    VkImageViewCreateInfo render_view_info = init::image_view_create_info(
        m_swapchain_data.draw_image.image_format, m_swapchain_data.draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(m_device, &render_view_info, nullptr, &m_swapchain_data.draw_image.image_view));

    std::println("Draw image created\n\twidth: {}\n\theigth: {}",
                 m_swapchain_data.draw_image.image_extent.width,
                 m_swapchain_data.draw_image.image_extent.height);
}

void Renderer::create_depth_image()
{
    m_swapchain_data.depth_image.image_format = VK_FORMAT_D32_SFLOAT;
    m_swapchain_data.depth_image.image_extent = m_swapchain_data.draw_image.image_extent;

    VkImageUsageFlags depth_image_usages = {};
    depth_image_usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo depth_img_info = init::image_create_info(
        m_swapchain_data.depth_image.image_format, depth_image_usages, m_swapchain_data.draw_image.image_extent);

    VmaAllocationCreateInfo render_img_alloc_info = {};
    render_img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    render_img_alloc_info.requiredFlags = static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(m_vma_allocator,
                   &depth_img_info,
                   &render_img_alloc_info,
                   &m_swapchain_data.depth_image.image,
                   &m_swapchain_data.depth_image.allocation,
                   nullptr);

    VkImageViewCreateInfo depth_view_info = init::image_view_create_info(
        m_swapchain_data.depth_image.image_format, m_swapchain_data.depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(m_device, &depth_view_info, nullptr, &m_swapchain_data.depth_image.image_view));
}

void Renderer::destroy_image(AllocatedImage& img)
{
    vkDestroyImageView(m_device, img.image_view, nullptr);
    vmaDestroyImage(m_vma_allocator, img.image, img.allocation);
}

void Renderer::create_command_buffers()
{
    VkCommandPoolCreateInfo command_info = {};
    command_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_info.pNext = nullptr;
    command_info.queueFamilyIndex = m_device.get_queue_index(vkb::QueueType::graphics).value();
    command_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (auto& frame : m_frame_data)
    {
        VK_CHECK(vkCreateCommandPool(m_device, &command_info, nullptr, &frame.command_pool));

        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.pNext = nullptr;
        alloc_info.commandPool = frame.command_pool;
        alloc_info.commandBufferCount = 1;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(m_device, &alloc_info, &frame.command_buffer));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_frame_data.size(); i++)
            {
                vkDestroyCommandPool(m_device, m_frame_data[i].command_pool, nullptr);
            }
        });

    // Immediate Cmd Buffer
    VK_CHECK(vkCreateCommandPool(m_device, &command_info, nullptr, &m_imm_command_pool));
    VkCommandBufferAllocateInfo cmd_alloc_info = init::command_buffer_allocate_info(m_imm_command_pool, 1);
    VK_CHECK(vkAllocateCommandBuffers(m_device, &cmd_alloc_info, &m_imm_command_buffer));

    m_deletion_queue.push_function(
        [this]()
        {
            std::cout << "m_deletion_queue vkDestroyCommandPool" << std::endl;
            vkDestroyCommandPool(m_device, m_imm_command_pool, nullptr);
        });
}

void Renderer::init_sync_structures()
{
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.pNext = nullptr;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphore_info.pNext = nullptr;

    for (auto& frame : m_frame_data)
    {
        VK_CHECK(vkCreateFence(m_device, &fence_info, nullptr, &frame.render_fence));
        VK_CHECK(vkCreateSemaphore(m_device, &semaphore_info, nullptr, &frame.acquire_semaphore));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_frame_data.size(); i++)
            {
                vkDestroyFence(m_device, m_frame_data[i].render_fence, nullptr);
                vkDestroySemaphore(m_device, m_frame_data[i].acquire_semaphore, nullptr);
            }
        });

    m_submit_semaphores.resize(m_swapchain_data.swapchain_images.size());
    for (size_t i = 0; i < m_swapchain_data.swapchain_images.size(); i++)
    {
        VK_CHECK(vkCreateSemaphore(m_device, &semaphore_info, nullptr, &m_submit_semaphores[i]));
    }

    m_deletion_queue.push_function(
        [this]()
        {
            for (size_t i = 0; i < m_swapchain_data.swapchain_images.size(); i++)
            {
                vkDestroySemaphore(m_device, m_submit_semaphores[i], nullptr);
            }
        });

    // Immediate Fence
    VK_CHECK(vkCreateFence(m_device, &fence_info, nullptr, &m_imm_fence));
    m_deletion_queue.push_function(
        [this]()
        {
            std::cout << "m_deletion_queue vkDestroyFence" << std::endl;
            vkDestroyFence(m_device, m_imm_fence, nullptr);
        });
}

// void Renderer::init_descriptors()
// {
//     VkDescriptorSetLayoutBinding layout_binding = {};
//     layout_binding.binding = 0;
//     layout_binding.descriptorCount = 1;
//     layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
//     layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
//     layout_binding.pImmutableSamplers = nullptr;
//
//     std::vector<VkDescriptorSetLayoutBinding> descriptor_layout_bindings;
//     descriptor_layout_bindings.push_back(layout_binding);
//
//     VkDescriptorSetLayoutCreateInfo layout_info = {};
//     layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//     layout_info.pNext = nullptr;
//     layout_info.bindingCount = (uint32_t)descriptor_layout_bindings.size();
//     layout_info.pBindings = descriptor_layout_bindings.data();
//     vkCreateDescriptorSetLayout(m_init_data.device, &layout_info, nullptr, &m_render_data.descriptor_layout);
//     m_deletion_queue.push_function(
//         [this]() { vkDestroyDescriptorSetLayout(m_init_data.device, m_render_data.descriptor_layout, nullptr); });
//
//     std::vector<VkDescriptorPoolSize> pool_sizes = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 } };
//
//     VkDescriptorPoolCreateInfo pool_info = {};
//     pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
//     pool_info.pNext = nullptr;
//     pool_info.pPoolSizes = pool_sizes.data();
//     pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
//     pool_info.maxSets = 1;
//     VK_CHECK(vkCreateDescriptorPool(m_init_data.device, &pool_info, nullptr, &m_render_data.descriptor_pool));
//     m_deletion_queue.push_function(
//         [this]() { vkDestroyDescriptorPool(m_init_data.device, m_render_data.descriptor_pool, nullptr); });
//     std::println("Descriptors initialized");
// }

void Renderer::init_triangle_pipeline()
{
    VkShaderModule triangle_frag_shader;
    if (!util::load_shader_module("shaders/colored_triangle.frag.spv", m_device, &triangle_frag_shader))
    {
        std::cerr << "Error when building the triangle fragment shader module" << std::endl;
    }

    VkShaderModule triangle_vertex_shader;
    // if (!util::load_shader_module("shaders/colored_triangle.vert.spv", m_device, &triangle_vertex_shader))
    if (!util::load_shader_module("shaders/colored_triangle_mesh.vert.spv", m_device, &triangle_vertex_shader))
    {
        std::cerr << "Error when building the triangle vertex shader module" << std::endl;
    }

    VkPushConstantRange buffer_range = {};
    buffer_range.offset = 0;
    buffer_range.size = sizeof(GPUDrawPushConstants);
    buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = init::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &buffer_range;
    pipeline_layout_info.pushConstantRangeCount = 1;
    // No descriptors for now
    // pipeline_layout_info.pSetLayouts = &m_single_image_descriptor_layout;
    // pipeline_layout_info.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(m_device, &pipeline_layout_info, nullptr, &m_triangle_pipeline_layout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.pipeline_layout = m_triangle_pipeline_layout;
    pipelineBuilder.set_shaders(triangle_vertex_shader, triangle_frag_shader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depth_test(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    // pipelineBuilder.disable_depth_test();
    pipelineBuilder.set_color_attachment_format(m_swapchain_data.draw_image.image_format);
    pipelineBuilder.set_depth_format(m_swapchain_data.depth_image.image_format);
    m_triangle_pipeline = pipelineBuilder.build_pipeline(m_device);

    vkDestroyShaderModule(m_device, triangle_frag_shader, nullptr);
    vkDestroyShaderModule(m_device, triangle_vertex_shader, nullptr);

    m_deletion_queue.push_function(
        [&]()
        {
            vkDestroyPipelineLayout(m_device, m_triangle_pipeline_layout, nullptr);
            vkDestroyPipeline(m_device, m_triangle_pipeline, nullptr);
        });
}

void Renderer::draw_triangle(VkCommandBuffer cmd)
{
    VkRenderingAttachmentInfo color_attachment = init::color_attachment_info(
        m_swapchain_data.draw_image.image_view, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo depth_attachment =
        init::depth_attachment_info(m_swapchain_data.depth_image.image_view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo render_info =
        init::rendering_info(m_swapchain_data.draw_extent_2D, &color_attachment, &depth_attachment);
    vkCmdBeginRendering(cmd, &render_info);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_triangle_pipeline);

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = m_swapchain_data.draw_image.image_extent.width;
    viewport.height = m_swapchain_data.draw_image.image_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = m_swapchain_data.draw_image.image_extent.width;
    scissor.extent.height = m_swapchain_data.draw_image.image_extent.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindIndexBuffer(cmd, m_rectangle.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    //m_rectangle_push_constants.world_matrix = glm::mat4{ 1.f };
    m_rectangle_push_constants.vertex_buffer = m_rectangle.vertex_buffer_address;
    glm::mat4 view = glm::translate(glm::vec3{ 0,0,-5 });
    // camera projection
    glm::mat4 projection = glm::perspective(glm::radians(70.f),
                         (float)m_swapchain_data.draw_extent_2D.width / (float)m_swapchain_data.draw_extent_2D.height,
                         10000.f,
                         0.1f);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    projection[1][1] *= -1;
    m_rectangle_push_constants.world_matrix = projection * view;

    vkCmdPushConstants(cmd,
                       m_triangle_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(GPUDrawPushConstants),
                       &m_rectangle_push_constants);

    // vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);
}

void Renderer::draw_frame()
{
    VK_CHECK(vkWaitForFences(m_device, 1, &get_current_frame().render_fence, true, 1'000'000'000));
    VK_CHECK(vkResetFences(m_device, 1, &get_current_frame().render_fence));
    get_current_frame().flush_frame_data();

    uint32_t swapchain_image_index;
    VkResult result = vkAcquireNextImageKHR(m_device,
                                            m_swapchain_data.swapchain,
                                            1'000'000'000,
                                            get_current_frame().acquire_semaphore,
                                            nullptr,
                                            &swapchain_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_swapchain_data.resize_requested = true;
        return;
    }

    VkCommandBuffer cmd_buffer = get_current_frame().command_buffer;
    VK_CHECK(vkResetCommandBuffer(cmd_buffer, 0));
    VkCommandBufferBeginInfo begin_info = init::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd_buffer, &begin_info));

    // Draw Triangle
    util::transition_image(cmd_buffer,
                           m_swapchain_data.draw_image.image,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_triangle(cmd_buffer);

    // Draw ImGui
    util::transition_image(cmd_buffer,
                           m_swapchain_data.draw_image.image,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    util::transition_image(cmd_buffer,
                           m_swapchain_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    util::copy_image_to_image(cmd_buffer,
                              m_swapchain_data.draw_image.image,
                              m_swapchain_data.swapchain_images[swapchain_image_index],
                              m_swapchain_data.draw_extent_2D,
                              m_swapchain_data.swapchain_extent_2D);
    util::transition_image(cmd_buffer,
                           m_swapchain_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd_buffer, m_swapchain_data.swapchain_image_views[swapchain_image_index]);
    util::transition_image(cmd_buffer,
                           m_swapchain_data.swapchain_images[swapchain_image_index],
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd_buffer));

    VkCommandBufferSubmitInfo cmd_buffer_info = init::command_buffer_submit_info(cmd_buffer);
    VkSemaphoreSubmitInfo wait_info = init::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                                                                  get_current_frame().acquire_semaphore);
    VkSemaphoreSubmitInfo signal_info =
        init::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, m_submit_semaphores[swapchain_image_index]);
    VkSubmitInfo2 submit = init::submit_info(&cmd_buffer_info, &signal_info, &wait_info);
    VK_CHECK(vkQueueSubmit2(
        m_device.get_queue(vkb::QueueType::graphics).value(), 1, &submit, get_current_frame().render_fence));

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.pSwapchains = &m_swapchain_data.swapchain.swapchain;
    present_info.swapchainCount = 1;
    present_info.pWaitSemaphores = &m_submit_semaphores[swapchain_image_index];
    present_info.waitSemaphoreCount = 1;
    present_info.pImageIndices = &swapchain_image_index;

    result = vkQueuePresentKHR(m_device.get_queue(vkb::QueueType::graphics).value(), &present_info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        m_swapchain_data.resize_requested = true;
    }

    m_frame_index++;
}

GPUMeshBuffers Renderer::gpu_mesh_upload(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    // Todo: Put this on a background thread?
    const size_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
    const size_t index_buffer_size = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers new_surface;
    new_surface.vertex_buffer = create_buffer(vertex_buffer_size,
                                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              VMA_MEMORY_USAGE_AUTO);

    VkBufferDeviceAddressInfo device_adress_info = { .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                                                     .buffer = new_surface.vertex_buffer.buffer };
    new_surface.vertex_buffer_address = vkGetBufferDeviceAddress(m_device, &device_adress_info);
    new_surface.index_buffer = create_buffer(
        index_buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO);

    AllocatedBuffer staging =
        create_buffer(vertex_buffer_size + index_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO);
    void* data = staging.info.pMappedData; // had to change this from vkguides
                                           // staging.alloction.GetMappedData()
    memcpy(data, vertices.data(), vertex_buffer_size);
    memcpy((char*)data + vertex_buffer_size, indices.data(), index_buffer_size);

    immediate_submit(
        [vertex_buffer_size, index_buffer_size, staging, new_surface](VkCommandBuffer cmd)
        {
            VkBufferCopy vertex_copy = {};
            vertex_copy.dstOffset = 0;
            vertex_copy.srcOffset = 0;
            vertex_copy.size = vertex_buffer_size;
            vkCmdCopyBuffer(cmd, staging.buffer, new_surface.vertex_buffer.buffer, 1, &vertex_copy);

            VkBufferCopy index_copy = {};
            index_copy.dstOffset = 0;
            index_copy.srcOffset = vertex_buffer_size;
            index_copy.size = index_buffer_size;
            vkCmdCopyBuffer(cmd, staging.buffer, new_surface.index_buffer.buffer, 1, &index_copy);
        });

    destroy_buffer(staging);
    return new_surface;
}

void Renderer::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    // Todo: Switch the queue to use another queue rather than graphics
    VK_CHECK(vkResetFences(m_device, 1, &m_imm_fence));
    VK_CHECK(vkResetCommandBuffer(m_imm_command_buffer, 0));

    VkCommandBuffer imm_cmd = m_imm_command_buffer;
    VkCommandBufferBeginInfo cmdBeginInfo =
        init::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(imm_cmd, &cmdBeginInfo));
    function(imm_cmd);
    VK_CHECK(vkEndCommandBuffer(imm_cmd));

    VkCommandBufferSubmitInfo cmd_info = init::command_buffer_submit_info(imm_cmd);
    VkSubmitInfo2 submit_info = init::submit_info(&cmd_info, nullptr, nullptr);
    VK_CHECK(vkQueueSubmit2(m_device.get_queue(vkb::QueueType::graphics).value(), 1, &submit_info, m_imm_fence));
    VK_CHECK(vkWaitForFences(m_device, 1, &m_imm_fence, true, 9999999999));
}

void Renderer::init_default_data()
{
    std::array<Vertex, 4> rect_vertices;
    rect_vertices[0].position = { 0.5, -0.5, 0 };
    rect_vertices[1].position = { 0.5, 0.5, 0 };
    rect_vertices[2].position = { -0.5, -0.5, 0 };
    rect_vertices[3].position = { -0.5, 0.5, 0 };

    rect_vertices[0].color = { 0, 0, 0, 1 };
    rect_vertices[1].color = { 0.5, 0.5, 0.5, 1 };
    rect_vertices[2].color = { 1, 0, 0, 1 };
    rect_vertices[3].color = { 0, 1, 0, 1 };

    std::array<uint32_t, 6> rect_indices;
    rect_indices[0] = 0;
    rect_indices[1] = 1;
    rect_indices[2] = 2;

    rect_indices[3] = 2;
    rect_indices[4] = 1;
    rect_indices[5] = 3;

    m_rectangle = gpu_mesh_upload(rect_indices, rect_vertices);

    m_deletion_queue.push_function(
        [this]()
        {
            std::cout << "m_deletion_queue destroy_buffer(m_rectangle.index_buffer);" << std::endl;
            destroy_buffer(m_rectangle.index_buffer);
            destroy_buffer(m_rectangle.vertex_buffer);
        });
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
