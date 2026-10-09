"""Strict parsing and rounded arithmetic for the unchanged two-pass report."""

import re
from decimal import Decimal

D = Decimal
HALF_US = D("0.0005")
HALF_PERCENT = D("0.005")
SNAPSHOT = ("transform update", "draw-list build", "frame recording-input build")
SHADOW = ("shadow command-pool reset", "shadow primary command recording")
FORWARD = (
    "forward coordinator command-pool reset",
    "forward secondary recording region",
    "forward primary command recording",
    "forward secondary command execution",
)
ACTIVE = SNAPSHOT + ("frame-uniform update",) + SHADOW + FORWARD + ("queue submission",)
RESET = "forward participant command-pool reset"
RECORD = "forward secondary command recording"
SUM_RESET = "forward participant pool reset (summed CPU)"
SUM_RECORD = "forward secondary recording (summed CPU)"
CRITICAL = "forward participant-region critical path"
RESET_SPAN = "forward participant reset-region span"
REGION = "forward secondary recording region"
EXECUTE = "forward secondary command execution"
COMPLETION = (
    "Completion acquired by polling",
    "Completion required a blocking wait",
    "Completion observed at the deadline boundary",
)
ROLES = ("laptop-nvidia", "laptop-lavapipe", "hosted-lavapipe", "mac-kosmickrisp")


class MeasurementError(ValueError):
    """An incomplete or incompatible acquisition cannot supply a comparison."""


def require(condition, message):
    if not condition:
        raise MeasurementError(message)


def one(text, pattern, description):
    matches = re.findall(pattern, text, re.MULTILINE)
    require(len(matches) == 1, f"expected exactly one {description}")
    return matches[0]


def field(text, label):
    return one(text, r"^  " + re.escape(label) + r": (.+)$", label)


def phase_names(mode):
    names = list(SNAPSHOT + ("frame-uniform update",) + SHADOW + (FORWARD[0],))
    names += [SUM_RESET, SUM_RECORD] if mode == "two" else [RESET, RECORD]
    names.append(REGION)
    if mode == "two":
        names += [
            CRITICAL,
            RESET_SPAN,
            "forward completion join wait",
            "forward completion tail",
            "forward helper work remaining at join",
        ]
        for participant in range(2):
            names += [
                f"forward participant {participant} {suffix}"
                for suffix in ("pool reset", "recording", "reset start offset")
            ]
    return (
        names
        + list(FORWARD[2:])
        + [
            "queue submission",
            "frame-fence wait",
            "image-acquisition wait",
            "presentation-fence wait",
            "presentation call",
        ]
    )


def total(means, names):
    return sum((means[name] for name in names), D(0))


def sum_interval(means, names):
    return (
        sum((max(D(0), means[name] - HALF_US) for name in names), D(0)),
        sum((means[name] + HALF_US for name in names), D(0)),
    )


def check_share(means, names, printed, label):
    low, high = sum_interval(means, names)
    active_low, active_high = sum_interval(means, ACTIVE)
    require(active_low > 0, "active work is too small to resolve at printed precision")
    lower = 100 * low / active_high
    upper = 100 * high / active_low
    # Conservative interval propagation, not a free percentage tolerance.
    require(
        printed + HALF_PERCENT >= lower and printed - HALF_PERCENT <= upper,
        f"{label}: inconsistent with rounded component means",
    )


def shares_for(mode):
    shares = {
        "Snapshot share of measured active work": SNAPSHOT,
        "Queue-submission share of measured active work": ("queue submission",),
        "Shadow-pass share of measured active work": SHADOW,
    }
    if mode != "direct":
        shares.update(
            {
                "Forward secondary-execution share of measured active work": (EXECUTE,),
                "Forward secondary-recording region share of measured active work": (
                    REGION,
                ),
                "Current serial share outside that region": tuple(
                    n for n in ACTIVE if n != REGION
                ),
            }
        )
        if mode != "two":
            shares["Forward participant-pool-reset share of measured active work"] = (
                RESET,
            )
    return shares


def quantities(means):
    return {
        "snapshot_us": total(means, SNAPSHOT),
        "S_phase_us": total(means, SHADOW),
        "F_phase_us": total(means, FORWARD),
        "A_phase_us": total(means, ACTIVE),
    }


