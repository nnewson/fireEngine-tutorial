"""Device-free controls for laptop qualification, not performance acquisition."""

import contextlib
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools/benchmark/preflight_lavapipe.py"
SPEC = importlib.util.spec_from_file_location("lavapipe_preflight", SCRIPT)
preflight = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preflight)

# Synthetic contracts copied from the output fields, not recorded GPU evidence.
INVENTORY = """Vulkan Instance Version: 1.4.357
GPU0:
    apiVersion = 1.4.318
    driverName = llvmpipe
    deviceName = llvmpipe (LLVM 19.1.7, 256 bits)
"""
REPORT = """Phase-level CPU benchmark
  Build configuration: Release
  Device: llvmpipe (LLVM 19.1.7, 256 bits)
  Driver: llvmpipe (Mesa test fixture)
  Workload: instances=1, nodes=2, draws=1
  Recorded packets per frame: shadow=1, forward=1
  Frames: 16 warm-up, 64 measured, 0 discarded attempts
  Forward draw bindings are cached independently inside each command buffer.
"""


class EnvironmentTests(unittest.TestCase):
    def test_only_child_icd_selection_changes(self):
        original = {
            "VK_DRIVER_FILES": "/old/intel.json",
            "VK_ICD_FILENAMES": "/old/nvidia.json",
            "LP_NUM_THREADS": "4",
            "__VK_LAYER_NV_optimus": "NVIDIA_only",
        }
        child = preflight.lavapipe_environment(original, Path("/selected/lvp.json"))
        self.assertEqual(original["VK_DRIVER_FILES"], "/old/intel.json")
        self.assertEqual(
            child,
            {
                **original,
                "VK_DRIVER_FILES": "/selected/lvp.json",
                "VK_ICD_FILENAMES": "/selected/lvp.json",
            },
        )

    def test_settings_are_recorded_not_rejected_and_unset_is_not_empty(self):
        environment = {
            "LP_NUM_THREADS": "2",
            "MESA_VK_VERSION_OVERRIDE": "1.4",
            "GALLIVM_PERF": "no_opt",
            "DRI_PRIME": "",
            "UNRELATED_SECRET": "do not copy",
        }
        snapshot = preflight.environment_snapshot(environment)
        self.assertEqual(snapshot["LP_NUM_THREADS"], "2")
        self.assertEqual(snapshot["MESA_VK_VERSION_OVERRIDE"], "1.4")
        self.assertEqual(snapshot["GALLIVM_PERF"], "no_opt")
        self.assertEqual(snapshot["DRI_PRIME"], "")
        self.assertIsNone(snapshot["VK_DRIVER_FILES"])
        self.assertNotIn("UNRELATED_SECRET", snapshot)


class OutputTests(unittest.TestCase):
    def test_inventory_accepts_only_qualified_physical_device(self):
        preflight.check_inventory(INVENTORY)
        preflight.check_inventory(INVENTORY.replace("1.4.318", "1.5.1"))
        for output in (
            "",
            INVENTORY.replace("1.4.318", "1.3.318"),
            INVENTORY.replace("driverName = llvmpipe", "driverName = NVIDIA"),
            INVENTORY + INVENTORY,
            INVENTORY + "driverName = NVIDIA\n",
            INVENTORY.replace("    apiVersion = 1.4.318\n", ""),
        ):
            with self.subTest(output=output), self.assertRaises(ValueError):
                preflight.check_inventory(output)

    def test_every_application_contract_field_is_required_once(self):
        preflight.check_application(REPORT)
        for line in REPORT.splitlines(keepends=True):
            if line.startswith("  Device:"):
                continue  # Driver identity is the selection authority here.
            for output in (REPORT.replace(line, ""), REPORT + line):
                with (
                    self.subTest(line=line, output=output),
                    self.assertRaises(ValueError),
                ):
                    preflight.check_application(output)

    def test_wrong_build_driver_workload_or_completion_fails(self):
        for before, after in (
            ("Release", "Debug"),
            ("Driver: llvmpipe", "Driver: NVIDIA"),
            ("instances=1", "instances=1000"),
            ("shadow=1", "shadow=0"),
            ("64 measured", "63 measured"),
        ):
            with self.subTest(after=after), self.assertRaises(ValueError):
                preflight.check_application(REPORT.replace(before, after))

    def test_discarded_attempts_do_not_turn_preflight_into_measurement_gate(self):
        preflight.check_application(REPORT.replace("0 discarded", "2 discarded"))


