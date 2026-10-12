"""Hosted CI wiring and visibly incomplete observational summaries."""

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/benchmark"))
import benchmark_provenance as provenance
import summarize_ci as summary

ROOT = Path(__file__).resolve().parents[2]


class HostedCiTests(unittest.TestCase):
    def test_hosted_conditions_do_not_invent_controlled_power_or_cooldown(self):
        conditions = provenance.load_conditions(
            ROOT / "tools/benchmark/conditions.hosted.json", "hosted-lavapipe"
        )
        self.assertIsNone(conditions["ac_power"])
        self.assertEqual(conditions["cpu_governor"], "not established")
        self.assertIn("No deliberate cooldown", conditions["cooldown"])
        self.assertIn("not controlled", conditions["background_load"])
        self.assertIn("Xvfb", conditions["presentation_route"])

    def test_no_session_or_multiple_sessions_are_incomplete_not_selected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "evidence"
            text, complete = summary.summarize(root, "success")
            self.assertFalse(complete)
            self.assertIn("found 0", text)
            (root / "matrix-hosted-lavapipe-first").mkdir(parents=True)
            (root / "matrix-hosted-lavapipe-second").mkdir()
            text, complete = summary.summarize(root, "success")
            self.assertFalse(complete)
            self.assertIn("found 2", text)
            self.assertNotIn("**COMPLETE", text)

    def test_cli_publishes_missing_measurement_warning_and_preserves_job_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            job_summary = root / "job-summary.md"
            job_summary.write_text("Earlier step\n")
            output = root / "evidence"
            with (
                patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(job_summary)}),
                patch.object(
                    sys,
                    "argv",
                    [
                        "summarize_ci.py",
                        "--output-root",
                        str(output),
                        "--outcome",
                        "skipped",
                    ],
                ),
                contextlib.redirect_stdout(io.StringIO()) as stdout,
            ):
                self.assertEqual(summary.main(), 0)
            text = (output / "summary.md").read_text()
            self.assertIn("**INCOMPLETE", text)
            self.assertEqual(job_summary.read_text(), "Earlier step\n" + text)
            self.assertIn(
                "::warning title=Hosted benchmark incomplete::", stdout.getvalue()
            )

    def test_workflow_uses_outcome_and_keeps_correctness_mandatory(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        linux = workflow.split("  build-test-linux:", 1)[1].split("  build-macos:", 1)[
            0
        ]
        correctness, benchmark = linux.split(
            "      - name: Configure Release benchmark", 1
        )
        self.assertNotIn("continue-on-error", correctness)
        self.assertIn("python3 -m unittest discover -s tests/benchmark -v", correctness)
        self.assertIn("tools/ci/run_validation_tests.py", correctness)
        self.assertEqual(benchmark.count("tools/benchmark/run_matrix.py"), 1)
        self.assertNotIn("./build-benchmark/fireEngineTutorial", benchmark)
        self.assertIn("steps.hosted_benchmark.outcome", benchmark)
        self.assertNotIn("steps.hosted_benchmark.conclusion", benchmark)
        for name in (
            "Summarize hosted benchmark completeness",
            "Upload hosted benchmark evidence",
        ):
            step = linux.split(f"      - name: {name}", 1)[1].split("      - name:", 1)[
                0
            ]
            self.assertIn("if: always()", step)
        self.assertIn("name: linux-benchmark-evidence", benchmark)

    def test_actual_workflow_pipeline_preserves_launcher_failure_and_output(self):
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        step = workflow.split("        id: hosted_benchmark", 1)[1].split(
            "      - name:", 1
        )[0]
        self.assertIn("shell: bash", step)
        self.assertIn("continue-on-error: true", step)
        body = step.split("        run: |\n", 1)[1]
        script = "\n".join(line[10:] for line in body.splitlines())
        self.assertIn("--role hosted-lavapipe", script)
        self.assertIn('--icd "${VK_DRIVER_FILES}"', script)
        self.assertIn("--output-root build/ci-benchmark", script)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = root / "xvfb-run"
            for code in (0, 17):
                with self.subTest(code=code):
                    # Run the workflow's exact shell body with a finite launcher,
                    # never Xvfb or the renderer. tee must not swallow exit 17.
                    launcher.write_text(
                        f"#!/bin/sh\nprintf 'launcher stdout\\n'\nprintf 'launcher stderr\\n' >&2\nexit {code}\n"
                    )
                    launcher.chmod(0o700)
                    result = subprocess.run(
                        [
                            "bash",
                            "--noprofile",
                            "--norc",
                            "-eo",
                            "pipefail",
                            "-c",
                            script,
                        ],
                        cwd=root,
                        env={
                            **os.environ,
                            "PATH": f"{root}{os.pathsep}{os.environ['PATH']}",
                            "VK_DRIVER_FILES": "/fixture/icd.json",
                        },
                        text=True,
                        capture_output=True,
                        timeout=10,
                    )
                    self.assertEqual(result.returncode, code, result.stderr)
                    log = (root / "build/ci-benchmark/acquisition.log").read_text()
                    self.assertIn("launcher stdout", log)
                    self.assertIn("launcher stderr", log)


if __name__ == "__main__":
    unittest.main()
