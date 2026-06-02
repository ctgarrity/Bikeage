# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

This is a C++23 CMake project using Ninja. The build directory is `build/` and outputs to `build/Debug/`.

```bash
# Configure (first time)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run
./build/Debug/Bikeage
```

The executable must be run from the project root because shaders are loaded by relative path (`shaders/*.spv`).

## Shaders

GLSL shaders live in `src/shaders/`. Compiled `.spv` binaries are written to `shaders/` at the project root. CMake automatically recompiles any changed shader via `glslc` as part of `cmake --build build` — the `compile_shaders` target runs before the executable is linked.

The runtime loads `.spv` files via `util::load_shader_module()` in `src/Utilities.cpp` using relative paths, so the executable must be run from the project root.

## Code Style

Formatting is enforced by `.clang-format` (Microsoft-based style). Key rules:
- 4-space indent, 120 column limit
- Allman brace style (braces on their own lines)
- `int*` pointer alignment (left)
- Namespaces are indented

Run `clang-format -i <file>` to format in place.

## Architecture

The entire renderer is encapsulated in the `Renderer` class (`include/Renderer.h`, `src/Renderer.cpp`). `main.cpp` calls `init()`, `run()`, `destroy()` — nothing else.

**Render loop** (`Renderer::run`): SDL event polling → mouse update → ImGui frame → `draw_frame()`. Window resize sets a flag and triggers `recreate_swapchain()` at the top of the next iteration.

**Frame structure**: Double-buffered with `FRAMES_IN_FLIGHT = 2`. Each `FrameData` holds its own command pool/buffer and per-frame `DeletionQueue`. The renderer also maintains per-swapchain-image submit semaphores (one per swapchain image, separate from the per-frame acquire semaphores).

**Draw order within `draw_frame()`**:
1. Compute background — `draw_background()` dispatches `gradient.comp` into the HDR draw image (R16G16B16A16)
2. Mesh pass — `draw_triangle()` renders the loaded glTF mesh over the compute image with depth testing
3. Blit — draw image is blitted to the swapchain image
4. ImGui — rendered directly onto the swapchain image via dynamic rendering

**Resource management**: `DeletionQueue` (a `std::deque<std::function<void()>>`) is the primary RAII mechanism. Lambdas capturing Vulkan handles are pushed at creation time and flushed in reverse order at destroy time. Both `Renderer` (global) and `FrameData` (per-frame) have their own queues.

**Memory**: Vulkan Memory Allocator (VMA) handles all `VkBuffer` and `VkImage` allocations. Wrapper structs are `AllocatedBuffer` and `AllocatedImage` in `include/Types.h`.

**GPU mesh upload** (`gpu_mesh_upload`): Uploads vertices, indices, and per-instance transform matrices via a single staging buffer using `immediate_submit()`. Vertices and instance transforms are bound via buffer device addresses passed as push constants (`GPUDrawPushConstants`), not vertex input bindings.

**Shader push constants**: Two push constant structs — `GPUDrawPushConstants` (vertex stage: world matrix + two BDAs) and `ComputePushConstants` (compute stage: time, colors, mouse cell coords).

**glTF loading** (`src/MeshLoader.cpp`): Uses fastgltf to parse `.gltf`/`.glb` files. All mesh nodes in the default scene are flattened into a single `LoadedMesh` (vertices + indices + one identity instance transform). The default asset is `vendored/gltf-assets/Models/CesiumMan/glTF/CesiumMan.gltf` resolved via the `BIKEAGE_PROJECT_ROOT` compile definition; falls back to a hardcoded quad if loading fails.

**Vulkan initialization order** (in `Renderer::init()`):
SDL → instance → surface → physical device → logical device → swapchain → VMA → descriptors → draw image → depth image → command buffers → sync structures → triangle pipeline → compute pipeline → ImGui → default mesh data

**Key helper namespaces**:
- `init::` (`src/Initializers.cpp`) — thin wrappers that fill Vulkan `CreateInfo` structs
- `util::` (`src/Utilities.cpp`) — image layout transitions, image blits, SPIR-V loading

**VK_CHECK macro**: Defined in `include/Types.h`. Calls `assert(false)` on any non-`VK_SUCCESS` result after printing the result string. All Vulkan calls that return `VkResult` should be wrapped with it.

## Dependencies (all vendored under `vendored/`)

| Library | Purpose |
|---------|---------|
| SDL3 | Window, input, Vulkan surface |
| vk-bootstrap | Instance/device/swapchain creation helpers |
| VMA | GPU memory allocation |
| glm | Math (radians, depth 0-to-1, experimental extensions enabled) |
| fastgltf | glTF 2.0 parsing |
| Dear ImGui | Debug UI (SDL3 + Vulkan backends, dynamic rendering) |
