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

The interactive application loads the committed Khronos AnimatedCube glTF,
uploads its mesh and base-color texture, and plays its imported rotation
animation until the window closes. The copied build-tree asset path is used for
both direct runs and CTest, so execution never depends on the working directory.

A positive frame limit remains available for quick validation:

```sh
./build/fireEngineTutorial --frames 1
```

CTest runs the Vulkan-free Catch2 suite plus five bounded device scenarios:
normal AnimatedCube animation, replacement after changed preparation inputs,
an untextured fallback draw, repeated swapchain recreation, and a one-instance
benchmark correctness run. The four smoke paths can be selected directly with
`--smoke basic`, `--smoke prepare-twice`, `--smoke untextured`, or
`--smoke resize`.

```sh
ctest --preset default
```

## Performance benchmark

The executable can replace AnimatedCube's imported hierarchy with a
deterministic synthetic scene containing any positive number of instances of
its cube geometry:

```sh
./build/fireEngineTutorial --benchmark 10000
```

Add `--direct-primary` to record the same workload directly into the primary
command buffer. This is an attribution control for comparing driver work around
secondary command buffers, not an alternative production renderer:

```sh
./build/fireEngineTutorial --benchmark 10000 --direct-primary
```

Add `--recording-threads 2` to split one frame's draws between the coordinator
and one helper thread, each recording its own secondary command buffer. The
same option applies to `--smoke` scenarios so the mixed-resource fixture can be
validated in split form:

```sh
./build/fireEngineTutorial --benchmark 10000 --recording-threads 2
./build/fireEngineTutorial --smoke prepare-twice --recording-threads 2
```

After 16 warm-up frames it measures 64 cleanly presented frames and reports
mean, median, and 95th-percentile CPU durations for transform resolution,
draw-list construction, recording-input compilation, frame-uniform updates,
coordinator and worker command-pool reset, primary and secondary recording,
submission, and presentation waits. The per-slot report attributes worker-owned
reset and recording work; it does not claim a placement speedup before workers
exist. Two Vulkan submission slots are cycled independently of the
driver-selected swapchain image. Frames affected by out-of-date or suboptimal
presentation are excluded from the measured sample set.

Use a Release build for performance results. Values are comparable only for
the same workload, build configuration, machine, and Vulkan driver. The plan's
registered architecture gates use same-session phase ratios from admitted
implementations; absolute hosted-runner timings are observations, not
thresholds. The one-instance CTest scenario checks only that scene generation,
measurement, aggregation, reporting, and Vulkan validation complete
successfully.

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
a Catch2 test executable. `GltfLoader` produces format-neutral `SceneContent`:
`RenderAssets` owns meshes, decoded images, textures, materials, and render-object
relationships; `Scene` owns the hierarchy that instances them; and `Animation`
stores reusable rotation channels. The committed sample and its CC0 attribution
live under `assets/AnimatedCube`, so builds and tests require no network access.

`Renderer::prepare()` validates those relationships through a cached
`RenderPreparation` compiler and uploads only the resources selected by the
current `SceneDrawList`. The internal `CompiledResources` subsystem owns mesh
buffers, sampled images, samplers, and the persistent white fallback texture.
The separate internal `ResourceCompiler` owns mutable compilation, staging, and
its dedicated setup-time upload command pool and fence. Scene traversal hashes
only ordered render-object dependencies, so animation-only transform changes
reuse the same preparation plan while `Renderer::drawFrame()` records current
transforms. Device, allocator, resource compiler, compiled resources,
presentation state, and frame resources remain hidden behind the renderer
facade.

Mathematical coordinates use `Vec3` and `Vec4`, while graphics colors use the
separate `Color4` aggregate with `r`, `g`, `b`, and `a` components. Both retain
the tightly packed float layout required by the shader interface without giving
color values unrelated vector operations.

The executable links the Vulkan loader supplied by vcpkg and uses GLFW to create
its window surface. Vulkan Memory Allocator owns vertex, index, staging,
frame-uniform, sampled-image, and depth-image allocations. A Vulkan driver must
still be installed on the machine and expose Vulkan 1.4, dynamic rendering,
synchronization 2, push descriptors, maintenance5, swapchain presentation, and
the KHR or equivalent EXT swapchain-maintenance path used for presentation
fences. Pipeline creation reads the compiled `scene.spv` directly through
maintenance5, so no `VkShaderModule` is created.

Each event-loop iteration waits for the previous frame, acquires a swapchain
image, recycles the command pool, and records dynamic-rendering draws from the
scene. The command buffer uses synchronization-2 barriers to discard prior
attachment contents, clear color and depth, bind compiled mesh and texture
resources, push the node transform and material factor, and transition the
completed image for presentation. The Slang scene shader applies the current
view-projection and samples the glTF base-color texture. `submit2` orders
rendering after acquisition, then the presentation queue waits for the semaphore
belonging to that acquired image.

Synchronization objects are split by what indexes them, which is the boundary
that makes both swapchain recreation and multiple frames in flight tractable
later. `FrameSlot` owns what belongs to a submission slot: uniform storage, the
image-available semaphore that orders acquisition before rendering, the fence
that tells the CPU when submitted work has finished, and pending-work state.
Independent recording contexts own the coordinator's primary pool and the
worker's secondary pool. `Swapchain` owns what
belongs to an image: one render-finished semaphore each, so presentation cannot
still be waiting on a semaphore when a later frame signals it again. The frame
fence cannot cover that case, because presentation runs after the submission the
fence tracks. A presentation fence supplied through swapchain maintenance proves
when each presented image and semaphore can be retired.

The swapchain, depth image, format-compatible pipeline, render-finished
semaphores, and presentation fences form one replaceable `PresentationState`.
Framebuffer callbacks request replacement after resize; zero-sized minimized
windows wait for an event instead of spinning. Recreation passes the retired
swapchain to Vulkan, updates the projection for the new extent, and leaves
compiled meshes and textures intact.

On macOS, this tutorial targets the KosmicKrisp technical preview from the
[LunarG Vulkan SDK](https://vulkan.lunarg.com/doc/sdk/1.4.357.0/mac/getting_started.html).
It requires Apple Silicon and macOS 26. Once the SDK installer has registered
its driver manifest in the standard Vulkan search path, the executable runs
without sourcing the SDK environment. `VK_DRIVER_FILES` remains available as an
optional override when selecting among multiple installed drivers. Shader
compilation likewise uses `slangc` supplied by vcpkg rather than an SDK tool in
the environment.

The executable does not directly link KosmicKrisp or enable the unsupported
Vulkan portability extensions. It displays the textured, depth-tested
AnimatedCube and continues rendering across resize, minimize/restore, and
out-of-date or suboptimal presentation results.
