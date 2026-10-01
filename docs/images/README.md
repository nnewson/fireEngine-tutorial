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

## Directional-shadow candidate (acceptance pending)

The first shadowed frame is reproducible. Its visibility-one control has a
**qualified acceptance**, not byte equality, under the explicitly reviewed
load-order amendment below. It failed the original silhouette-only criterion;
that failure is preserved rather than retrospectively called a pass.
Continuous review reported a rapid cube self-shadow transition. The bounded
pose diagnostic below finds stable grazing-angle breakup, not a reproduced
temporal reversal. That investigation is concluded on the exercised paths;
the visual limitation is not yet accepted. The header-selection repair now
passes locally, with cross-platform CI pending. Quaternion interpolation will
be corrected separately, and shadow quality then reviewed at matched
orientations before final acquisition. No
`0.10-directional-shadow.png` is added as an accepted reference yet. The
original AnimatedCube reference remains unchanged and still reproduces exactly
with current NLERP playback; the planned SLERP change requires an explicitly
reviewed new checkpoint, not an overwritten historical hash.

### Candidate provenance

| Field | Value |
| --- | --- |
| SHA-256 | `30f9e7eada0b5a715204eace2a24befe5f830e72be35038a47fa06478aeeb136` |
| Size | 491,274 bytes |
| Image | 1600 x 1200, 8-bit RGBA, non-interlaced PNG |
| Swapchain format | `B8G8R8A8Srgb`, FIFO presentation |
| Frame ordinal | 5 (one-based, presented frames) |
| Presentation recreations | 0 at the captured attempt |
| Source commit | `972e67062fb9ea20728a109d94bfff8f462c721c` |
| Capture date | 2026-09-30 |
| Host | Apple M2 Pro, arm64, macOS 26.6 build 25G72 |
| Build | Release, Xcode 27.0 build 27A266a, Apple clang 21.0.0 |
| API and driver | Vulkan 1.4, KosmicKrisp `vulkan-sdk-1.4.357.0 (git-6e2f85ffe3)` |
| `VCPKG_COMMIT` | `04a9d8e5212d01ee1dd9478eadd9caade4f8b0d4` |
| Slang port | `2026.7.1#1` |
| stb port | `2024-07-29#1` |
| PNG encoder | stb defaults; compression level unchanged |
| Shadow map | 1024 x 1024, `D32Sfloat` |
| Comparison sampler | nearest, less-or-equal, clamp-to-border, opaque white |
| Raster depth bias | constant 1.25, clamp 0.0, slope 1.75 |
| Receiver depth offset | 0.0015, subtracted after rejecting out-of-range depth |
| Shadow culling | back faces, counterclockwise front face, negative-height viewport |

The capture command is:

```sh
./build-benchmark/fireEngineTutorial --smoke shadow \
    --capture <candidate.png> --capture-frame 5
shasum -a 256 <candidate.png>
```

Two independent Release runs produced identical bytes. A Debug run with two
forward participants and both synchronization-validation settings reproduced
the same image without validation errors. Release itself has no validation
layer enabled. Every captured attempt reported zero recreations.

### Visual observations and deliberate controls

The five fixed-step captures described below show distinct cube poses with
corresponding cast-shadow silhouettes. The shadow falls behind and to the right
of the cube, contact has no obvious gap, and the lit faces show no obvious acne
at these sampled poses. The plane remains white outside the cast shadow. These
are still-image observations, not a completed continuous-motion check or an
exercised wrong-slot control. No bias, filtering, light, or culling adjustment
was made.

### Fixed-step animation sweep

The reviewer supplied five independent `--smoke shadow` captures on
2026-09-30, selecting ordinals one through five. The PNGs were independently
hashed and inspected, along with the reviewer's side-by-side montage. All
five originals are 1600 x 1200 RGBA PNGs. This audit did not rerun those GPU
captures. Frames two and five also match the earlier captures exactly.

