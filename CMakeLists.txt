cmake_minimum_required(VERSION 3.26)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Bikeage VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

find_package(Vulkan REQUIRED)
add_subdirectory(vendored/sdl EXCLUDE_FROM_ALL)
add_subdirectory(vendored/vk-bootstrap EXCLUDE_FROM_ALL)
add_subdirectory(vendored/vma EXCLUDE_FROM_ALL)
add_subdirectory(vendored/glm EXCLUDE_FROM_ALL)
add_subdirectory(vendored/fastgltf EXCLUDE_FROM_ALL)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(SOURCES
  src/main.cpp
  src/VmaUsage.cpp
  src/Renderer.cpp
  src/Initializers.cpp
  src/Utilities.cpp
  src/PipelineBuilder.cpp
  src/Camera.cpp
  src/MeshLoader.cpp

  vendored/imgui/imgui.cpp
  vendored/imgui/imgui_demo.cpp
  vendored/imgui/imgui_draw.cpp
  vendored/imgui/imgui_tables.cpp
  vendored/imgui/imgui_widgets.cpp
  vendored/imgui/backends/imgui_impl_sdl3.cpp
  vendored/imgui/backends/imgui_impl_vulkan.cpp
)

set(HEADERS
    include/Renderer.h
    include/Types.h
    include/Initializers.h
    include/Utilities.h
    include/PipelineBuilder.h
    include/Camera.h
    include/MeshLoader.h
)

find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)

set(SHADER_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/shaders")

set(SHADER_SOURCES
    src/shaders/colored_triangle.vert
    src/shaders/colored_triangle_mesh.vert
    src/shaders/colored_triangle.frag
    src/shaders/gradient.comp
)
set(SHADER_OUTPUTS
    ${SHADER_OUTPUT_DIR}/colored_triangle.vert.spv
    ${SHADER_OUTPUT_DIR}/colored_triangle_mesh.vert.spv
    ${SHADER_OUTPUT_DIR}/colored_triangle.frag.spv
    ${SHADER_OUTPUT_DIR}/gradient.spv
)

foreach(SRC OUT IN ZIP_LISTS SHADER_SOURCES SHADER_OUTPUTS)
    add_custom_command(
        OUTPUT  ${OUT}
        COMMAND ${GLSLC} ${PROJECT_SOURCE_DIR}/${SRC} -o ${OUT}
        DEPENDS ${PROJECT_SOURCE_DIR}/${SRC}
        COMMENT "Compiling shader ${SRC}"
    )
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
)

add_dependencies(${PROJECT_NAME} compile_shaders)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    BIKEAGE_PROJECT_ROOT="${PROJECT_SOURCE_DIR}"
)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/vendored
        ${PROJECT_SOURCE_DIR}/vendored/imgui
        ${PROJECT_SOURCE_DIR}/vendored/imgui/backends
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS} Vulkan::Vulkan SDL3::SDL3 vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator glm::glm fastgltf::fastgltf)
