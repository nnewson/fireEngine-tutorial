# Laptop Lavapipe preflight

Before building the Stage 7 measurement runner, qualify the installed laptop
Lavapipe driver. This helper runs only `vulkaninfo --summary` and the actual
application's `--benchmark 1`; it does not acquire a performance baseline.

On the Linux laptop, check out the revision containing this helper and build
the current application in Release. Use the repository's existing vcpkg setup
and a C++23 compiler/library (CI uses GCC 14):

```sh
cmake --preset vcpkg -B build-benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build build-benchmark --target fireEngineTutorial
```

Run from your normal graphical desktop terminal, without `sudo`. Locate the
installed Lavapipe manifest, typically under `/usr/share/vulkan/icd.d`:

```sh
ls /usr/share/vulkan/icd.d/*lvp*.json
```

Choose the manifest for the build's architecture, not an Intel or NVIDIA one.
Replace the example path below if your distribution uses a different filename:

```sh
python3 tools/benchmark/preflight_lavapipe.py \
  --icd /usr/share/vulkan/icd.d/lvp_icd.x86_64.json
```

The helper requires Python 3.10 or later and an installed `vulkaninfo`. Each
invocation has a 60-second timeout. It opens one bounded application window;
leave it alone until it exits. No stopwatch, resizing or visual verdict is
needed. No packages are installed and no driver, power or system settings
are changed. Missing prerequisites or a timeout are failures to qualify, not
permission to upgrade Mesa or silently substitute a hosted CI machine.

Both child processes receive the same single manifest through `VK_DRIVER_FILES`
and the older `VK_ICD_FILENAMES`. With both present, the newer variable takes
precedence; matching values also cover an older system loader. Other settings
are preserved, including Mesa thread limits and Optimus selection. See the
[Vulkan loader's selection contract](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderDriverInterface.md#overriding-the-default-driver-discovery).

The helper checks physical-device Vulkan 1.4 support in the inventory and
Lavapipe identity in both outputs. It requires a completed Release one-draw,
two-pass application report and successful process exits. The application
itself tests its actual surface, swapchain-maintenance and device requirements;
system `vulkaninfo` can use a different loader and cannot establish those alone.
This is not a full benchmark-report parser, layer-activation audit or measurement
qualification: discarded attempts and inherited instrumentation are retained,
not silently cleaned up for a timing claim.

Return the printed `build-evidence/lavapipe-preflight-...` directory, containing
stdout/stderr for both invocations and `summary.json`. Even failures retain their
partial logs. The summary records commands, statuses, executable/manifest hashes,
checkout revision/status and selected environment variables (unset differs from
empty). Unrelated environment variables are not copied. A checkout revision is
not proof of a binary's source; the preceding build remains required.

Driver settings are recorded, not rejected. For later NVIDIA measurements,
Optimus variables and GPU identity alone will not establish whether presentation
uses a PRIME copy through the integrated GPU; that route needs separate system
evidence or an explicit "not established" entry. No topology verdict is made by
this Lavapipe-only preflight.

Device-free checks for the helper:

```sh
python3 -m unittest discover -s tests/benchmark -v
```
