"""Registered report, selection, acquisition and provenance failure controls."""

import contextlib
import copy
import io
import json
import os
import re
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/benchmark"))
import analyze_matrix as matrix
import benchmark_provenance as provenance
import benchmark_report as report
import preflight_lavapipe as preflight
import run_matrix as runner
from matrix_fixture import report as fixture

FIXTURES = Path(__file__).parent / "fixtures"
D = report.D


def parsed(spec, overrides=None):
    return report.parse_report(
        fixture(spec["draws"], spec["mode"], overrides),
        "",
        spec["draws"],
        spec["mode"],
        "laptop-lavapipe",
    )


def populated(overrides=None):
    reports = {}
    while (spec := matrix.next_spec(reports)) is not None:
        reports[spec["id"]] = parsed(spec, (overrides or {}).get(spec["id"]))
    return reports


class ReportTests(unittest.TestCase):
    def test_real_reports_and_rounding(self):
        one = report.parse_report(
            (FIXTURES / "lavapipe-one-auto.txt").read_text(),
            "",
            1,
            "auto",
            "laptop-lavapipe",
        )
        two = report.parse_report(
            (FIXTURES / "kosmickrisp-two.txt").read_text(),
            "",
            10000,
            "two",
            "mac-kosmickrisp",
        )
        self.assertEqual(one["quantities"]["S_phase_us"], D("8.553"))
        self.assertEqual(two["means"][report.SUM_RESET], D("244.731"))
        # The printed participant means sum to 244.730, not 244.731.
        # Their rounding intervals overlap; exact printed equality is not required.
        self.assertEqual(
            sum(two["means"][f"forward participant {p} pool reset"] for p in range(2)),
            D("244.730"),
        )

    def test_one_two_direct_and_both_workloads(self):
        for draws in (1, 1000, 10000):
            for mode in ("one", "direct", "auto") + (("two",) if draws > 1 else ()):
                with self.subTest(draws=draws, mode=mode):
                    result = parsed(matrix.arm_spec("fixture", draws, mode))
                    self.assertEqual(result["draws"], draws)
                    self.assertNotEqual(
                        result["metadata"]["forward_format"],
                        result["metadata"]["shadow"][3],
                    )

    def test_exact_active_arithmetic_does_not_add_diagnostics_or_waits(self):
        result = parsed(matrix.arm_spec("fixture", 1000, "one"))
        self.assertEqual(
            result["quantities"],
            {
                "snapshot_us": D(6),
                "S_phase_us": D(20),
                "F_phase_us": D(46),
                "A_phase_us": D(76),
            },
        )
        low, high = report.sum_interval(result["means"], report.ACTIVE)
        self.assertEqual((low, high), (D("75.9945"), D("76.0055")))

    def test_every_phase_is_required_once_in_order(self):
        for mode in ("one", "two", "direct"):
            text = fixture(mode=mode)
            for line in text.splitlines(keepends=True):
                if not re.match(
                    r"^  .+\s+\d+\.\d{3}\s+\d+\.\d{3}\s+\d+\.\d{3}\n$", line
                ):
                    continue
                for modified in (
                    text.replace(line, ""),
                    text.replace(line, line + line),
                ):
                    with (
                        self.subTest(mode=mode, line=line),
                        self.assertRaises(report.MeasurementError),
                    ):
                        report.parse_report(modified, "", 1000, mode, "laptop-lavapipe")

    def test_bad_numeric_and_broken_share_rejected(self):
        text = fixture()
        for modified in (
            text.replace("2.000", "nan", 1),
            text.replace("2.000", "inf", 1),
            text.replace("2.000", "-1.000", 1),
            text.replace("2.000", "1e300", 1),
            re.sub(
                r"(Shadow-pass share of measured active work:) [\d.]+%",
                r"\1 0.00%",
                text,
            ),
            re.sub(r"^  Current serial share.*\n", "", text, flags=re.MULTILINE),
            text + "  Snapshot share of measured active work: 99.00%\n",
            text + "  Invented share: 10.00%\n",
        ):
            with (
                self.subTest(modified=modified[-300:]),
                self.assertRaises(report.MeasurementError),
            ):
                report.parse_report(modified, "", 1000, "one", "laptop-lavapipe")

    def test_wrong_workload_path_build_or_incomplete_report_rejected(self):
        text = fixture()
        for before, after in (
            ("Release", "Debug"),
            ("instances=1000", "instances=1"),
            ("shadow=1000", "shadow=999"),
            ("nodes=1001", "nodes=1000"),
            ("64 measured", "63 measured"),
            ("0 discarded", "1 discarded"),
            ("1 forced, 1 effective", "2 forced, 1 effective"),
            ("secondary command buffer", "direct primary command buffer"),
            ("Presented 80 frames.", "Presented 79 frames."),
            ("0.016667 seconds", "0.800000 seconds"),
            (
                "Shadow map creations after run: 2.",
                "Shadow map creations after run: 4.",
            ),
            (
                "  Forward draw bindings are cached independently inside each command buffer.\n",
                "",
            ),
        ):
            with self.subTest(after=after), self.assertRaises(report.MeasurementError):
                report.parse_report(
                    text.replace(before, after), "", 1000, "one", "laptop-lavapipe"
                )
        with self.assertRaises(report.MeasurementError):
            report.parse_report(text + text, "", 1000, "one", "laptop-lavapipe")

    def test_validation_truncation_and_driver_role_rejected(self):
        for stderr in (
            "Vulkan validation error: fault",
            "WARNING: ThreadSanitizer: data race",
            "Test Output for this test has been truncated",
        ):
            with (
                self.subTest(stderr=stderr),
                self.assertRaises(report.MeasurementError),
            ):
                report.parse_report(fixture(), stderr, 1000, "one", "laptop-lavapipe")
        for role in ("laptop-nvidia", "mac-kosmickrisp"):
            with self.subTest(role=role), self.assertRaises(report.MeasurementError):
                report.parse_report(fixture(), "", 1000, "one", role)
        text = (
            fixture()
            .replace("llvmpipe (fixture)", "NVIDIA GeForce GTX 1050")
            .replace("llvmpipe (Mesa fixture)", "NVIDIA (driver fixture)")
        )
        report.parse_report(text, "", 1000, "one", "laptop-nvidia")

    def test_participant_sums_and_completion_shares_are_checked(self):
        for overrides in ({report.SUM_RESET: 9}, {report.SUM_RECORD: 17}):
            with (
                self.subTest(overrides=overrides),
                self.assertRaises(report.MeasurementError),
            ):
                parsed(matrix.arm_spec("fixture", 1000, "two"), overrides)
        with self.assertRaises(report.MeasurementError):
            report.parse_report(
                fixture(mode="two").replace("75.00% of frames", "76.00% of frames"),
                "",
                1000,
                "two",
                "laptop-lavapipe",
            )

    def test_direct_empty_pool_reset_stays_in_active_sum(self):
        result = parsed(matrix.arm_spec("fixture", 1000, "direct"))
        self.assertEqual(result["quantities"]["F_phase_us"], D(44))
        with self.assertRaises(report.MeasurementError):
            parsed(matrix.arm_spec("fixture", 1000, "direct"), {report.REGION: 0})


