#include "MeshLoader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <iostream>

namespace
{
glm::mat4 to_glm(const fastgltf::math::fmat4x4& matrix)
{
    glm::mat4 out{ 1.0f };
    for (std::size_t column = 0; column < 4; ++column)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            out[column][row] = matrix[column][row];
        }
    }
    return out;
}

glm::vec3 to_glm(const fastgltf::math::fvec3& vector)
{
    return { vector.x(), vector.y(), vector.z() };
}

glm::vec4 to_glm(const fastgltf::math::fvec4& vector)
{
    return { vector.x(), vector.y(), vector.z(), vector.w() };
}

bool append_primitive(const fastgltf::Asset& asset,
                      const fastgltf::Primitive& primitive,
                      const glm::mat4& transform,
                      LoadedMesh& mesh)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles)
    {
        return true;
    }

    const auto* position_attribute = primitive.findAttribute("POSITION");
    if (position_attribute == primitive.attributes.end())
    {
        std::cerr << "glTF primitive is missing POSITION data.\n";
        return false;
    }

    const auto& position_accessor = asset.accessors[position_attribute->accessorIndex];
    const uint32_t vertex_offset = static_cast<uint32_t>(mesh.vertices.size());

    std::vector<Vertex> vertices(position_accessor.count);
    const glm::mat3 normal_transform = glm::inverseTranspose(glm::mat3(transform));

    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        asset,
        position_accessor,
        [&](const fastgltf::math::fvec3& position, std::size_t index)
        {
            Vertex& vertex = vertices[index];
            vertex.position = glm::vec3(transform * glm::vec4(to_glm(position), 1.0f));
            vertex.normal = { 0.0f, 1.0f, 0.0f };
            vertex.uv_x = 0.0f;
            vertex.uv_y = 0.0f;
            vertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        });

    if (const auto* normal_attribute = primitive.findAttribute("NORMAL"); normal_attribute != primitive.attributes.end())
    {
        const auto& normal_accessor = asset.accessors[normal_attribute->accessorIndex];
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            asset,
            normal_accessor,
            [&](const fastgltf::math::fvec3& normal, std::size_t index)
            {
                vertices[index].normal = glm::normalize(normal_transform * to_glm(normal));
            });
    }

    if (const auto* texcoord_attribute = primitive.findAttribute("TEXCOORD_0");
        texcoord_attribute != primitive.attributes.end())
    {
        const auto& texcoord_accessor = asset.accessors[texcoord_attribute->accessorIndex];
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            asset,
            texcoord_accessor,
            [&](const fastgltf::math::fvec2& uv, std::size_t index)
            {
                vertices[index].uv_x = uv.x();
                vertices[index].uv_y = uv.y();
            });
    }

    if (const auto* color_attribute = primitive.findAttribute("COLOR_0"); color_attribute != primitive.attributes.end())
    {
        const auto& color_accessor = asset.accessors[color_attribute->accessorIndex];
        if (color_accessor.type == fastgltf::AccessorType::Vec3)
        {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset,
                color_accessor,
                [&](const fastgltf::math::fvec3& color, std::size_t index)
                {
                    vertices[index].color = glm::vec4(to_glm(color), 1.0f);
                });
        }
        else if (color_accessor.type == fastgltf::AccessorType::Vec4)
        {
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset,
                color_accessor,
                [&](const fastgltf::math::fvec4& color, std::size_t index)
                {
                    vertices[index].color = to_glm(color);
                });
        }
    }

    mesh.vertices.insert(mesh.vertices.end(), vertices.begin(), vertices.end());

    if (!primitive.indicesAccessor.has_value())
    {
        for (uint32_t index = 0; index < static_cast<uint32_t>(vertices.size()); ++index)
        {
            mesh.indices.push_back(vertex_offset + index);
        }
        return true;
    }

    const auto& index_accessor = asset.accessors[*primitive.indicesAccessor];
    std::vector<uint32_t> indices(index_accessor.count);
    fastgltf::copyFromAccessor<uint32_t>(asset, index_accessor, indices.data());

    for (uint32_t index : indices)
    {
        mesh.indices.push_back(vertex_offset + index);
    }

    return true;
}
}

std::optional<LoadedMesh> LoadMesh(const std::filesystem::path& path)
{
    auto gltf_file = fastgltf::GltfDataBuffer::FromPath(path);
    if (!gltf_file)
    {
        std::cerr << "Failed to open glTF file '" << path << "': " << fastgltf::getErrorMessage(gltf_file.error())
                  << '\n';
        return std::nullopt;
    }

    static constexpr auto supported_extensions = fastgltf::Extensions::KHR_mesh_quantization;
    fastgltf::Parser parser(supported_extensions);

    constexpr auto options = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble |
                             fastgltf::Options::LoadExternalBuffers | fastgltf::Options::GenerateMeshIndices;

    auto asset_result = parser.loadGltf(gltf_file.get(), path.parent_path(), options);
    if (asset_result.error() != fastgltf::Error::None)
    {
        std::cerr << "Failed to parse glTF file '" << path << "': " << fastgltf::getErrorMessage(asset_result.error())
                  << '\n';
        return std::nullopt;
    }

    const fastgltf::Asset& asset = asset_result.get();
    if (asset.scenes.empty())
    {
        std::cerr << "glTF file '" << path << "' does not contain a scene.\n";
        return std::nullopt;
    }

    const std::size_t scene_index = asset.defaultScene.value_or(0);
    if (scene_index >= asset.scenes.size())
    {
        std::cerr << "glTF file '" << path << "' references an invalid default scene.\n";
        return std::nullopt;
    }

    LoadedMesh mesh;
    mesh.instance_transforms.push_back(glm::mat4{ 1.0f });

    bool success = true;
    fastgltf::iterateSceneNodes(
        asset,
        scene_index,
        fastgltf::math::fmat4x4(),
        [&](const fastgltf::Node& node, const fastgltf::math::fmat4x4& matrix)
        {
            if (!success || !node.meshIndex.has_value())
            {
                return;
            }

            const glm::mat4 transform = to_glm(matrix);
            const fastgltf::Mesh& gltf_mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive& primitive : gltf_mesh.primitives)
            {
                if (!append_primitive(asset, primitive, transform, mesh))
                {
                    success = false;
                    return;
                }
            }
        });

    if (!success || mesh.vertices.empty() || mesh.indices.empty())
    {
        std::cerr << "glTF file '" << path << "' did not produce any drawable triangle mesh data.\n";
        return std::nullopt;
    }

    return mesh;
}
