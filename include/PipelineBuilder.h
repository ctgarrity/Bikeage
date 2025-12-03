#pragma once
#include <vector>
#include "vulkan/vulkan.h"

class PipelineBuilder
{
public:
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages = {};
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    VkPipelineLayout pipeline_layout = {};
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    VkPipelineRenderingCreateInfo render_info = {};
    VkFormat color_attachment_format = {};

    PipelineBuilder();
    ~PipelineBuilder();
    void clear();
    VkPipeline build_pipeline(VkDevice device);
    void set_shaders(VkShaderModule vertex_shader, VkShaderModule fragment_shader);
    void set_input_topology(VkPrimitiveTopology topology);
    void set_polygon_mode(VkPolygonMode mode);
    void set_cull_mode(VkCullModeFlags cull_mode, VkFrontFace front_face);
    void set_multisampling_none();
    void enable_blending_additive();
    void enable_blending_alpha_blend();
    void disable_blending();
    void set_color_attachment_format(VkFormat format);
    void set_depth_format(VkFormat format);
    void enable_depth_test(bool depth_write_enable, VkCompareOp op);
    void disable_depth_test();
};