class SelectionTests(unittest.TestCase):
    def asymmetric_cell(self, replacement):
        prefix = "retry-" if replacement else ""
        changes = {"1000-X": {report.REGION: 30}} if replacement else {}
        changes.update(
            {
                f"{prefix}1000-A": {
                    report.RESET: 4,
                    report.RECORD: 10,
                    report.REGION: 40,
                    report.SHADOW[0]: 2,
                    report.SHADOW[1]: 18,
                },
                f"{prefix}1000-B": {
                    report.RESET: 8,
                    report.RECORD: 18,
                    report.REGION: 52,
                    report.SHADOW[0]: 6,
                    report.SHADOW[1]: 30,
                },
            }
        )
        return matrix.derive(populated(changes))["1000"]

    def test_model_averages_asymmetric_selected_controls_component_wise(self):
        # A: active 86, B: 114, X: 66. Mean control 100, drift 28,
        # improvement 34: original and replacement variants both resolve.
        # W=(4+8)/2=6; R=(10+18)/2=14; F1=4, F0=1.
        for replacement in (False, True):
            with self.subTest(replacement=replacement):
                cell = self.asymmetric_cell(replacement)
                model = cell["model"]
                self.assertEqual(model["T1_us"], D(100))
                self.assertEqual(model["T2_us"], D(66))
                self.assertEqual(model["W_us"], D(6))
                self.assertEqual(model["R_us"], D(14))
                self.assertEqual(model["actual_reduction"], D("0.34"))
                self.assertEqual(model["endpoints"]["F1"]["p"], D("0.16"))
                self.assertEqual(model["endpoints"]["F1"]["ideal_reduction"], D("0.08"))
                self.assertEqual(model["endpoints"]["F1"]["materialization"], D("4.25"))
                self.assertEqual(
                    model["endpoints"]["F1"]["predicted_critical_us"], D(12)
                )
                self.assertEqual(model["endpoints"]["F0"]["p"], D("0.19"))
                self.assertEqual(
                    model["endpoints"]["F0"]["materialization"], D(68) / 19
                )

    def test_pass_costs_average_asymmetric_selected_controls_component_wise(self):
        # Shadow=(20+36)/2=28, forward=(56+68)/2=62. These checks must
        # fail independently of the model if pass_costs is given only arm A.
        for replacement in (False, True):
            with self.subTest(replacement=replacement):
                cost = self.asymmetric_cell(replacement)["one_control"]
                self.assertEqual(cost["A_phase_us"], D(100))
                self.assertEqual(cost["S_phase_us"], D(28))
                self.assertEqual(cost["F_phase_us"], D(62))
                self.assertEqual(cost["shadow_reset_us"], D(4))
                self.assertEqual(cost["shadow_recording_us"], D(24))
                self.assertEqual(cost["shadow_share"], D("0.28"))
                self.assertEqual(cost["serial_measured_pass_us"], D(90))
                self.assertEqual(cost["idealized_overlapped_us"], D(62))
                self.assertEqual(cost["removable_measured_work_bound_us"], D(28))
                self.assertEqual(cost["measured_active_reduction_bound"], D("0.28"))

    def test_resolved_regression_is_retained_not_retried(self):
        reports = populated({"1000-X": {report.REGION: 50}})
        cell = matrix.derive(reports)["1000"]
        self.assertEqual(
            cell["selected_comparisons"]["A_phase"]["classification"], "regression"
        )
        self.assertEqual(cell["original_status"], "selected")
        self.assertEqual(len(reports), 11)

    def test_strict_drift_rule_and_sign(self):
        cases = [
            (100, 80, 110, "improvement"),
            (100, 120, 110, "regression"),
            (100, 95, 110, "unresolved"),
            (100, 100, 100, "unresolved"),
        ]
        for a, x, b, expected in cases:
            self.assertEqual(
                matrix.compare(D(a), D(x), D(b))["classification"], expected
            )

    def test_initial_matrix_order_and_no_retry_for_resolved_primary(self):
        expected = [
            "qualify-1000-auto",
            "qualify-10000-auto",
            "one-draw",
            "1000-A",
            "1000-X",
            "1000-B",
            "10000-A",
            "10000-X",
            "10000-B",
            "1000-direct",
            "10000-direct",
        ]
        reports = populated()
        self.assertEqual(list(reports), expected)
        self.assertEqual(
            matrix.derive(reports)["1000"]["selected_arms"],
            ["1000-A", "1000-X", "1000-B"],
        )

    def test_secondary_unresolved_cannot_authorize_retry(self):
        reports = populated({"1000-X": {report.REGION: 30, SHADOW_RECORD: 8}})
        cell = matrix.derive(reports)["1000"]
        self.assertEqual(
            cell["selected_comparisons"]["A_phase"]["classification"], "improvement"
        )
        self.assertEqual(
            cell["selected_comparisons"]["F_phase"]["classification"], "unresolved"
        )
        self.assertEqual(len(reports), 11)

    def test_secondary_resolved_cannot_prevent_primary_retry(self):
        reports = populated({"1000-X": {SHADOW_RECORD: 28}})
        cell = matrix.derive(reports)["1000"]
        self.assertEqual(
            cell["original_comparisons"]["F_phase"]["classification"], "improvement"
        )
        self.assertEqual(cell["original_status"], "void/context")
        self.assertEqual(
            cell["selected_arms"], ["retry-1000-A", "retry-1000-X", "retry-1000-B"]
        )

    def test_two_unresolved_triplets_select_replacement_for_every_quantity(self):
        changes = {"1000-X": {report.REGION: 30}}
        for name in ("A", "X", "B"):
            changes[f"retry-1000-{name}"] = {SHADOW_RECORD: 58, report.REGION: 30}
            if name != "X":
                changes[f"retry-1000-{name}"].update(
                    {report.RESET: 6, report.RECORD: 12}
                )
        cell = matrix.derive(populated(changes))["1000"]
        self.assertTrue(cell["descriptive_only"])
        self.assertEqual(cell["model"]["T1_us"], D(116))
        self.assertEqual(cell["model"]["T2_us"], D(116))
        self.assertEqual(cell["model"]["W_us"], D(6))
        self.assertEqual(cell["model"]["R_us"], D(12))
        self.assertEqual(cell["one_control"]["S_phase_us"], D(60))
        self.assertEqual(cell["two"]["S_phase_us"], D(60))
        self.assertEqual(cell["two"]["removable_measured_work_bound_us"], D(36 + 10))
        self.assertEqual(cell["model"]["F1_us"], D(4))
        self.assertEqual(cell["model"]["F0_us"], D(1))
        self.assertEqual(cell["ancillary_arms"], ["one-draw", "1000-direct"])

    def test_retries_are_once_in_workload_order_after_all_initial_arms(self):
        changes = {
            f"{prefix}{n}-X": {report.REGION: 30}
            for n in (1000, 10000)
            for prefix in ("", "retry-")
        }
        reports = populated(changes)
        self.assertEqual(
            list(reports)[11:],
            [
                "retry-1000-A",
                "retry-1000-X",
                "retry-1000-B",
                "retry-10000-A",
                "retry-10000-X",
                "retry-10000-B",
            ],
        )
        self.assertEqual(len(reports), 17)

    def test_incomplete_replacement_has_no_derived_fallback(self):
        reports = populated({"1000-X": {report.REGION: 30}})
        del reports["retry-1000-B"]
        with self.assertRaisesRegex(report.MeasurementError, "incomplete"):
            matrix.derive(reports)

    def test_model_and_overlap_use_registered_arithmetic(self):
        cell = matrix.derive(populated())["1000"]
        model = cell["model"]
        self.assertEqual(model["T1_us"], D(76))
        self.assertEqual(model["T2_us"], D(66))
        self.assertEqual(model["endpoints"]["F1"]["p"], D(10) / 76)
        self.assertEqual(model["endpoints"]["F1"]["materialization"], D(2))
        self.assertTrue(cell["overlap"]["complete_overlap_observed"])
        self.assertEqual(cell["two"]["removable_measured_work_bound_us"], D(20))

    def test_model_violations_are_reported_without_clamping(self):
        reports = populated(
            {
                "one-draw": {report.RESET: 10},
                "1000-direct": {report.RESET: 20, report.REGION: 20},
            }
        )
        model = matrix.derive(reports)["1000"]["model"]
        self.assertIn("F0 exceeds F1; fixed-cost bracket is inverted", model["issues"])
        self.assertEqual(model["endpoints"]["F1"]["variable_reset_us"], D(-6))
        self.assertLess(model["endpoints"]["F0"]["p"], 0)
        self.assertIsNone(model["endpoints"]["F0"]["materialization"])


