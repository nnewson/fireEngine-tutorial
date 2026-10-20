# Fire Engine Tutorial
[![CI](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml/badge.svg)](https://github.com/nnewson/fireEngine-tutorial/actions/workflows/ci.yml)

A tutorial for building a 3D engine step-by-step.
Based on [FireEngine](https://github.com/nnewson/fireEngine) and documented
under the development blog at [nnewson.dev](https://nnewson.dev)

Version 0.10.0 adds an explicit shadow pass and retains the measured forward
recording controls.

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
adds a procedural ground-plane receiver, uploads both meshes and the cube's
base-color texture, and plays the imported rotation animation until the window
closes. The copied build-tree asset path is used for both direct runs and CTest,
so execution never depends on the working directory.

A directional shadow map makes the cube cast onto the receive-only plane and
shadow its own light-averted faces. This is deliberately simple base-color
attenuation, not a normal-based lighting model: shadow visibility scales RGB
between 0.35 and 1.0 without changing alpha. The fixed 1024 by 1024 depth map
uses nearest comparison filtering, so hard edges and finite resolution are
visible. Receiver regions outside the map remain lit. Brief grazing-angle
self-shadow breakup is a documented limitation retained for 0.10.

Use `--smoke shadow` for the bounded cube-and-plane scene. The reuse and
changed-preparation receiver scenarios below use the same composition. The
`--frames` mode and the four original smoke scenarios retain the AnimatedCube-only
fixture; the shadow pass still runs in all of them.
Accepted reference images, reproduction commands, and visual limits are in
[the image notes](https://github.com/nnewson/fireEngine-tutorial/blob/main/docs/images/README.md).

A positive frame limit remains available for quick validation:

```sh
./build/fireEngineTutorial --frames 1
```

A one-shot diagnostic capture must be paired with a finite frame or smoke run.
The one-based capture ordinal must not exceed that run's frame limit:

```sh
./build/fireEngineTutorial --frames 3 --capture frame.png --capture-frame 3
```

Capture does not shorten the requested run and cannot be combined with a
performance benchmark.

CTest runs the Vulkan-free Catch2 suite plus bounded device scenarios covering
normal AnimatedCube animation, replacement after changed preparation inputs, an
untextured fallback draw, repeated swapchain recreation, frame capture,
benchmark recording controls, and snapshot acceptance. The eight smoke paths can
be selected directly with `--smoke basic`, `--smoke prepare-twice`,
`--smoke untextured`, `--smoke resize`, `--smoke shadow`,
`--smoke shadow-reuse`, `--smoke shadow-prepare-twice`, or
`--smoke snapshot-acceptance`. The `shadow-reuse` scenario reuses the receiver
scene for 32 presented frames without forced recreation or repeated preparation;
`shadow` retains its five-frame capture contract. The `shadow-prepare-twice`
scenario presents two original frames, replaces the active caster with narrowed,
untextured geometry, then presents six replacement frames. It preserves the
receiver and animation, and exercises preparation without an app-side idle wait
or a replacement image upload that could mask a missing retirement wait. All
three receiver scenarios check packet and actual recording-participant counts on
every presented frame, including when `--forward-recording-participants 2`
forces the helper's secondary command buffer to participate. The
`snapshot-acceptance` scenario scripts the catalog contract on the receiver
scene: a snapshot from a newer revision, or with reordered and repeated draws,
presents without preparing; an unprepared object, a same-shaped catalog with
identical numeric IDs, and the replaced catalog's snapshots are rejected; and a
replaced catalog presents only after `prepare()`.

Options belonging to the same mode may be supplied in any order.

```sh
ctest --preset default
```

Linux CI additionally installs the optional `ci-validation` vcpkg feature from
the same pinned registry as the loader and headers. Its validation runner
requires the application's loader trace to show the pinned Khronos layer in
both the instance and device chains of **every** `vulkan-device` test, including
wrapped captures. Merely enumerating the layer or setting sync options is not
enough. It also checks the registered validation settings and rejects failed,
skipped, missing, or output-truncated tests.

The runner then hides the layer for one ordinary and one synchronization test.
Those applications must still run, but the same activation check must reject
their output; restored runs must activate validation again. CI retains the
registry, per-test output and controls in `linux-validation-evidence`. This
checks layer activation, not every validator's ability to detect every fault.
Validation remains optional for normal users and disabled in Release benchmarks.

To reproduce this Linux gate with an X11 display (or under `xvfb-run`):

```sh
cmake --preset vcpkg -DVCPKG_MANIFEST_FEATURES=ci-validation
cmake --build --preset default
python3 -m unittest discover -s tests/ci -v
python3 tools/ci/run_validation_tests.py --installed-dir build/vcpkg_installed/x64-linux
```

## Performance benchmark

The executable can replace AnimatedCube's imported hierarchy with a
deterministic synthetic scene containing any positive number of instances of
its cube geometry:

```sh
./build/fireEngineTutorial --benchmark 10000
```

Add `--forward-direct-primary` to record the same workload directly into the
primary command buffer. This is an attribution control for comparing driver
work around forward secondary command buffers, not an alternative production
renderer:

```sh
./build/fireEngineTutorial --benchmark 10000 --forward-direct-primary
```

Forward recording is split between the coordinator and one helper thread at
5,000 draws per participant (10,000 total for two participants). This retains
the existing policy, not a newly measured crossover: the two-pass rebaseline
resolves a total-active-work improvement on laptop Lavapipe at 10,000 draws,
but leaves NVIDIA's corresponding result unresolved by the registered drift
rule. A forward-only improvement does not resolve that primary question.
The serial shadow pass does not use this threshold.
`--forward-recording-participants 2` overrides that policy so a
forced split can be measured or validated below the threshold; it is a
diagnostic control rather than a production setting. The same option applies to
`--smoke` scenarios so the mixed-resource fixture can be validated in split
form:

```sh
./build/fireEngineTutorial --benchmark 10000 --forward-recording-participants 2
./build/fireEngineTutorial --smoke prepare-twice --forward-recording-participants 2
```

After 16 warm-up frames it measures 64 cleanly presented frames and reports
mean, median, and 95th-percentile CPU durations for transform resolution,
scene-snapshot construction (reported as draw-list build), shared frame-input
compilation and uniform updates, shadow pool reset and primary recording,
forward coordinator and participant pool resets, forward primary and secondary
recording, submission, and presentation waits. Every synthetic instance casts,
so each measured frame records the reported draw count in both passes. These
two-pass active-work figures have a larger workload and denominator than the
historical one-pass baselines. The direct-primary control changes only forward
recording. The per-participant report attributes reset and recording work. Its
overlapping durations are detail, not extra terms to add to the active-work
total. Two Vulkan submission slots are cycled independently of the
driver-selected swapchain image. Frames affected by out-of-date or suboptimal
presentation are excluded from the measured sample set.

Use a Release build for performance results. Values are comparable only for
the same workload, build configuration, machine, and Vulkan driver. The plan's
registered architecture gates use same-session phase ratios from admitted
implementations; absolute hosted-runner timings are observations, not
thresholds. The one-instance CTest scenario checks only that scene generation,
measurement, aggregation, reporting, and Vulkan validation complete
successfully.

Linux CI runs the
[reviewed benchmark matrix](https://github.com/nnewson/fireEngine-tutorial/blob/main/tools/benchmark/README.md) in a
separate Release build and uploads `linux-benchmark-evidence`, including raw
logs, provenance and replayed results. Its job summary distinguishes complete
observations from incomplete acquisition. These hosted timings are not policy
decision measurements or performance thresholds for the correctness gates.

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

## Renderer structure

The project builds a reusable engine library, shared application-support helpers,
a small event-loop executable, and a Catch2 test executable. `GltfLoader`
produces format-neutral `SceneContent`:
`RenderAssets` owns meshes, decoded images, textures, materials, and render-object
relationships; `Scene` owns the hierarchy that instances them; and `Animation`
stores reusable rotation channels. Imported LINEAR rotations use shortest-arc
SLERP. The committed sample and its CC0 attribution live under `assets/AnimatedCube`;
once dependencies are installed, builds and tests need no asset downloads.

`Scene::buildSnapshot()` writes the current draws into a `SceneSnapshot`, which
owns them and records the identity and revision of the `RenderAssets` catalog
they refer to. Applications recycle one draw buffer by releasing each snapshot's
storage into the next build. `Renderer::prepare()` validates the catalog's
relationships through a cached `RenderPreparation` compiler and uploads only the
resources the snapshot selects. The internal `CompiledResources` subsystem owns
mesh buffers, sampled images, samplers, and the persistent white fallback
texture, and records the last frame submission that used them. A changed
`prepare()` compiles the replacement first, then waits only for that submission
before destroying the old resources; it does not wait for presentation. The
separate internal `ResourceCompiler` owns mutable compilation, staging, and its
dedicated setup-time upload command pool and fence. The preparation cache keys
on the catalog's identity and revision and the exact ordered render-object
references, never transforms, so animation-only changes reuse the same plan
while `Renderer::drawFrame()` records current transforms. Neither call keeps a
reference to the snapshot after it returns. `drawFrame()` accepts a snapshot
only from the catalog the installed resources were compiled from, at any
revision whose draws they cover, and rejects anything else before acquiring an
image; `prepare()` rejects a snapshot built from another catalog. Catalog
identity follows content through moves and is never reused. Device, allocator,
resource compiler, compiled resources, presentation state, and frame resources
remain hidden behind the renderer facade.

Mathematical coordinates use `Vec3` and `Vec4`, while graphics colors use the
separate `Color4` aggregate with `r`, `g`, `b`, and `a` components. Both retain
the tightly packed float layout required by the shader interface without giving
color values unrelated vector operations.

The executable links the Vulkan loader supplied by vcpkg and uses GLFW to create
its window surface. Vulkan Memory Allocator owns vertex, index, staging,
frame-uniform, sampled-image, and depth-image allocations. A Vulkan driver must
still be installed on the machine and expose Vulkan 1.4, timeline semaphores,
dynamic rendering, synchronization 2, push descriptors, maintenance5, swapchain
presentation, and the KHR or equivalent EXT swapchain-maintenance path used for
presentation fences. The pipelines read compiled `shadow.spv` and `forward.spv`
directly through maintenance5, so no `VkShaderModule` is created.

One frame-input compiler walks the snapshot's draws once. Each resource lookup
emits a forward packet and, only for a caster, a geometry-only shadow packet.
The two restricted, borrowed views expose no resource owners to their recorders.
Both passes use camera/light matrices from one frame-slot uniform buffer;
each actual forward draw command buffer pushes its own shadow descriptor.

The ownership boundary is explicit rather than a render graph:

```text
Renderer
  compiled scene resources; shadow pipeline and comparison sampler
  frame-input compiler; shadow recorder; forward recorder and its helper
  frame timeline semaphore and submission serials (slot selection)
  FrameResources[2]
    FrameSlot: acquisition semaphore, pending-work state
    shared uniform buffer
    shadow: depth map and primary recording context
    forward: primary and participant recording contexts
  replaceable PresentationState
    swapchain images/views and per-image render-finished semaphores
    per-image presentation fences
    per-slot forward depth attachments and compatible forward pipeline
```

After freezing input, the coordinator waits on the renderer's timeline
semaphore for the serial of the selected slot's last submission, then updates
that slot's uniform buffer. A slot's first use waits for nothing. It acquires a
swapchain image, retires its previous presentation fence, then records the
shadow primary followed by the forward primary. The forward recorder may
dispatch its helper and join it before executing the completed secondaries. The
CPU records one frame at a time; the two submission slots cycle independently
of acquired image indices.

The shadow entry barrier discards contents only after that wait has retired
**all** prior uses of that map. Its exit barrier publishes early/late
fragment-test depth writes to fragment-shader sampled reads. One `submit2`
contains the shadow and forward primaries in that order, and signals the next
serial on the timeline with all-commands scope, retiring both; no inter-pass
semaphore is needed. The serial is published, the slot advanced, and the serial
recorded as the compiled resources' last use, only after submission succeeds. An
attempt that fails before successful submission consumes no serial. Later
capture or presentation failures leave all three intact. The acquisition
semaphore orders swapchain color-attachment work. Ordinary presentation waits on
the acquired image's render-finished semaphore. A diagnostic capture instead
retires the complete submission on the host before presenting.

Presentation fences supplied through swapchain maintenance cover the image and
semaphore lifetime that the timeline's command completion cannot cover.
Recreation retires submitted work, offers the old swapchain to Vulkan, and
replaces only `PresentationState`. It preserves prepared scene resources, both
shadow maps, the shadow pipeline, the comparison sampler, and the frame
timeline's serials. Forward and shadow depth formats are selected and reported
independently.

Framebuffer callbacks request replacement after resize. A zero framebuffer
extent waits for events rather than rebuilding repeatedly. Minimization does
not necessarily produce zero extent: reviewed macOS/KosmicKrisp runs continued
rendering at about 120 presentations/s while hidden, with no recreation churn.
They recovered and closed cleanly, but hidden-window GPU/CPU work is a retained
limitation, not an idle-power claim.

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