| Presented ordinal | Nominal clip time (s) | SHA-256 |
| --- | --- | --- |
| 1 | 0.8 | `cf0c209dcd821af1b7e0f998f38d384d48d1ae085a13c03d5945ca6ab2dbc5ad` |
| 2 | 1.6 | `7216b992bb732898dc546800b2b1591bb97db98761467459ecca09d04ac27c81` |
| 3 | 0.4 | `cb26341040fa48fda2d5e7c1917ffa080bdfb3f63f5f52deee1f9981d85c100b` |
| 4 | 1.2 | `37adca7c8e40b89f9addb5c1bb6f5aee3dec73cbb1e91080f3e09f00089d7f7b` |
| 5 | 0.0 | `30f9e7eada0b5a715204eace2a24befe5f830e72be35038a47fa06478aeeb136` |

These are five evenly spaced phases of the two-second clip, visited in the
order above. Times are nominal decimal phases, not exact floating-point
values. The application advances animation before drawing, by 0.8 seconds per
loop attempt; the presented-ordinal mapping assumes no failed attempt or
recreation before capture. For reproduction, run the canonical command once
per ordinal in a fresh process with a distinct output path. Retain each
capture-success report, require zero recreations through that captured
attempt, and check the physical extent. The supplied sweep directory contains
images rather than run logs, so its recreation counts were not independently
audited from those files.

Frames one and three are nearly face-on; two, four, and five expose a more
rotated pose and a darker light-averted face. The visible shadow outlines
change with the corresponding cube poses. The montage juxtaposes unscaled
620 x 480 crops at (520, 330), left to right in ordinal order; it is an
inspection aid, not another reference image. The original PNGs remain the
full-frame evidence. This is a reproducible pose/alignment check, not proof
that every intermediate pose is artifact-free.

A wrong-slot control should use this fixed-step sequence rather than trying
to discern one interactive frame of lag. On a clean run, slots alternate
0, 1, 0, 1, 0. Forcing the sampled map to slot zero predicts stale shadows at
ordinals two and four, with odd ordinals serving as current-map controls.
That fault has not been exercised here; the correct sweep is its baseline.

The interactive check has a different purpose: observe continuous
motion as visible faces turn edge-on to the light, looking for acne, striping,
or unstable lit/shadowed transitions, and inspect contact as the cube's lower
edges sweep across the plane. Record it as a subjective observation, not a
proof of absent one-frame lag. The review below did not clear that check.

### Continuous-motion finding

The user inspected the Release demonstration and reported a rotating cube
with a rotating, clean-looking ground shadow, but intermittent on/off
self-shadowing on one cube face as it turned toward the camera. They also
noticed rotation slowing near the end of the cycle. These are user-observed
findings; at that point no capture had independently localized them. That
observation blocked the continuous-motion criterion; clean stills did not
override it. A separate contact-line verdict was not reported. The later
clarification and completed pose diagnostic below qualify the initial flicker
description without retroactively declaring this inspection clean.

The user clarified the spatial pattern: the face briefly switches from lit to
dark and back to lit, with no acne, bands, speckles, or other flickering
observed. Record this as a whole-face visibility reversal, not an acne finding.
The camera and light look from different directions, so a face turning toward
the camera is not necessarily turning toward the light. Neither that geometry
nor the absence of speckles establishes whether this brief reversal is correct;
the affected pose and its neighbors still need to be isolated.

During the subsequent probe, the user qualified the lit/dark/lit description:
it may have been the impression of shadowing visible for only a few frames,
not a confirmed reversal on one physical face. The controlled result below
must not be described as reproducing that original temporal sequence.

The completed interactive process presented 27,359 frames and retained two
shadow-map creations. Its log identifies KosmicKrisp
`vulkan-sdk-1.4.363 (git-269a955c89)`, newer than the candidate provenance
above, on Apple M2 Pro with a 1600 x 1200 FIFO swapchain and `D32Sfloat` maps.
This Release run is not validation evidence. Do not attribute the new visual
finding to the driver change without a controlled comparison.

