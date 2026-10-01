# Reference images

Committed images are evidence, not decoration. Each one records a rendered
state that later work must be able to reproduce or deliberately supersede.

## `0.10-step-0-animated-cube.png`

The AnimatedCube scene as it rendered immediately before shadow work began. It
is the historical visual baseline for 0.10. A changed image requires an
explanation: an intended shadow effect, a deliberate and separately recorded
correction such as the SLERP change below, or a regression to investigate.

### Provenance

| Field | Value |
| --- | --- |
| SHA-256 | `a8fff0f2a9ea5ba650cd41c73ab7df85dd15b28b73eee5ae26a9875b41a0297e` |
| Size | 1,537,547 bytes |
| Image | 1600 x 1200, 8-bit RGBA, non-interlaced PNG |
| Swapchain format | `B8G8R8A8Srgb`, FIFO presentation |
| Frame ordinal | 3 (one-based, presented frames) |
| Presentation recreations | 0 at the captured attempt |
| Source commit | `df918305d92361295346c50d34f79b16a18977e0` |
| Host | Apple M2 Pro, arm64, macOS 26.6 build 25G72 |
| Build | Release, Xcode 26.6 build 17F113 |
| API and driver | Vulkan 1.4, KosmicKrisp `vulkan-sdk-1.4.357.0 (git-6e2f85ffe3)` |
| `VCPKG_COMMIT` | `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` |
| stb port | `2024-07-29#1` |
| PNG encoder | stb defaults; `stbi_write_png_compression_level` is never assigned |

### Reproducing it

Use the recorded source revision, which uses NLERP animation playback. The
SLERP correction below changes the pose selected by frame three; running this
command against that later binary is not a reproduction of the old pose.

```sh
./build-benchmark/fireEngineTutorial \
    --smoke basic --capture <output.png> --capture-frame 3
shasum -a 256 <output.png>
```

`--smoke basic` advances animation by a fixed 0.8-second step and exits after
exactly three presented frames, so the ordinal selects a deterministic scene
state rather than a wall-clock one. Two independent Release runs from the source
commit produced byte-identical files before this one was accepted; a Debug build
of the same tree reproduces the same digest, so the image does not depend on
build configuration.

Byte equality depends on the pinned dependency set as well as the renderer. A
different `VCPKG_COMMIT` can change stb's deflate output while leaving every
pixel identical, so a digest mismatch is a dependency question before it is a
rendering question.

### What it does not establish

- **It is not portable across platforms.** The logical window is 800 by 600;
  this host's Retina framebuffer makes the physical extent 1600 by 1200. Linux
  CI captures 800 by 600 and exercises the same code path, but its output must
  never be compared against this file.
- **It is not evidence about production swapchain configuration.** Capture adds
  `eTransferSrc` to swapchain image usage, which ordinary runs never request.
  The pixels are unaffected; the configuration is not the shipped one.
- **It is not evidence about cost.** Capture performs a host fence wait, an
  image-to-buffer copy, and file I/O. Benchmarks reject `--capture` for that
  reason.
- **Scene content is verified by eye.** The automated capture scenarios check
  PNG structure, dimensions against the success report, and cross-process
  determinism. No automated device test checks semantic scene content.
  Device-free tests pin the capture formats and channel conversion, but a
  structurally valid capture of the wrong rendered scene could still pass.
  Confirm the textured cube, dark blue-gray background, and correct channel
  order visually before accepting a replacement.

## `0.10-slerp-animated-cube.png`

The [AnimatedCube reference](0.10-slerp-animated-cube.png) after correcting
rotation playback to shortest-arc SLERP. Frame three selects a different pose
than the historical NLERP reference above; that image remains unchanged.

### Provenance

| Field | Value |
| --- | --- |
| SHA-256 | `1f02267cdbe0d9748f05b629e48d145d4c0aa172a8addaff071609859bedf708` |
| Size | 1,494,240 bytes |
| Image | 1600 x 1200, 8-bit RGBA, non-interlaced PNG |
| Swapchain format | `B8G8R8A8Srgb`, FIFO |
| Frame / recreations | Presented frame 3; zero recreations through capture |
| Source commit | `fb73ab8a1b2b3fd5f863e805b0dbaa1f7bc18fa2` |
| Capture date | 2026-10-01 |
| Host | Apple M2 Pro, arm64, macOS 26.6 build 25G72 |
| Build | Release, Xcode 27.0 build 27A266a, Apple clang 21.0.0 |
| API / driver | Vulkan 1.4; KosmicKrisp `vulkan-sdk-1.4.363 (git-269a955c89)` |
| Effective Vulkan headers | vcpkg VulkanHeaders 1.4.357.0 |
| `VCPKG_COMMIT` | `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` |
| Slang / stb ports | `2026.7.1#1` / `2024-07-29#1` |
| PNG encoder | stb defaults; compression level unchanged |
| Animation | SLERP; fixed 0.8-second smoke step |

### Reproducing it

