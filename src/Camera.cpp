#include "Camera.h"

glm::mat4 Camera::get_view_matrix()
{
    glm::mat4 cam_translation = glm::translate(glm::mat4{1.f}, position);
    glm::mat4 cam_rotation = get_rotation_matrix();
    return glm::inverse(cam_translation * cam_rotation);
}

glm::mat4 Camera::get_rotation_matrix()
{
    glm::quat pitch_rot = glm::angleAxis(pitch, glm::vec3{1, 0, 0});
    glm::quat yaw_rot = glm::angleAxis(yaw, glm::vec3{0, -1, 0});
    return glm::toMat4(yaw_rot * pitch_rot);
}

void Camera::process_sdl_event(SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (event.key.scancode == SDL_SCANCODE_W)
            velocity.z = -1;
        if (event.key.scancode == SDL_SCANCODE_S)
            velocity.z = 1;
        if (event.key.scancode == SDL_SCANCODE_A)
            velocity.x = -1;
        if (event.key.scancode == SDL_SCANCODE_D)
            velocity.x = 1;
    }
    if (event.type == SDL_EVENT_KEY_UP)
    {
        if (event.key.scancode == SDL_SCANCODE_W || event.key.scancode == SDL_SCANCODE_S)
            velocity.z = 0;
        if (event.key.scancode == SDL_SCANCODE_A || event.key.scancode == SDL_SCANCODE_D)
            velocity.x = 0;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        yaw += event.motion.xrel / 200.f;
        pitch -= event.motion.yrel / 200.f;
        pitch = glm::clamp(pitch, -1.5f, 1.5f);
    }
}

void Camera::update(float delta_s)
{
    glm::mat4 cam_rotation = get_rotation_matrix();
    position += glm::vec3(cam_rotation * glm::vec4(velocity * 3.f * delta_s, 0.f));
}
