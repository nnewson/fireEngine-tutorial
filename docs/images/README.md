# Reference images

Committed images are evidence, not decoration. Each one records a rendered
state that later work must be able to reproduce or deliberately supersede.

## `0.10-step-0-animated-cube.png`

The AnimatedCube scene as it rendered immediately before shadow work began. It
is the visual baseline for 0.10: a rendering change that alters this image is
either the intended effect of a shadow feature or a regression, and nothing in
between.

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