Unchanged Release checkpoints were recaptured on that driver, both with zero
recreations: shadow frame five still gives `30f9e7ea...eb136`, and basic frame
three still gives `a8fff0f2...0297e` (the full hashes are recorded above).
These two checkpoints therefore survive the environment change; this does
not establish driver equivalence at unobserved poses or dismiss the flicker.
The new captures are local diagnostics under
`/tmp/fire-engine-5c-interactive.qCNT8n`, not accepted replacement references.

The animation has a separate, code-supported explanation for variable speed.
Its keys are at 0, 1, and 2 seconds, representing approximately successive
half-turns. Playback uses normalized linear quaternion interpolation, as
documented before the shadow work. This moves more slowly near keys and
faster between them: for an ideal half-turn, equally spaced quarter-interval
samples have angles 0, 36.87, 90, 143.13, and 180 degrees. Frame pacing was
not measured by this observation. glTF recommends spherical interpolation
for `LINEAR` rotation channels; changing playback belongs to a separately
reviewed animation correction, not an unrecorded shadow remedy. See the
[glTF interpolation specification][gltf-interpolation].

No bias, sampler, culling, shader, or animation change was made. The registered
next diagnostic was a finer fixed-step sequence around the affected pose,
followed by a held-pose check through both frame slots and reuse. It compared
the same physical face, not whichever face occupied the same screen region.
A stable held pose would support angular sensitivity on the exercised runs;
switching at an identical pose would instead require a temporal/resource-state
investigation. The result follows below. Interpolation was kept unchanged so
the diagnostic could not move the poses selected by the capture ordinals.

### Bounded pose diagnostic

Temporary application-only controls selected a capture's clip time and the
advance between preceding frames. Interpolation, shader modules, map extent,
sampler, culling, and bias remained unchanged. The controls were confined to
the existing five-frame shadow capture scenario, rejected failed presentation,
and required zero recreations. A moving run and a held run used exact binary
time fractions to reach the same pose without accumulated rounding differences.

The Release sweep sampled 64 poses over the two-second clip, then 42 finer
poses around the two grazing transitions in its first half. Fine spacing was
1/512 second, about 1.95 ms; animation between submitted frames advanced by
1/128 second. The sampled face interiors showed dark-to-lit transitions as the
same physical face turned toward the light, not a lit/dark/lit reversal.
Full-resolution images reveal substantial jagged/triangular shadow patches
within the narrow grazing interval, which the five original stills missed.

The following sparse face measurements track fixed object-space points,
projected with the current model and camera matrices. They average linear-light
RGB over a 15 x 15 grid spanning the inner 70% of each face, with a 3 x 3 pixel
neighborhood per point. They are diagnostics, not a whole-face pixel assertion
or a new acceptance tolerance. The normal/light dot product is CPU geometry
analysis only; the shader still has no normal-based lighting term.

| Pose (s) | Object-space face | Normal dot light | Mean linear RGB |
| --- | --- | --- | --- |
| 0.28906250 | +X | -0.0085626 | 0.1680144 |
| 0.29296875 | +X | +0.0001693 | 0.3224090 |
| 0.29687500 | +X | +0.0089495 | 0.4760854 |
| 0.70312500 | -Z | -0.0089495 | 0.1683965 |
| 0.70703125 | -Z | -0.0001693 | 0.3234226 |
| 0.71093750 | -Z | +0.0085625 | 0.4759409 |

Each row's pose was then held for independent captures at ordinals one through
five. All five PNGs for a given pose were byte-identical, including both slots
and their reuse, and matched the moving capture at that pose. Debug captures
at ordinals four and five, with both synchronization-validation settings,
matched Release exactly and emitted no validation errors. This establishes
stability on those exercised paths, not a proof excluding every timing defect.

The dark-to-lit change occurs within approximately eight milliseconds of
animation in each sampled interval, shorter than one 60 Hz frame. That is
consistent with an abrupt visual impression, but does not independently
reproduce what the user saw. The jagged breakup is spatial and stable at the
tested poses. These tests do not isolate how much comes from map resolution,
nearest sampling, or bias; no parameter was changed to obtain a cleaner image.
Review this explicit visual limitation before accepting the reference.

