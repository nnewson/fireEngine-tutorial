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

Or run the same executable through CTest:

```sh
ctest --preset default
```

## Documentation

Public API comments and the explanatory notes alongside the Vulkan implementation
are published as Doxygen HTML. The generated API reference is available at
[nnewson.dev/fireEngine-tutorial](https://nnewson.dev/fireEngine-tutorial/).

To build the pages locally, install Doxygen and run this from the repository root:

```sh
cmake -E make_directory build/docs
doxygen docs/Doxyfile
```

Open `build/docs/html/index.html` to browse the result. Documentation warnings
fail the build, so missing parameter descriptions and invalid references are
caught by CI. Every CI run uploads the generated site as the `doxygen-html`
artifact.

GitHub Pages deployment is optional. To publish after each push to `main`, select
GitHub Actions as the repository's Pages source and set the repository Actions
variable `PUBLISH_DOXYGEN` to `true`. Generated HTML remains under `build/` and
is not checked into source control.

The executable links the Vulkan loader supplied by vcpkg, uses GLFW to create
its window surface, and initializes Vulkan Memory Allocator for future render
resources. It selects a suitable presentation format and mode, creates a
swapchain and one image view per swapchain image, then builds a push-descriptor
layout and dynamic-rendering graphics pipeline from build-time-compiled Slang.
A Vulkan driver must still be installed on the machine, and must expose Vulkan
1.4, dynamic rendering, synchronization 2, push descriptors, maintenance5, and
swapchain presentation support. Because Vulkan 1.4 folded maintenance5 into the
core API, pipeline creation reads the compiled SPIR-V directly and no
`VkShaderModule` is ever created.

On macOS, this tutorial targets the KosmicKrisp technical preview from the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/doc/sdk/1.4.357.0/mac/getting_started.html).
It requires Apple Silicon and macOS 26. Once the SDK installer has registered
its driver manifest in the standard Vulkan search path, the executable runs
without sourcing the SDK environment. `VK_DRIVER_FILES` remains available as an
optional override when selecting among multiple installed drivers. Shader
compilation likewise uses `slangc` supplied by vcpkg rather than an SDK tool in
the environment.

The executable does not directly link KosmicKrisp or enable the unsupported
Vulkan portability extensions. At this milestone the GLFW window closes as soon
as allocator, swapchain, image-view, and graphics-pipeline creation have
completed successfully; command recording and rendering follow in later
milestones.