Build the recorded source revision in Release, then run twice independently:

```sh
./build-benchmark/fireEngineTutorial \
    --smoke basic --capture <output.png> --capture-frame 3
shasum -a 256 <output.png>
```

Both recorded Release runs were byte-identical; Debug with synchronization
validation and shader-access tracking reproduced the same digest without
validation errors. The report must show 1600 x 1200 and zero recreations.
The nominal clip time is 0.4 seconds; use the ordinal, not a wall-clock delay.

### What it does not establish

This is the corrected animation checkpoint, not a replacement for the old
NLERP artifact or proof of shadow quality. There is no receiver plane in this
fixture. The platform, dependency, capture-only swapchain, and performance
limits of the historical reference apply here too. Byte equality remains a
strict requirement for repetitions of the same source and configuration;
a changed pose or shader needs its own explanation.

## `0.10-directional-shadow.png`

The accepted [directional-shadow reference](0.10-directional-shadow.png):
AnimatedCube casts onto a receive-only white plane and darkens its own
light-averted faces. Manual continuous-motion and contact review accepted it
for 0.10 with the grazing-angle limitation below. This is base-color
attenuation by shadow visibility, not normal-based lighting.

### Provenance

| Field | Value |
| --- | --- |
| SHA-256 | `30f9e7eada0b5a715204eace2a24befe5f830e72be35038a47fa06478aeeb136` |
| Size | 491,274 bytes |
| Image | 1600 x 1200, 8-bit RGBA, non-interlaced PNG |
| Swapchain format | `B8G8R8A8Srgb`, FIFO |
| Frame / recreations | Presented frame 5; zero recreations through capture |
| Source commit | `fb73ab8a1b2b3fd5f863e805b0dbaa1f7bc18fa2` |
| Capture date | 2026-10-01 |
| Host | Apple M2 Pro, arm64, macOS 26.6 build 25G72 |
| Build | Release, Xcode 27.0 build 27A266a, Apple clang 21.0.0 |
| API / driver | Vulkan 1.4; KosmicKrisp `vulkan-sdk-1.4.363 (git-269a955c89)` |
| Effective Vulkan headers | vcpkg VulkanHeaders 1.4.357.0 |
| `VCPKG_COMMIT` | `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` |
| Slang / stb ports | `2026.7.1#1` / `2024-07-29#1` |
| PNG encoder / animation | stb defaults unchanged; SLERP, fixed 0.8-second step |
| Shadow map | 1024 x 1024, `D32Sfloat` |
| Sampler | nearest comparison, less-or-equal, clamp-to-border, opaque white |
| Raster bias / receiver offset | constant 1.25, slope 1.75, clamp 0; subtract 0.0015 |
| Culling / viewport | back faces, counterclockwise front face, negative height |
| Attenuation | RGB multiplied by visibility remapped to [0.35, 1]; alpha unchanged |

### Reproducing it

Build the recorded source revision in Release, then run twice independently:

```sh
./build-benchmark/fireEngineTutorial \
    --smoke shadow --capture <output.png> --capture-frame 5
shasum -a 256 <output.png>
```

Two independent Release runs match exactly. Debug with two forward participants,
synchronization validation, and shader-access tracking reproduces the same
bytes without validation errors. All five smoke ordinals were captured twice
in Release and once in Debug sync; each trio matches. Require the reported
physical extent and zero recreations. Interactive review uses the executable
with no arguments; fixed-step stills alone do not establish continuous motion.

### What it does not establish

- **The interpolation correction.** Frame five is about 6e-8 seconds past the
  clip's first key and remains byte-identical under NLERP and SLERP despite
  their tiny pose difference; the SLERP reference above distinguishes them.
- **Uniformly clean grazing shadows.** Stable, deterministic breakup is
  concentrated within roughly +/-0.7 degrees around edge-on in the measured
  face-interior samples, not a strict whole-face bound. Neighboring-surface
  or background texel sampling is the supported explanation, not traced
  provenance. Normal-aware treatment is deferred beyond 0.10.
- **Cross-driver or fallback quality.** Visual evidence is KosmicKrisp with
  D32Sfloat, not D16 or other drivers. This scene does not exercise near/far
  rejection. Linux's 800 x 600 output is not compared to this Retina image.
- **Byte invariance under shader edits.** Semantically neutral edits can move
  encoded pixels on this toolchain. The visibility-one control uses a reviewed
  criterion replacing the failed silhouette-only rule: changes are at most
  one value per RGB channel with alpha unchanged, confined to textured
  surfaces with plane/background unchanged, and neutral load-order variants
  reproduce both images using the same executable. All three checks require
  fresh evidence; the final SLERP pair meets them with 70 pixels / 77 changed
  components. Counts alone never pass, and same-binary repeats remain exact.
- **Temporal correctness or performance.** A clean still does not prove slot
  selection, and capture adds transfer usage, a host wait, and file I/O.
  Content and moving contact were reviewed by eye, not a CTest pixel gate.