SHADOW_RECORD = "shadow primary command recording"


def condition_sample(governor="performance", online="1", supply_type="Mains"):
    return {
        "readings": {
            "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor": {
                "value": governor
            },
            "/sys/class/power_supply/AC/type": {"value": supply_type},
            "/sys/class/power_supply/AC/online": {"value": online},
        }
    }


class ConditionTests(unittest.TestCase):
    def observation(self, samples):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "telemetry.jsonl"
            rows = [
                {"phase": "before", **samples[0]},
                *[{"phase": "during", **sample} for sample in samples[1:-1]],
                {"phase": "after", **samples[-1]},
            ]
            path.write_text("\n".join(json.dumps(row) for row in rows) + "\n")
            return provenance.observed_conditions(path)

    def test_observed_stable_conditions_and_missing_sensors(self):
        declared = {"cpu_governor": "performance", "ac_power": True}
        observed = self.observation([condition_sample(), condition_sample()])
        self.assertEqual(observed["ac_states"], ["online"])
        self.assertEqual(provenance.condition_issues({"a": observed}, declared), [])
        unavailable = self.observation([{}, {"unavailable": "no sensors"}])
        self.assertEqual(unavailable["ac_states"], ["unavailable"])
        self.assertEqual(unavailable["governor_unavailable_samples"], 2)
        self.assertEqual(provenance.condition_issues({"a": unavailable}, declared), [])
        battery = self.observation([condition_sample(supply_type="Battery")] * 2)
        self.assertEqual(battery["ac_states"], ["unavailable"])
        two_policies = condition_sample()
        two_policies["readings"][
            "/sys/devices/system/cpu/cpufreq/policy1/scaling_governor"
        ] = {"value": "performance"}
        missing_policy = self.observation([two_policies, condition_sample()])
        self.assertEqual(missing_policy["governor_unavailable_samples"], 1)

    def test_policy_changes_and_declaration_mismatches_are_flagged(self):
        performance = self.observation([condition_sample()] * 2)
        powersave = self.observation([condition_sample(governor="powersave")] * 2)
        mixed = self.observation(
            [condition_sample(), condition_sample(governor="powersave", online="0")]
        )
        for observations in ({"a": performance, "b": powersave}, {"a": mixed}):
            issues = provenance.condition_issues(
                observations, {"cpu_governor": "performance", "ac_power": True}
            )
            self.assertTrue(any("governor changed" in issue for issue in issues))
            self.assertTrue(
                any("contradicts declared performance" in issue for issue in issues)
            )
        issues = provenance.condition_issues({"a": mixed}, {"ac_power": True})
        self.assertIn("AC power changed within session", issues)
        self.assertIn("observed AC offline contradicts declared AC power", issues)


class AcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        (self.output / "build-provenance.json").write_text('{"fixture": true}\n')
        self.environment = os.environ.copy()
        self.fingerprint = {
            "source_revision": "fixture",
            "files": {"executable": "fixture"},
        }
        self.session = {
            "schema": 1,
            "session_id": "fixture",
            "role": "laptop-lavapipe",
            "status": "running",
            "arms": [],
            "provenance": {
                "build_provenance_sha256": matrix.digest(
                    self.output / "build-provenance.json"
                ),
                "fingerprints": self.fingerprint,
                "child_environment": provenance.snapshot(self.environment),
                "executable": sys.executable,
                "conditions": {"ac_power": True, "cpu_governor": "performance"},
            },
        }
        self.configurations = {}
        self.commands = []

    def fake_child(self, name, command, output, environment, timeout):
        self.commands.append(command)
        mode = (
            "direct"
            if "--forward-direct-primary" in command
            else (
                ("two" if command[-1] == "2" else "one")
                if "--forward-recording-participants" in command
                else "auto"
            )
        )
        config = {
            "draws": int(command[command.index("--benchmark") + 1]),
            "mode": mode,
            **self.configurations.get(name, {}),
        }
        config_path = output / f"{name}.fixture.json"
        config_path.write_text(json.dumps(config))
        # The adapter checks the real runner's argv, then invokes a finite fake
        # child instead of a renderer. These directories are test evidence only.
        actual = [
            sys.executable,
            str(Path(__file__).with_name("matrix_fixture.py")),
            str(config_path),
        ]
        result = preflight.run_check(
            name, actual, output, environment, 0.1 if config.get("sleep") else timeout
        )
        result["command"] = command
        return result

    def acquire(self, fingerprints=None, telemetry=provenance.Telemetry):
        with contextlib.redirect_stdout(io.StringIO()):
            return runner.acquire(
                self.output,
                self.session,
                sys.executable,
                self.environment,
                fingerprints or (lambda: self.fingerprint),
                run=self.fake_child,
                telemetry=telemetry,
            )

    def rewrite_ledger(self, ledger):
        (self.output / "session.json").write_text(matrix.json_text(ledger))

    def test_finite_fake_children_end_to_end_and_read_only_replay(self):
        result = self.acquire()
        self.assertEqual(len(self.commands), 11)
        self.assertEqual(
            self.commands[2],
            [
                sys.executable,
                "--benchmark",
                "1",
                "--forward-recording-participants",
                "1",
            ],
        )
        self.assertEqual(
            self.commands[-1],
            [sys.executable, "--benchmark", "10000", "--forward-direct-primary"],
        )
        self.assertEqual(
            (self.output / "results.json").read_text(), matrix.json_text(result)
        )
        self.assertEqual(
            result["cells"]["1000"]["selected_arms"], ["1000-A", "1000-X", "1000-B"]
        )
        for entry in self.session["arms"]:
            observations = [
                json.loads(line)
                for line in (self.output / f"{entry['spec']['id']}.telemetry.jsonl")
                .read_text()
                .splitlines()
            ]
            self.assertEqual(observations[0]["phase"], "before")
            self.assertEqual(observations[-1]["phase"], "after")
        replay = matrix.load_session(self.output)
        self.assertEqual(matrix.json_text(replay), matrix.json_text(result))
        with self.assertRaises(report.MeasurementError):
            matrix.load_session(self.output, partial=True)

    def test_live_replacement_and_other_resolved_cell_retained(self):
        self.configurations["1000-X"] = {"overrides": {report.REGION: 30}}
        result = self.acquire()
        self.assertEqual(len(self.commands), 14)
        self.assertEqual(result["cells"]["1000"]["selected_arms"][0], "retry-1000-A")
        self.assertEqual(result["cells"]["10000"]["selected_arms"][0], "10000-A")

    def test_failed_replacement_stops_and_never_falls_back(self):
        self.configurations.update(
            {"1000-X": {"overrides": {report.REGION: 30}}, "retry-1000-X": {"exit": 4}}
        )
        with self.assertRaises(report.MeasurementError):
            self.acquire()
        self.assertEqual(self.session["status"], "failed")
        self.assertEqual(len(self.commands), 13)
        self.assertFalse((self.output / "results.json").exists())
        self.assertEqual(self.session["arms"][-1]["process"]["returncode"], 4)
        with self.assertRaises(report.MeasurementError):
            matrix.load_session(self.output)

    def fail_after_regression(self):
        self.configurations.update(
            {
                "1000-X": {"overrides": {report.REGION: 50}},
                "10000-X": {"overrides": {report.REGION: 30}},
                "retry-10000-X": {"exit": 4},
            }
        )
        with self.assertRaises(report.MeasurementError):
            self.acquire()

    def test_partial_replay_keeps_unfavorable_completed_triplets_without_authority(
        self,
    ):
        self.fail_after_regression()
        partial = matrix.load_session(self.output, partial=True)
        self.assertEqual(partial["cells"], {})
        self.assertIn("non-authoritative", partial["authority"])
        self.assertEqual(len(partial["completed_triplets"]), 2)
        small, large = partial["completed_triplets"]
        self.assertEqual(small["arms"], ["1000-A", "1000-X", "1000-B"])
        self.assertEqual(
            small["comparisons"]["A_phase"]["classification"], "regression"
        )
        self.assertEqual(
            large["comparisons"]["A_phase"]["classification"], "unresolved"
        )
        self.assertEqual(partial["rejected_arms"][0]["arm"], "retry-10000-X")
        self.assertEqual(
            (self.output / "partial-results.json").read_text(),
            matrix.json_text(partial),
        )
        rendered = matrix.markdown(partial)
        self.assertIn("FAILED SESSION — NON-AUTHORITATIVE", rendered)
        self.assertIn("regression", rendered)
        self.assertNotIn("Forward model:", rendered)
        with patch.object(
            sys,
            "argv",
            [
                "analyze_matrix.py",
                str(self.output),
                "--partial",
                "--format",
                "markdown",
            ],
        ):
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                self.assertEqual(matrix.main(), 0)
            self.assertEqual(stdout.getvalue(), rendered)

    def test_partial_replay_does_not_relax_prefix_or_tail_integrity(self):
        self.fail_after_regression()
        original = copy.deepcopy(self.session)
        for mutate in (
            lambda s: s["arms"].pop(3),
            lambda s: s["arms"][3].update(status="rejected"),
            lambda s: s["arms"][-1]["process"].update(command=["unregistered"]),
            lambda s: s["arms"][-1]["log_sha256"].update(stdout=None),
            lambda s: s["arms"][3]["environment"].update(LP_NUM_THREADS="99"),
        ):
            changed = copy.deepcopy(original)
            mutate(changed)
            self.rewrite_ledger(changed)
            with (
                self.subTest(mutate=mutate),
                self.assertRaises(report.MeasurementError),
            ):
                matrix.load_session(self.output, partial=True)
        self.rewrite_ledger(original)
        for name in ("1000-X", "retry-10000-X"):
            path = self.output / f"{name}.stdout.log"
            saved = path.read_text()
            path.write_text(saved + "tampered\n")
            with (
                self.subTest(name=name),
                self.assertRaisesRegex(report.MeasurementError, "changed stdout"),
            ):
                matrix.load_session(self.output, partial=True)
            path.write_text(saved)
        # A correct hash cannot make a malformed earlier report a valid prefix.
        path = self.output / "1000-X.stdout.log"
        path.write_text(path.read_text().replace("Presented 80 frames.", "incomplete"))
        changed = copy.deepcopy(original)
        changed["arms"][4]["log_sha256"]["stdout"] = matrix.digest(path)
        self.rewrite_ledger(changed)
        with self.assertRaises(report.MeasurementError):
            matrix.load_session(self.output, partial=True)

    def test_partial_replay_includes_completed_replacement_but_never_derives_cells(
        self,
    ):
        self.configurations.update(
            {
                "1000-X": {"overrides": {report.REGION: 30}},
                "10000-X": {"overrides": {report.REGION: 30}},
                "retry-1000-X": {"overrides": {report.REGION: 50}},
                "retry-10000-A": {"exit": 4},
            }
        )
        with self.assertRaises(report.MeasurementError):
            self.acquire()
        partial = matrix.load_session(self.output, partial=True)
        self.assertEqual(len(partial["completed_triplets"]), 3)
        replacement = next(
            t for t in partial["completed_triplets"] if t["kind"] == "replacement"
        )
        self.assertEqual(
            replacement["comparisons"]["A_phase"]["classification"], "regression"
        )
        self.assertEqual(partial["cells"], {})

    def test_governor_or_ac_change_stops_acquisition_and_is_visible_in_partial_report(
        self,
    ):
        def telemetry(path):
            # A power change, despite stable declared/environment values, is
            # detected across arms, not only inside each telemetry file.
            online = "0" if path.name.startswith("10000-A.") else "1"
            return provenance.Telemetry(
                path, sample=lambda: condition_sample(online=online)
            )

        with self.assertRaisesRegex(report.MeasurementError, "AC power changed"):
            self.acquire(telemetry=telemetry)
        self.assertEqual(len(self.commands), 7)
        partial = matrix.load_session(self.output, partial=True)
        self.assertEqual(
            partial["observed_conditions"]["10000-A"]["ac_states"], ["offline"]
        )
        rendered = matrix.markdown(partial)
        self.assertIn("performance", rendered)
        self.assertIn("offline", rendered)
        self.assertIn("contradicts declared AC", rendered)
        self.assertEqual(len(partial["completed_triplets"]), 1)

    def test_timeout_keeps_partial_logs_and_fails_not_retries(self):
        self.configurations["qualify-1000-auto"] = {"sleep": 30}
        with self.assertRaises(report.MeasurementError):
            self.acquire()
        self.assertEqual(len(self.commands), 1)
        self.assertIn("timeout", self.session["arms"][0]["process"]["error"])
        self.assertIn(
            "Phase-level", (self.output / "qualify-1000-auto.stdout.log").read_text()
        )
        self.assertEqual(matrix.load_session(self.output, partial=True)["reports"], {})

    def test_between_arm_failure_keeps_the_already_completed_triplet(self):
        calls = 0

        def changed_before_next_arm():
            nonlocal calls
            calls += 1
            if calls == 13:  # Six accepted arms, then the 10,000-A precheck.
                raise report.MeasurementError("source changed before next arm")
            return self.fingerprint

        with self.assertRaisesRegex(report.MeasurementError, "before next arm"):
            self.acquire(fingerprints=changed_before_next_arm)
        partial = matrix.load_session(self.output, partial=True)
        self.assertEqual(len(self.session["arms"]), 6)
        self.assertEqual(self.session["arms"][-1]["status"], "accepted")
        self.assertEqual(len(partial["completed_triplets"]), 1)
        self.assertEqual(partial["rejected_arms"], [])

    def test_source_change_during_arm_is_failure(self):
        calls = 0

        def changed():
            nonlocal calls
            calls += 1
            return self.fingerprint if calls == 1 else {"source_revision": "changed"}

        with self.assertRaisesRegex(report.MeasurementError, "during arm"):
            self.acquire(fingerprints=changed)
        self.assertEqual(len(self.commands), 1)

    def test_offline_rejects_changed_logs_commands_provenance_and_order(self):
        self.acquire()
        original = json.loads((self.output / "session.json").read_text())
        mutations = [
            lambda s: s["arms"].reverse(),
            lambda s: s["arms"].append(copy.deepcopy(s["arms"][0])),
            lambda s: s["arms"].pop(),
            lambda s: s["arms"][0]["process"].update(returncode=1),
            lambda s: s["arms"][0]["process"].update(command=["another-binary"]),
            lambda s: s["arms"][0]["after"].update(source_revision="changed"),
            lambda s: s["arms"][0]["environment"].update(LP_NUM_THREADS="1"),
            lambda s: s["arms"][0].update(retry_reason={"unexpected": True}),
        ]
        for mutate in mutations:
            changed = copy.deepcopy(original)
            mutate(changed)
            self.rewrite_ledger(changed)
            with (
                self.subTest(mutate=mutate),
                self.assertRaises(report.MeasurementError),
            ):
                matrix.load_session(self.output)
        self.rewrite_ledger(original)
        path = self.output / "1000-X.stdout.log"
        path.write_text(path.read_text() + "extra\n")
        with self.assertRaisesRegex(report.MeasurementError, "changed stdout"):
            matrix.load_session(self.output)

    def test_mixed_metadata_rejected_even_with_updated_log_hash(self):
        self.acquire()
        path = self.output / "1000-X.stdout.log"
        path.write_text(path.read_text().replace("Mesa fixture", "Mesa other"))
        ledger = json.loads((self.output / "session.json").read_text())
        entry = next(
            entry for entry in ledger["arms"] if entry["spec"]["id"] == "1000-X"
        )
        entry["log_sha256"]["stdout"] = matrix.digest(path)
        self.rewrite_ledger(ledger)
        with self.assertRaisesRegex(report.MeasurementError, "mixed device"):
            matrix.load_session(self.output)

    def test_missing_telemetry_or_build_metadata_invalidates_replay(self):
        self.acquire()
        for path in (
            self.output / "build-provenance.json",
            self.output / "1000-A.telemetry.jsonl",
        ):
            content = path.read_text()
            path.write_text("changed\n")
            with self.subTest(path=path), self.assertRaises(report.MeasurementError):
                matrix.load_session(self.output)
            path.write_text(content)

    def test_failure_cleanup_cannot_reseal_modified_evidence(self):
        self.acquire()
        entry = self.session["arms"][-1]
        saved = copy.deepcopy(entry)
        path = self.output / f"{entry['spec']['id']}.stdout.log"
        path.write_text("changed after seal\n")
        runner.seal_artifacts(self.output, entry)
        self.assertEqual(entry, saved)
        with self.assertRaisesRegex(report.MeasurementError, "changed stdout"):
            matrix.load_session(self.output)

    def test_interruption_marks_incomplete_no_resume(self):
        def interrupt(*_):
            raise KeyboardInterrupt

        with (
            contextlib.redirect_stdout(io.StringIO()),
            self.assertRaises(KeyboardInterrupt),
        ):
            runner.acquire(
                self.output,
                self.session,
                sys.executable,
                self.environment,
                lambda: self.fingerprint,
                run=interrupt,
            )
        ledger = json.loads((self.output / "session.json").read_text())
        self.assertEqual(ledger["status"], "failed")
        self.assertEqual(ledger["failure"], "KeyboardInterrupt")
        with self.assertRaises(report.MeasurementError):
            matrix.load_session(self.output)
        partial = matrix.load_session(self.output, partial=True)
        self.assertEqual(partial["reports"], {})
        self.assertEqual(
            ledger["arms"][0]["log_sha256"], {"stdout": None, "stderr": None}
        )