**SDK build caveat.** The first Debug probe aborted before rendering on a
Vulkan-Hpp header-version assertion. Compiler search-order inspection showed
the new `/usr/local/include` Vulkan headers (363) ahead of the pinned vcpkg
system include path (357). Clean Debug and Release builds removed mixed-version
objects; they did not enforce the intended header pin. Both shader-module
hashes stayed unchanged. Subsequent validation evidence uses the clean build.
Header precedence needs a separate build-system follow-up, not a shadow fix.
All 136 Release captures (64 coarse, 42 fine, 30 held) were then reacquired
with the clean build and required to match their earlier hashes; all did.
The 12 clean Debug captures likewise matched the corresponding Release images.

The temporary application controls have been removed and `main.cpp` matches
its pre-probe SHA-256 exactly. No rendering or animation change is retained,
and the candidate has not been accepted by this diagnostic.

Local captures, per-run logs, face measurements, temporary source patch, and
reproduction scripts are under `/tmp/fire-engine-5c-pose.QplhmM`. These remain
scratch diagnostics, not a permanent test harness or accepted image assets.

### Header-selection repair checkpoint

The 2026-10-01 build-only draft restores the pinned 1.4.357 headers while
leaving KosmicKrisp `vulkan-sdk-1.4.363 (git-269a955c89)` installed. Compiler
traces verify the C, C++, and RAII headers come from vcpkg for all four
first-party targets in Debug and Release. A generated version guard fails for
each target when only the search-order repair is removed from its recorded
compilation command. No warning suppression, animation, or shadow change is
part of this repair. Cross-platform CI is still pending.

The repair changes search order, not just the number of entries: Clang drops
the driver's ordinary `-I` entry when `-idirafter` classifies the same directory
as system. The configure-time scan and compile-time version guard are separate
protections; a matching version alone cannot establish file provenance. The
lookup problem may have existed with a matching 1.4.357 SDK before the update,
but that SDK's installation path and earlier search order were not verified.

After clean rebuilds, two independent Release captures of each scene and a
Debug synchronization-validation capture reproduce the existing full hashes:
basic frame three `a8fff0f2...0297e`, shadow frame five `30f9e7ea...eb136`.
Both synchronization settings are enabled; the shadow Debug capture forces two
forward participants. All six report 1600 by 1200 and zero recreations, with
no validation errors. Both SPIR-V hashes are unchanged. These results establish
the pre-animation comparison point, not final visual acceptance or a general
cross-driver guarantee.

Evidence is under `build-evidence/header-selection-20261001`. The three earlier
scratch directories were copied to `build-evidence/shadow-review-20261001` and
compared file-for-file, with 648 SHA-256 entries in the adjacent manifest.
These are ignored local evidence copies, not committed reference assets or
off-machine backups.

### Planned acceptance sequence

The bounded investigation is complete, but the image remains a candidate.
The reviewed follow-up sequence keeps environment, animation, and shadow
changes independently attributable:

1. Repair effective Vulkan header selection without changing the dependency
   pin or installed driver. Prove the selected paths, including with the
   competing SDK present, then clean-rebuild and repeat the current NLERP
   image checkpoints before changing playback.
2. Correct quaternion rotation to SLERP with device-free angular-pacing and
   playback tests. Keep shadow settings unchanged and acquire a repeatable
   comparison baseline, including the five fixed-step poses. Equal ordinals
   keep their nominal clip times but can now represent different rotations.
3. Revisit the grazing-angle limitation at the same model transforms and in
   continuous motion. Changing time spent at a pose is not a shadow repair.
   Accept the named limitation explicitly or review one bounded experiment
   before changing shadow parameters; no unregistered tuning sweep.
4. Retry final reference acceptance on the reviewed configuration, including
   repeated Release bytes, Debug synchronization validation, visual controls,
   and a fresh continuous-motion/contact inspection.

The local evidence copies and header-repair draft are now recorded above.
Animation and shadow-quality follow-ups remain unimplemented; the original
`/tmp` paths must not be treated as archival storage.

