# Host rebaseline tooling

These Python 3.10+ tools require no extra Python packages. They do not change
renderer code, choose a performance policy, install drivers, or set CPU governors.

## Laptop Lavapipe preflight

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

## Acquire one reviewed measurement session

Use `run_matrix.py` only after the tooling revision has been reviewed, committed
and correctness-qualified. Rebuild the current source in Release first. An
updated checkout does not update an old executable. The runner requires a clean
tracked worktree, a matching Release CMake build tree, and both compiled shaders;
it records the executable, shader, compiler, dependency and source identities
before and after each arm. Sanitizer build flags and inherited validation,
layer-path, tracing or sanitizer overrides are rejected rather than silently
removed. Normal driver settings and ordinary implicit-layer policy are preserved.

Select the role and one installed ICD explicitly:

| Role | Driver required in the app report | Use |
| --- | --- | --- |
| `laptop-nvidia` | NVIDIA device and driver | Decision-bearing hardware session |
| `laptop-lavapipe` | llvmpipe/Lavapipe | Decision-bearing software session |
| `hosted-lavapipe` | llvmpipe/Lavapipe | Observational hosted session |
| `mac-kosmickrisp` | KosmicKrisp | Observational Mac session |

Only the Mac role permits omitting `--icd` for its normal installed-driver
selection. Identity comes from the application report, not from the requested
filename. Selecting the NVIDIA ICD does not mean that presentation bypasses
the integrated GPU. Record that route separately, or state "not established".

Prepare an operator-conditions file before each session:

```sh
mkdir -p build-evidence
cp tools/benchmark/conditions.example.json build-evidence/nvidia-conditions.json
```

Edit the copy: confirm AC power, the declared `cpu_governor` (`performance` for
the laptop sessions), and replace both `REPLACE` entries with actual
background-load and cooldown conditions. Use `not established` for the governor
on a platform without this interface. The runner rejects an unedited
template. For display routing and active implicit layers, an explicit
"not established" is acceptable; available manifests and X11 providers are
context, not proof of which layers loaded or of a PRIME copy. Nothing attempts
to repair or reconfigure the machine. Keep power/governor and other settings
unchanged throughout the run; a declared condition is not an automated proof.

From the normal Linux desktop terminal, run NVIDIA first. Adjust the manifest
and build-directory names to the installed files:

```sh
python3 tools/benchmark/run_matrix.py \
  --role laptop-nvidia \
  --icd /usr/share/vulkan/icd.d/nvidia_icd.json \
  --build-dir build-benchmark \
  --conditions build-evidence/nvidia-conditions.json
```

After a recorded idle/cooldown interval, make a separate conditions file and
run Lavapipe with the **same unchanged Release binary**:

```sh
python3 tools/benchmark/run_matrix.py \
  --role laptop-lavapipe \
  --icd /usr/share/vulkan/icd.d/lvp_icd.json \
  --build-dir build-benchmark \
  --conditions build-evidence/lavapipe-conditions.json
```

The desktop windows are unattended and bounded; do not resize/minimize them or
start competing work. No stopwatch or visual review is required. Each child has
a 20-minute timeout. The output directory is always new; no resume, overwrite
of an earlier session, or automatic retry after a crash/timeout is supported.
Retain an incomplete run and review the cause before starting another session.

### Fixed sequence and replacement rule

Two automatic-policy qualification runs (1,000 and 10,000 draws, selecting one
and two participants) precede this nine-arm matrix:

| Order | Draws | Forward mode | Role |
| --- | --- | --- | --- |
| 1 | 1 | forced one | F1 reset estimate |
| 2–4 | 1,000 | one / two / one | A / X / B |
| 5–7 | 10,000 | one / two / one | A / X / B |
| 8 | 1,000 | direct primary | F0 / attribution |
| 9 | 10,000 | direct primary | F0 / attribution |

For each workload, primary `A_phase` means give control `C=(A+B)/2`, drift
`D=abs(B-A)` and signed improvement `C-X`. A direction resolves only if
`abs(C-X)>D`. After all nine initial arms, each unresolved primary gets exactly
one replacement A/X/B, 1,000 before 10,000 if both qualify. A resolved regression
is retained, not retried. The secondary `F_phase` result never schedules or
prevents a retry. The whole operation stays in one sitting; a new hosted job
is a new session, not a replacement arm.

A valid replacement supplies **every** A/X/B-derived quantity for that workload:
both comparisons, model inputs, shadow cost and measured-pass overlap bounds.
The original remains void/context. If the replacement is unresolved too, use
its numbers descriptively but leave the cell unclassified. If it fails, there
is no fallback to the original. The original F1 and workload-matched direct
arms remain fixed ancillary inputs; they are not selected after seeing values.

### Failed sessions: authority and visibility

After an invalid arm stops a session, retain the entire directory and review the
cause before acquiring again. The **next complete session is authoritative for
every cell** on that implementation; no earlier cell can be carried into it.
Do not rerun an already complete session for a more favorable result.

Failed sessions still contribute visible context. Partial replay checks the
ordered, accepted prefix and the integrity of every sealed artifact, including
the terminal failure's files. It lists both comparisons for every completed
original/replacement triplet, marked **non-authoritative**, with no final model
or selected-cell result. In the Stage 7 interpretation, list these classifications
beside the authoritative session so an earlier regression cannot disappear.
Acquisition history and selection across session directories remain an explicit
review step; the tool does not choose among multiple complete sessions.

