#include "Camera.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_events.h"

glm::mat4 Camera::get_view_matrix()
{
}

glm::mat4 Camera::get_rotation_matrix()
{
}

void Camera::process_sdl_event(SDL_Event& event)
{
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            pitch = event.motion.yrel;
            yaw = event.motion.xrel;
        }
    }
}

void Camera::update()
{
}