The original `0.10-step-0-animated-cube.png` and its provenance stay unchanged.
If the reviewed SLERP basic capture differs, a separately named
`0.10-slerp-animated-cube.png` will become the new basic checkpoint. The final
shadow reference remains `0.10-directional-shadow.png`, added only after
acceptance. Record interpolation and effective header paths/version alongside
driver and dependency provenance. Same-binary determinism remains byte-exact.
Visibility-one and later wrong-slot controls must use the new poses, not
silently compare against an old NLERP sequence. The 27-pixel load-order
qualification below is specific to its historical pair, not permission for
new differences after the animation correction.

### Deliberate visual controls

- **Visibility one:** the cast shadow and cube darkening disappear, but the
  receiver image is not byte-identical to its historical unshadowed state.
  Its SHA is `ca1d227307a9c7d79733518ee5072ecd337d5aa52bd57359d3dcc37b41a93389`,
  reproduced in a second process. Against historical SHA
  `bb65759bd71495dce90b65a8105e1bfd004fd32f54608ca530c9f9b0600cb5bd`,
  decoded RGBA differs at 27 pixels, maximum channel delta 1, with no alpha
  changes. The inclusive difference bounds are x=844..978, y=416..743.
  Differences occur inside surfaces, including (903, 533) and (941, 578),
  not only within one pixel of the silhouette. There are 30 changed RGB
  components: three pixels change two channels, all by at most one. This
  failed the original criterion; its qualified acceptance is explained below.
- **Wrong V sign:** replacing `0.5 - y * 0.5` with `0.5 + y * 0.5` produces
  misplaced dark triangles across the cube and a shadow extending to its
  left/front. The orientation check visibly rejects it.
- **Black border:** changing only opaque white to opaque black darkens the
  uncovered front and left receiver regions while leaving the in-frustum
  cast shadow intact. This exercises real border sampling, not just sampler
  construction. Restoring the sampler restores the candidate bytes.

The visibility-one mismatch survived separate diagnostics restoring the old
camera-position expression, removing the light-coordinate varying, and
returning the base color directly. The visibility-one SPIR-V really multiplies
RGB by exactly 1.0; this is not an approximate `lerp` endpoint. Using the exact
pre-sampling shader from `eeb384b` in the current executable reproduces the
historical receiver hash. That localizes the difference to the shader change,
but those initial diagnostics did not isolate its cause. The subsequent
one-edit-at-a-time bisection does, to the extent described below.
All temporary edits were restored; the original shadowed candidate reproduced.

### Reviewed visibility-one amendment

The 2026-09-30 review started from the original shader at `29f25a1` and added
one change at a time. The retained sources, SPIR-V, and PNGs were independently
inspected and hashed; all six modules pass `spirv-val --target-env vulkan1.4`.
The captures were supplied by the reviewer, not newly rerun for this audit.

| Change from original shader | Receiver PNG prefix | SPIR-V prefix |
| --- | --- | --- |
| None | `bb65759b` | `b6f90865` |
| Declare unused binding two | `bb65759b` | `b6f90865`, identical |
| Store fragment base color, then return it | `bb65759b` | `f899f2c4` |
| Add and output light-space varying | `bb65759b` | `acd45aec` |
| Store world position before projection | `ca1d2273` | `fe6e2de7` |
| Same, but load view-projection first | `bb65759b` | `97624a24` |

The decisive SPIR-V digests are:

```text
original:   b6f9086542f255f5e85af0308801db4696f736e61d97af7ad7695742644cd090
world-first: fe6e2de74a5852fe046f9c9ca81e9ebbdcc791b46a13bd0e2097895e33b79bc6
view-first:  97624a243f94fa3f1a6ac5fef408516a48af1644dfffcbfd37012b55fdb1ce55
```

Both vertex programs multiply position by model, then by view-projection.
After accounting for identifier, declaration, and debug-name ordering, the
operative instruction change is when the independent view-projection load
occurs. The matrices, arithmetic, layouts, and interpolants have the same
meaning. Moving that load earlier in the otherwise identical world-position
variant restores the historical PNG. Neither variant samples shadows.