The launcher seals partial artifacts on handled failure/interruption and attempts
to write `partial-results.json` and `partial-results.md`. An artifact that was
never produced is recorded as absent; that arm cannot supply a report. Changed
or missing sealed evidence is rejected, not reinterpreted as an acquisition
failure. An abruptly killed launcher may leave an unsealed `running` ledger;
that needs manual evidence review and cannot pass partial replay automatically.

Replay a sealed failed session without resuming or modifying it:

```sh
python3 tools/benchmark/analyze_matrix.py build-evidence/matrix-ROLE-SESSION \
  --partial --format markdown
```

Omit `--format markdown` for JSON. Ordinary replay still rejects a failed session.

### Evidence and offline replay

The printed directory contains:

- `session.json`: ordered attempts, commands, UTC times, statuses, identities,
  inherited/effective environment and retry reasons, including failures.
- Per-arm stdout **and stderr**, even when empty; plus before/after and one-second
  telemetry in `*.telemetry.jsonl`. Preserve empty files when transferring.
- `build-provenance.json`: CMake/compiler configuration, installed vcpkg versions,
  manifests and selected ICD text. The executable and both SPIR-V hashes are
  recorded; binaries are not copied. Hashes/checkout metadata cannot attest that
  a binary was built from that source; rebuilding remains the operator's job.
- `results.json` and `results.md` for a complete, replay-validated session.
- Partial results for an integrity-checked failed session, or
  `partial-replay-error.txt` explaining why they could not be produced.

Environment provenance includes `LP_NUM_THREADS`, `MESA_*`, `GALLIVM_*`, ICD,
PRIME and display-selection variables. Unset and empty are distinct. Other
environment variables are not dumped. Linux telemetry retains raw cpufreq
governors/frequencies (kHz), thermal readings (millidegrees Celsius where provided
by these interfaces), throttle counters, load averages, `/proc/stat` counters and
power-supply state. Sensor coverage varies; unavailable readings are recorded,
not treated as "no throttling". Sampling cannot prove absence of brief throttling.
Mac lacks these Linux interfaces; its missing readings remain explicit.

The Markdown report summarizes observed governors by policy and AC state for
each arm beside the declarations. Known governor/power changes within or between
arms, or a contradiction of the declared values, stop acquisition and are also
rejected on ordinary offline replay. Partial replay retains the condition flags.
Unavailable readings are shown, not treated as stability or a contradiction.
AC is inferred only from supplies reporting type `Mains` and their `online`
values: an unrelated USB device or battery status does not establish AC power.

`scaling_cur_freq` can describe the last requested P-state rather than the
hardware's actual clock; `cpuinfo_cur_freq`, when available, is the hardware
reading. Keep those fields distinct, not a single asserted effective frequency.
See the [kernel CPUFreq documentation](https://docs.kernel.org/admin-guide/pm/cpufreq.html).

Transfer the **entire** directory, then replay without a GPU or application:

```sh
python3 tools/benchmark/analyze_matrix.py build-evidence/matrix-ROLE-SESSION
```

This prints freshly derived JSON and never launches a command from the ledger
or rewrites evidence. Decimal phase values are JSON strings to preserve printed
precision. Replay checks log/telemetry/build-provenance hashes, exact arm order,
commands, participant/workload/sample counts and consistent driver/presentation
metadata. It recomputes the replacement decision; cached analysis is not trusted.

### Accounting and limits

`A_phase` is the existing eleven-component sum: snapshot (three rows), shared
uniform update, shadow reset/recording, forward coordinator reset/secondary region/
primary recording/secondary execution, and submission. `S_phase` is the two
shadow rows; `F_phase` is those four forward rows. Participant sums, critical
path and join/tail diagnostics overlap and are not added again. Direct mode's
empty secondary-pool reset still belongs to its secondary region. Wait/present
rows are context, not GPU times, and process elapsed time is not frame time.

All phase rows (mean, median and p95) are preserved, but sums and models use
means. Share checks propagate each row's 0.0005 us rounding bound and the printed
percentage's 0.005-point bound; these are formatting bounds, not performance
tolerances. Participant aggregate checks add means, never medians or percentiles.

The model keeps F0/F1 sensitivity and reports inverted brackets, negative
variable-reset work or invalid p rather than clamping. The pass-overlap bound
is `min(mean(S),mean(F))`, potentially looser than the mean of per-frame minima.
It excludes untimed selection/job/timing-merge bookkeeping and prices no new
coordination. It is not a measured parallel speedup or the total cost of shadows.
The complete participant overlap relation is reported separately from the
scheduler-sensitive reset-only diagnostic. Failure to observe overlap at the
forced-two workload requires review, not an extra timing retry.

Neither an unresolved cell, an observational platform, nor a favorable secondary
comparison can supply a missing positive primary result. Review both laptop
sessions, raw phase tables, conditions and limits before making the Stage 7
policy decision. No production threshold or CI timing gate is changed here;
hosted workflow integration is a separate follow-up after laptop acquisition.