def parse_report(stdout, stderr, draws, mode, role):
    """Parse one admitted run; 'auto' is for qualification, never an A/X/B arm."""
    require(role in ROLES, "unknown session role")
    require(mode in ("one", "two", "direct", "auto"), "unknown recording mode")
    require(draws in (1, 1000, 10000), "unregistered workload")
    combined = stdout + "\n" + stderr
    for marker in (
        "Vulkan validation error:",
        "WARNING: ThreadSanitizer",
        "Test Output for this test has been truncated",
        "[This part of the test output was removed",
    ):
        require(marker not in combined, f"inadmissible output: {marker}")
    one(stdout, r"^Phase-level CPU benchmark$", "benchmark report")
    one(stdout, r"^Presented 80 frames\.$", "completed presentation sequence")
    one(
        stdout,
        r"^  Forward draw bindings are cached independently inside each command buffer\.$",
        "report footer",
    )
    require(
        field(stdout, "Build configuration") == "Release", "measurement must be Release"
    )
    device = field(stdout, "Device")
    driver = field(stdout, "Driver")
    if "lavapipe" in role:
        require(
            re.fullmatch(r"(?:llvmpipe|lavapipe) \(.+\)", driver) is not None,
            "requested Lavapipe role ran another driver",
        )
    elif role == "laptop-nvidia":
        require(
            "nvidia" in driver.lower() and "nvidia" in device.lower(),
            "requested NVIDIA role ran another driver/device",
        )
    else:
        require(
            driver.startswith("KosmicKrisp ("), "requested Mac role ran another driver"
        )
    require(
        one(stdout, r"^Selected Vulkan 1\.4 device: (.+)$", "startup device") == device,
        "startup/report device disagreement",
    )
    require(
        one(stdout, r"^Driver: (.+)$", "startup driver") == driver,
        "startup/report driver disagreement",
    )
    require(
        field(stdout, "Workload")
        == f"instances={draws}, nodes={draws + 1}, draws={draws}",
        "wrong workload",
    )
    require(
        field(stdout, "Recorded packets per frame")
        == f"shadow={draws}, forward={draws}",
        "wrong two-pass packet counts",
    )
    require(
        field(stdout, "Frames") == "16 warm-up, 64 measured, 0 discarded attempts",
        "incomplete or discarded frame sequence",
    )
    require(
        field(stdout, "Fixed animation step") == "0.016667 seconds",
        "changed synthetic step",
    )
    path = field(stdout, "Forward recording path")
    participant_lines = re.findall(
        r"^  Forward secondary recording participants: (.+)$", stdout, re.MULTILINE
    )
    if mode == "direct":
        require(
            path == "direct primary command buffer" and not participant_lines,
            "direct-primary arm did not use the direct path",
        )
    else:
        require(
            path == "secondary command buffer" and len(participant_lines) == 1,
            "secondary arm has wrong path/participant report",
        )
        count = 2 if mode == "two" or (mode == "auto" and draws == 10000) else 1
        expected = (
            f"automatic at or above 5000 draws per participant, {count} effective"
            if mode == "auto"
            else f"{count} forced, {count} effective"
        )
        require(
            participant_lines[0] == expected,
            "wrong forced/automatic or effective participants",
        )
    effective_mode = ("two" if draws == 10000 else "one") if mode == "auto" else mode

    swapchain = one(
        stdout,
        r"^Swapchain created: (\d+) images at (\d+)x(\d+) \(([^,]+), ([^)]+)\), (\d+) presentation semaphores\.$",
        "swapchain",
    )
    images, width, height, color, present, semaphores = swapchain
    require(
        int(images) > 0 and images == semaphores and int(width) > 0 and int(height) > 0,
        "invalid swapchain geometry/counts",
    )
    require(
        field(stdout, "Presentation") == f"{width}x{height}, {color}, {present}",
        "mixed presentation metadata",
    )
    slots = field(stdout, "Frames in flight")
    require(slots == "2", "changed frame-slot count")
    forward_format = one(
        stdout, r"^Forward depth format: (\w+)\.$", "forward depth format"
    )
    shadow = one(
        stdout,
        r"^Shadow maps: (\d+) at (\d+)x(\d+), format (\w+), (\d+) created\.$",
        "shadow maps",
    )
    require(
        shadow[:3] == (slots, "1024", "1024") and shadow[4] == slots,
        "changed shadow resources",
    )
    require(
        forward_format in ("D32Sfloat", "D16Unorm")
        and shadow[3] in ("D32Sfloat", "D16Unorm"),
        "unregistered depth format",
    )
    require(
        one(stdout, r"^Shadow map creations after run: (\d+)\.$", "exit creation count")
        == slots,
        "shadow resource replacement during run",
    )
    metadata = {
        "device": device,
        "driver": driver,
        "swapchain": swapchain,
        "slots": slots,
        "forward_format": forward_format,
        "shadow": shadow,
        "graphics_queue": one(
            stdout, r"^Graphics queue family: (\d+)$", "graphics queue"
        ),
        "present_queue": one(stdout, r"^Present queue family: (\d+)$", "present queue"),
    }

    header = one(stdout, r"^(  Phase\s+mean us\s+median us\s+p95 us)$", "phase header")
    table = stdout.split(header + "\n", 1)[1].split("\n  Snapshot share", 1)[0]
    phases = {}
    for line in table.splitlines():
        if not line.strip():
            continue
        tokens = line.strip().rsplit(None, 3)
        require(len(tokens) == 4, "malformed phase row")
        name, *values = tokens
        require(name not in phases, f"duplicate phase: {name}")
        require(
            all(re.fullmatch(r"\d+\.\d{3}", value) for value in values),
            f"non-finite, negative or malformed duration: {name}",
        )
        phases[name] = dict(zip(("mean_us", "median_us", "p95_us"), map(D, values)))
        require(
            phases[name]["median_us"] <= phases[name]["p95_us"],
            f"invalid quantiles: {name}",
        )
    require(
        list(phases) == phase_names(effective_mode),
        "missing, unknown or reordered phase rows",
    )
    means = {name: stats["mean_us"] for name, stats in phases.items()}
    require(total(means, ACTIVE) > 0, "zero measured active work")
    raw_shares = re.findall(
        r"^  ([^\n:]+): ([^\n%]+)%(?: of frames)?$", stdout, re.MULTILINE
    )
    shares = {}
    for name, value in raw_shares:
        require(
            name not in shares and re.fullmatch(r"\d+\.\d{2}", value),
            "duplicate or malformed share",
        )
        shares[name] = D(value)
        require(0 <= shares[name] <= 100, "share outside [0, 100]")
    expected_shares = shares_for(effective_mode)
    require(
        set(shares)
        == set(expected_shares)
        | (set(COMPLETION) if effective_mode == "two" else set()),
        "missing or unexpected report shares",
    )
    recomputed = {}
    for name, components in expected_shares.items():
        check_share(means, components, shares[name], name)
        recomputed[name] = 100 * total(means, components) / total(means, ACTIVE)
    if effective_mode == "two":
        require(
            abs(sum(shares[n] for n in COMPLETION) - 100) <= 3 * HALF_PERCENT,
            "completion shares do not sum to 100 percent",
        )
        for summed, suffix in ((SUM_RESET, "pool reset"), (SUM_RECORD, "recording")):
            component_sum = sum(
                means[f"forward participant {p} {suffix}"] for p in range(2)
            )
            require(
                abs(means[summed] - component_sum) <= 3 * HALF_US,
                f"{summed}: inconsistent with participant means",
            )
    if effective_mode == "direct":
        require(
            phases[RESET] == phases[REGION],
            "direct empty-pool reset attribution changed",
        )
        require(
            all(
                value == 0
                for name in (RECORD, EXECUTE)
                for value in phases[name].values()
            ),
            "direct path reported secondary work",
        )
    return {
        "metadata": metadata,
        "mode": effective_mode,
        "draws": draws,
        "phases": phases,
        "means": means,
        "shares": shares,
        "recomputed_shares": recomputed,
        "quantities": quantities(means),
        "validation_warnings": combined.count("Vulkan validation warning:"),
    }