class ProcessTests(unittest.TestCase):
    def run_child(self, directory, code, timeout=5):
        return preflight.run_check(
            "child",
            [sys.executable, "-c", code],
            Path(directory),
            os.environ.copy(),
            timeout,
        )

    def test_success_preserves_both_streams_and_command(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_child(
                directory, "import sys; print('out'); print('err', file=sys.stderr)"
            )
            self.assertEqual(result["returncode"], 0)
            self.assertNotIn("error", result)
            self.assertEqual(result["command"][0], sys.executable)
            self.assertEqual(
                (Path(directory) / "child.stdout.log").read_text(), "out\n"
            )
            self.assertEqual(
                (Path(directory) / "child.stderr.log").read_text(), "err\n"
            )

    def test_nonzero_and_signal_are_not_success(self):
        for code, expected in (
            ("import sys; print('partial'); sys.exit(7)", 7),
            ("import os, signal; os.kill(os.getpid(), signal.SIGTERM)", -15),
        ):
            with (
                self.subTest(expected=expected),
                tempfile.TemporaryDirectory() as directory,
            ):
                result = self.run_child(directory, code)
                self.assertEqual(result["returncode"], expected)
                self.assertIn("error", result)

    def test_timeout_preserves_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_child(
                directory,
                "import time; print('started', flush=True); time.sleep(30)",
                1,
            )
            self.assertIsNone(result["returncode"])
            self.assertIn("timeout", result["error"])
            self.assertEqual(
                (Path(directory) / "child.stdout.log").read_text(), "started\n"
            )

    def test_missing_executable_records_launch_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = preflight.run_check(
                "missing", [str(output / "absent")], output, os.environ.copy(), 5
            )
            self.assertIsNone(result["returncode"])
            self.assertIn("could not launch", result["error"])
            self.assertTrue((output / "missing.stderr.log").is_file())


class QualificationTests(unittest.TestCase):
    def qualify(self, inventory=INVENTORY, report=REPORT, stderr="", results=None):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            for name, content in (("vulkaninfo", inventory), ("benchmark", report)):
                (output / f"{name}.stdout.log").write_text(content)
                (output / f"{name}.stderr.log").write_text(stderr)
            results = results or {
                "vulkaninfo": {"returncode": 0},
                "benchmark": {"returncode": 0},
            }
            return preflight.qualify(output, results)

    def test_both_programs_must_pass(self):
        self.assertEqual(self.qualify(), [])
        self.assertTrue(self.qualify(inventory=""))
        self.assertTrue(self.qualify(report=""))
        self.assertTrue(
            self.qualify(
                results={
                    "vulkaninfo": {"returncode": 0},
                    "benchmark": {"returncode": 1, "error": "exit 1"},
                }
            )
        )

    def test_validation_error_on_either_stream_cannot_pass(self):
        for kwargs in (
            {"stderr": "Vulkan validation error: deliberate control"},
            {"report": REPORT + "Vulkan validation error: deliberate control\n"},
        ):
            with self.subTest(kwargs=kwargs):
                self.assertTrue(self.qualify(**kwargs))

    def test_warnings_are_counted_not_hidden(self):
        results = {"vulkaninfo": {"returncode": 0}, "benchmark": {"returncode": 0}}
        self.assertEqual(
            self.qualify(stderr="Vulkan validation warning: fixture", results=results),
            [],
        )
        self.assertEqual(results["benchmark"]["validation_warnings"], 1)

    def test_main_runs_both_checks_even_when_inventory_fails(self):
        for inventory_exit in (0, 3):
            with (
                self.subTest(inventory_exit=inventory_exit),
                tempfile.TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                manifest = root / "lvp.json"
                manifest.write_text('{"ICD": {"library_path": "fixture"}}\n')
                calls = []
                real_run_check = preflight.run_check

                def fake_child(name, command, output, environment, timeout):
                    calls.append((name, command, environment))
                    content = INVENTORY if name == "vulkaninfo" else REPORT
                    exit_code = inventory_exit if name == "vulkaninfo" else 0
                    # Real bounded child/streams, but no Vulkan or window required.
                    code = f"import sys; print({content!r}); sys.exit({exit_code})"
                    return real_run_check(
                        name, [sys.executable, "-c", code], output, environment, timeout
                    )

                with (
                    patch.object(sys, "platform", "linux"),
                    patch.object(os, "geteuid", return_value=1000),
                    patch.object(
                        sys,
                        "argv",
                        [
                            str(SCRIPT),
                            "--icd",
                            str(manifest),
                            "--executable",
                            sys.executable,
                            "--output-root",
                            str(root),
                        ],
                    ),
                    patch.object(preflight, "git_output", return_value="fixture"),
                    patch.object(preflight, "run_check", side_effect=fake_child),
                    contextlib.redirect_stdout(io.StringIO()),
                    contextlib.redirect_stderr(io.StringIO()),
                ):
                    self.assertEqual(preflight.main(), 0 if inventory_exit == 0 else 1)
                self.assertEqual(
                    [call[0] for call in calls], ["vulkaninfo", "benchmark"]
                )
                self.assertEqual(calls[1][1][1:], ["--benchmark", "1"])
                self.assertEqual(calls[0][2], calls[1][2])
                self.assertEqual(
                    calls[1][2]["VK_DRIVER_FILES"], str(manifest.resolve())
                )
                saved = json.loads(next(root.glob("*/summary.json")).read_text())
                self.assertEqual(
                    saved["status"], "qualified" if inventory_exit == 0 else "failed"
                )
                self.assertEqual(
                    saved["executable"],
                    preflight.file_identity(Path(sys.executable).resolve()),
                )
                self.assertEqual(set(saved["results"]), {"vulkaninfo", "benchmark"})


if __name__ == "__main__":
    unittest.main()
