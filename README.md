# Fire Engine Tutorial
[![CI](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml/badge.svg)](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml)

A tutorial for building a 3D engine step-by-step.
Based on [FireEngine](https://github.com/nnewson/fireEngine) and documented
under the development blog at [nnewson.dev](https://nnewson.dev)

## Build and run

vcpkg is used to manage dependencies. Install it from
[vcpkg.io](https://vcpkg.io/en/getting-started.html). The vcpkg configuration
pins a registry baseline so local and CI builds resolve the same dependency
versions.

All platforms require a C++23 compiler and standard library. This tutorial uses
C++23 library facilities including `std::print` and `std::println`.

Set `VCPKG_ROOT` to a vcpkg checkout, then configure and build:

```sh
cmake --preset vcpkg
cmake --build --preset default
./build/fireEngineTutorial
```

Debug builds enable Vulkan validation when the standard validation layer is
installed. Validation is disabled for every other build configuration.

The interactive application renders until its window closes. A positive frame
limit is available for automation and quick validation:

```sh
./build/fireEngineTutorial --frames 1
```

CTest runs the Vulkan-free Catch2 unit tests and uses that bounded mode for a
one-frame integration test without waiting for user input:

```sh
ctest --preset default
```

## Documentation

Public API comments and the explanatory notes alongside the Vulkan implementation
are published as independent Doxygen views. The generated references are available
at:

- [Public API](https://nnewson.dev/fireEngine-tutorial/)
- [Internal implementation](https://nnewson.dev/fireEngine-tutorial/internals/)

To build the pages locally, install Doxygen and run this from the repository root:

```sh
cmake -E make_directory build/docs
cmake -E make_directory build/docs-internal
doxygen docs/Doxyfile
doxygen docs/Doxyfile.internal
```

Open `build/docs/html/index.html` for the public API or
`build/docs-internal/html/index.html` for the internal implementation.
Documentation warnings fail the build, so missing parameter descriptions and
invalid references are caught by CI. Every CI run uploads both views together as
the `doxygen-html` artifact, with the internal view nested under `internals/`.

GitHub Pages deployment is optional. To publish after each push to `main`, select
GitHub Actions as the repository's Pages source and set the repository Actions
variable `PUBLISH_DOXYGEN` to `true`. Generated HTML remains under `build/` and
is not checked into source control.

The project builds a reusable engine library, a small event-loop executable, and
a Catch2 test executable. `RenderAssets` owns the application's Vulkan-free
`Mesh`, `Material`, and `RenderObject` descriptions; `Scene` owns only the
`SceneNode` hierarchy that instances them. `Renderer::prepare()` validates those
relationships through a cached `RenderPreparation` compiler and uploads each
distinct indexed mesh required by the current `SceneDrawList`. Scene traversal
hashes only ordered render-object dependencies, so transform changes reuse the
same preparation plan while still producing current per-frame draws. Meanwhile,
`Renderer::drawFrame()` traverses the current scene transforms and records the
resulting draws. Device, allocator, swapchain, pipeline, and frame resources all
remain hidden behind the renderer facade.

Mathematical coordinates use `Vec3` and `Vec4`, while graphics colors use the
separate `Color4` aggregate with `r`, `g`, `b`, and `a` components. Both retain
the tightly packed float layout required by the shader interface without giving
color values unrelated vector operations.

The executable links the Vulkan loader supplied by vcpkg and uses GLFW to create
its window surface. Vulkan Memory Allocator owns host-writable vertex, index, and
frame-uniform buffers. A Vulkan driver must still be installed on the machine,
and must expose Vulkan 1.4, dynamic rendering, synchronization 2, push
descriptors, maintenance5, and swapchain presentation support. Because Vulkan
1.4 folded maintenance5 into the core API, pipeline creation reads the compiled
SPIR-V directly and no `VkShaderModule` is ever created.

Each event-loop iteration waits for the previous frame, acquires a swapchain
image, recycles the command pool, and records dynamic-rendering draws from the
scene. The command buffer uses synchronization-2 barriers to discard the
previous image contents, clear the color attachment, bind each compiled mesh's
vertex and index buffers, push the node transform and material color, and
transition the completed image for presentation. `submit2` orders rendering
after acquisition, then the presentation queue waits for the semaphore belonging
to that acquired image.

Synchronization objects are split by what indexes them, which is the boundary
that makes both swapchain recreation and multiple frames in flight tractable
later. `FrameInFlight` owns what belongs to a frame: the image-available
semaphore that orders acquisition before rendering, and the fence that tells the
CPU when the frame's submitted work has finished executing. `Swapchain` owns what
belongs to an image: one render-finished semaphore each, so presentation cannot
still be waiting on a semaphore when a later frame signals it again. The frame
fence cannot cover that case, because presentation runs after the submission the
fence tracks. The event loop exercises both sides of that ownership boundary on
every frame.

On macOS, this tutorial targets the KosmicKrisp technical preview from the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/doc/sdk/1.4.357.0/mac/getting_started.html).
It requires Apple Silicon and macOS 26. Once the SDK installer has registered
its driver manifest in the standard Vulkan search path, the executable runs
without sourcing the SDK environment. `VK_DRIVER_FILES` remains available as an
optional override when selecting among multiple installed drivers. Shader
compilation likewise uses `slangc` supplied by vcpkg rather than an SDK tool in
the environment.

The executable does not directly link KosmicKrisp or enable the unsupported
Vulkan portability extensions. It displays the colored triangle until the user
closes the window. Swapchain recreation remains deliberately separate: if the
surface becomes out of date or suboptimal, the loop waits for the device to
become idle and exits cleanly so a later tutorial can introduce replacement of
the swapchain and format-dependent pipeline together.
