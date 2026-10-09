"""Independent synthetic report/child for device-free matrix controls."""

import json
import sys
from decimal import Decimal as D
from pathlib import Path

SNAPSHOT = ["transform update", "draw-list build", "frame recording-input build"]
SHADOW = ["shadow command-pool reset", "shadow primary command recording"]
FORWARD = [
    "forward coordinator command-pool reset",
    "forward secondary recording region",
    "forward primary command recording",
    "forward secondary command execution",
]
ACTIVE = SNAPSHOT + ["frame-uniform update"] + SHADOW + FORWARD + ["queue submission"]


def report(draws=1000, mode="one", overrides=None):
    effective = ("two" if draws == 10000 else "one") if mode == "auto" else mode
    two, direct = effective == "two", effective == "direct"
    reset = (
        "forward participant pool reset (summed CPU)"
        if two
        else "forward participant command-pool reset"
    )
    recording = (
        "forward secondary recording (summed CPU)"
        if two
        else "forward secondary command recording"
    )
    means = dict(zip(SNAPSHOT, [2, 2, 2]))
    means.update(
        {
            "frame-uniform update": 1,
            SHADOW[0]: 2,
            SHADOW[1]: 18,
            FORWARD[0]: 3,
            reset: 8 if two else 4,
            recording: 16 if two else 10,
            FORWARD[1]: 20 if two else 30,
        }
    )
    if two:
        means.update(
            {
                "forward participant-region critical path": 14,
                "forward participant reset-region span": 5,
                "forward completion join wait": 1,
                "forward completion tail": 0.2,
                "forward helper work remaining at join": 0.8,
            }
        )
        for participant in range(2):
            means.update(
                {
                    f"forward participant {participant} pool reset": 4,
                    f"forward participant {participant} recording": 8,
                    f"forward participant {participant} reset start offset": participant,
                }
            )
    means.update(
        {
            FORWARD[2]: 5,
            FORWARD[3]: 8,
            "queue submission": 3,
            "frame-fence wait": 1,
            "image-acquisition wait": 100,
            "presentation-fence wait": 2,
            "presentation call": 20,
        }
    )
    if direct:
        means.update(
            {reset: 1, recording: 0, FORWARD[1]: 1, FORWARD[2]: 40, FORWARD[3]: 0}
        )
    means.update(overrides or {})
    means = {name: D(str(value)) for name, value in means.items()}
    lines = [
        "Selected Vulkan 1.4 device: llvmpipe (fixture)",
        "Driver: llvmpipe (Mesa fixture)",
        "Graphics queue family: 0",
        "Present queue family: 0",
        "Swapchain created: 4 images at 800x600 (B8G8R8A8Srgb, Mailbox), 4 presentation semaphores.",
        "Forward depth format: D32Sfloat.",
        "Shadow maps: 2 at 1024x1024, format D16Unorm, 2 created.",
        "Shadow map creations after run: 2.",
        "",
        "Phase-level CPU benchmark",
        "  Build configuration: Release",
        "  Device: llvmpipe (fixture)",
        "  Driver: llvmpipe (Mesa fixture)",
        "  Forward recording path: "
        + ("direct primary command buffer" if direct else "secondary command buffer"),
    ]
    if not direct:
        count = 2 if two else 1
        selection = (
            "automatic at or above 5000 draws per participant"
            if mode == "auto"
            else f"{count} forced"
        )
        lines.append(
            f"  Forward secondary recording participants: {selection}, {count} effective"
        )
    lines += [
        "  Frames in flight: 2",
        "  Presentation: 800x600, B8G8R8A8Srgb, Mailbox",
        f"  Workload: instances={draws}, nodes={draws + 1}, draws={draws}",
        f"  Recorded packets per frame: shadow={draws}, forward={draws}",
        "  Frames: 16 warm-up, 64 measured, 0 discarded attempts",
        "  Fixed animation step: 0.016667 seconds",
        "",
        "  Phase                                     mean us    median us       p95 us",
    ]
    for name, value in means.items():
        lines.append(f"  {name:<36} {value:12.3f} {value:12.3f} {value:12.3f}")
    active = sum(means[name] for name in ACTIVE)
    shares = {
        "Snapshot share of measured active work": sum(means[n] for n in SNAPSHOT),
        "Queue-submission share of measured active work": means["queue submission"],
        "Shadow-pass share of measured active work": sum(means[n] for n in SHADOW),
    }
    if not direct:
        shares.update(
            {
                "Forward secondary-execution share of measured active work": means[
                    FORWARD[3]
                ],
                "Forward secondary-recording region share of measured active work": means[
                    FORWARD[1]
                ],
                "Current serial share outside that region": active - means[FORWARD[1]],
            }
        )
        if not two:
            shares["Forward participant-pool-reset share of measured active work"] = (
                means[reset]
            )
    lines.append("")
    for name, numerator in shares.items():
        lines.append(f"  {name}: {100 * numerator / active:.2f}%")
    if two:
        lines += [
            "  Completion acquired by polling: 75.00% of frames",
            "  Completion required a blocking wait: 25.00% of frames",
            "  Completion observed at the deadline boundary: 0.00% of frames",
        ]
    lines += [
        "  Forward draw bindings are cached independently inside each command buffer.",
        "Presented 80 frames.",
    ]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    # The integration harness runs this finite child, never the actual renderer.
    configuration = json.loads(Path(sys.argv[1]).read_text())
    print(
        report(
            configuration["draws"],
            configuration["mode"],
            configuration.get("overrides"),
        ),
        end="",
        flush=True,
    )
    if configuration.get("sleep"):
        import time

        time.sleep(configuration["sleep"])
    sys.exit(configuration.get("exit", 0))
