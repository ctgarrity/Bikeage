#pragma once

#include "Types.h"

#include "glm/mat4x4.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

struct LoadedMesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<glm::mat4> instance_transforms;
};

std::optional<LoadedMesh> LoadMesh(const std::filesystem::path& path);
