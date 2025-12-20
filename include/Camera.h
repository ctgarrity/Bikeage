#pragma once
#include "glm/glm.hpp"
#include "SDL3/SDL.h"

class Camera
{
public:
    glm::vec3 velocity;
    glm::vec3 position;

    float pitch{ 0.0f };
    float yaw{ 0.0f };

    glm::mat4 get_view_matrix();
    glm::mat4 get_rotation_matrix();

    void process_sdl_event(SDL_Event& event);

    void update();
};