This demonstrates sensitivity to a semantically neutral shader/load-order
change on this driver. It does not identify the downstream translation,
instruction scheduling, contraction, or interpolation behavior responsible.
Small vertex-position differences can affect interpolated texture coordinates
inside triangles, not only their silhouettes; that explains why the original
spatial rule was too restrictive, but those intermediate values were not
measured here. No specific driver-internal mechanism is claimed as proved.

The explicit amendment accepts this particular visibility-one result because:

1. Its differences are bounded and recorded: 27 pixels, maximum component
   delta one, unchanged alpha, fixed bounds, and byte-stable repeated captures.
2. The independently inspected load-order variants reproduce both exact PNGs
   without changing the arithmetic or introducing shadow sampling.

This is weaker than byte equality, not a general one-unit pixel tolerance.
New differences require their own diagnosis and review. The original failure
remains part of the evidence. Same-shader determinism, the historical
pre-sampling check, and the basic frame-three hash stay strict for this NLERP
comparison. The planned SLERP checkpoint transition above changes the selected
poses, not the strictness of repeatability within either configuration.
The production shader is not reordered to recover one driver's old output;
the load-first experiment was diagnostic and has not been established as a
cross-toolchain policy or as a fix with the complete shadow shader present.

### Reproducible patch measurements

The following measurements compare the shadowed candidate to its visibility-one
control at the **same pixel rectangles**, avoiding texture differences between
faces. Coordinates have a top-left origin; x1 and y1 are exclusive. Decode each
8-bit RGB channel `s = byte / 255` to linear light using `s / 12.92` for
`s <= 0.04045`, otherwise `((s + 0.055) / 1.055)^2.4`. Average all three decoded
channels over the rectangle, exclude alpha, then divide the two means. This is
an equal-weight RGB mean, not a luminance-weighted measurement.

| Patch | Rectangle (x0, y0, x1, y1) | Shadowed mean | Visibility-one mean | Ratio |
| --- | --- | --- | --- | --- |
| Plane, shadowed | (1000, 670, 1040, 710) | 0.351532600 | 1.000000000 | 0.351532600 |
| Plane, lit | (1050, 850, 1090, 890) | 1.000000000 | 1.000000000 | 1.000000000 |
| Cube, light-averted | (900, 540, 940, 580) | 0.165214402 | 0.472033122 | 0.350005952 |
| Cube, lit | (670, 540, 710, 580) | 0.415491388 | 0.415491388 | 1.000000000 |
| Cube, top | (760, 420, 800, 450) | 0.456613124 | 0.456613124 | 1.000000000 |

The shadowed plane is uniformly RGB (160, 160, 160), consistent with 0.35 after
sRGB quantization. The cube measurement supports the intended self-shadow
attenuation without assuming that different faces have identical textures.
For the border control, rectangle (1000, 1150, 1040, 1190) changes from linear
mean 1.0 to 0.351532600. These are manual diagnostics, not new pixel-based CTest
gates, and do not substitute for the separately reviewed historical-image
comparison.

### Remaining limits

On this driver, a semantically neutral shader edit can change byte-exact
pixels. Hashes remain strict checks for repeat runs of the same shader and
configuration; the Debug and Release forward modules here are byte-identical.
A changed hash after a shader edit calls for diagnosis, not an automatic claim
of a semantic rendering change or an automatic waiver. The effect is
scene-dependent: this reordering preserved AnimatedCube's frame-three hash
while changing the receiver's 27 pixels. Quantization boundaries can expose
small differences in one scene but not another; their exact upstream cause
remains unmeasured here.

The D16 fallback, other drivers, and near/far rejection have no visual evidence
from this scene. The receiver lies inside the registered depth range even
where it extends beyond the XY frustum. Fixed frustum coverage, hard edges,
finite map resolution, and base-color attenuation without normals remain
intentional limitations. Capture-specific swapchain usage and host waits have
the same scope limits as the original reference. The Linux framebuffer extent
differs; its PNG must not be compared directly to this Retina candidate.

[gltf-interpolation]: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.pdf