class ProvenanceTests(unittest.TestCase):
    def test_build_contract_pins_both_shaders_compiler_and_dependency_status(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            cache = (
                f"CMAKE_HOME_DIRECTORY:INTERNAL={provenance.ROOT}\n"
                "CMAKE_BUILD_TYPE:STRING=Release\n"
                "CMAKE_CACHE_MAJOR_VERSION:INTERNAL=4\n"
                "CMAKE_CACHE_MINOR_VERSION:INTERNAL=4\n"
                "CMAKE_CACHE_PATCH_VERSION:INTERNAL=4\n"
                f"VCPKG_INSTALLED_DIR:PATH={build / 'installed'}\n"
            )
            (build / "CMakeCache.txt").write_text(cache)
            files = provenance.build_files(build)
            self.assertEqual(files["forward_shader"], build / "shaders/forward.spv")
            self.assertEqual(files["shadow_shader"], build / "shaders/shadow.spv")
            self.assertEqual(
                files["compiler"], build / "CMakeFiles/4.4.4/CMakeCXXCompiler.cmake"
            )
            self.assertEqual(files["dependencies"], build / "installed/vcpkg/status")
            for invalid in (
                cache.replace("Release", "Debug"),
                cache.replace(str(provenance.ROOT), directory),
                cache + "CMAKE_CXX_FLAGS:STRING=-fsanitize=thread\n",
            ):
                (build / "CMakeCache.txt").write_text(invalid)
                with self.assertRaises(report.MeasurementError):
                    provenance.build_files(build)

    def test_fingerprint_requires_clean_tracked_sources_and_reads_real_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "binary"
            artifact.write_bytes(b"first")
            with patch.object(provenance, "git_output", side_effect=["", "revision"]):
                first = provenance.fingerprints({"binary": artifact})
            artifact.write_bytes(b"changed")
            with patch.object(provenance, "git_output", side_effect=["", "revision"]):
                changed = provenance.fingerprints({"binary": artifact})
            self.assertNotEqual(first, changed)
            with (
                patch.object(
                    provenance, "git_output", return_value=" M src/app/main.cpp"
                ),
                self.assertRaises(report.MeasurementError),
            ):
                provenance.fingerprints({"binary": artifact})

    def test_instrumentation_rejected_but_driver_settings_preserved(self):
        for key in provenance.FORBIDDEN:
            with self.subTest(key=key), self.assertRaises(report.MeasurementError):
                provenance.measurement_environment({key: "1"}, None)
        inherited = {
            "LP_NUM_THREADS": "4",
            "MESA_DEBUG": "context",
            "__NV_PRIME_RENDER_OFFLOAD": "1",
        }
        child = provenance.measurement_environment(inherited, Path("/icd.json"))
        self.assertEqual({key: child[key] for key in inherited}, inherited)
        self.assertNotIn("VK_DRIVER_FILES", inherited)
        self.assertEqual(child["VK_DRIVER_FILES"], "/icd.json")

    def test_missing_sensors_are_named_not_assumed_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            sample = provenance.host_sample(Path(directory), Path(directory))
            self.assertIn("unavailable", sample["cpu_counters"])
            self.assertEqual(sample["readings"], {})
            self.assertIn("no sysfs sensors", sample["availability_note"])

    def test_host_readings_preserve_governor_clock_and_sensor_values(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            readings = {
                "devices/system/cpu/cpufreq/policy0/scaling_governor": "performance",
                "devices/system/cpu/cpufreq/policy0/scaling_cur_freq": "3900000",
                "devices/system/cpu/cpufreq/policy0/cpuinfo_cur_freq": "3200000",
                "devices/system/cpu/cpu0/thermal_throttle/core_throttle_count": "4",
                "class/thermal/thermal_zone0/type": "cpu",
                "class/thermal/thermal_zone0/temp": "72000",
                "class/hwmon/hwmon0/temp1_input": "71000",
                "class/power_supply/AC/online": "1",
            }
            for relative, value in readings.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value + "\n")
            (root / "stat").write_text("cpu 1 2 3 4\n")
            sample = provenance.host_sample(root, root)
            for relative, value in readings.items():
                self.assertEqual(
                    sample["readings"][str(root / relative)], {"value": value}
                )
            self.assertEqual(sample["cpu_counters"], {"value": "cpu 1 2 3 4"})
            self.assertIn(
                "unavailable",
                sample["readings"][str(root / "class/power_supply/AC/status")],
            )

    def test_low_rate_sampler_keeps_before_during_after(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "telemetry.jsonl"
            sampled = threading.Event()
            calls = []

            def sample():
                calls.append(1)
                if len(calls) >= 2:
                    sampled.set()
                return {"fixture": 1}

            with provenance.Telemetry(path, sample=sample):
                self.assertTrue(
                    sampled.wait(5), "observer never sampled during the arm"
                )
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(rows[0]["phase"], "before")
            self.assertEqual(rows[-1]["phase"], "after")
            self.assertTrue(all(row["phase"] == "during" for row in rows[1:-1]))
            self.assertGreaterEqual(len(rows), 3)

    def test_failed_sensor_is_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "telemetry.jsonl"
            with provenance.Telemetry(
                path,
                sample=lambda: (_ for _ in ()).throw(OSError("sensor unavailable")),
            ):
                pass
            self.assertTrue(
                all(
                    "unavailable" in json.loads(line)
                    for line in path.read_text().splitlines()
                )
            )

    def test_placeholder_conditions_and_battery_rejected(self):
        conditions = {
            "ac_power": True,
            "cpu_governor": "performance",
            "background_load": "idle",
            "cooldown": "10 minutes",
            "presentation_route": "not established",
            "implicit_layers": "not established",
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "conditions.json"
            path.write_text(json.dumps(conditions))
            self.assertEqual(
                provenance.load_conditions(path, "laptop-nvidia"), conditions
            )
            for changed in (
                {**conditions, "ac_power": False},
                {**conditions, "cooldown": "REPLACE me"},
            ):
                path.write_text(json.dumps(changed))
                with self.assertRaises(report.MeasurementError):
                    provenance.load_conditions(path, "laptop-nvidia")


if __name__ == "__main__":
    unittest.main()
