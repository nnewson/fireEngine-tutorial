"""Publish hosted measurement completeness, never a performance pass/fail gate."""

import argparse
import os
from pathlib import Path

from analyze_matrix import load_session, markdown
from benchmark_report import require

REPLAY_ERRORS = (OSError, ValueError, KeyError, TypeError)


def replay_hosted(directory, partial=False):
    result = load_session(directory, partial=partial)
    require(result["role"] == "hosted-lavapipe", "not a hosted Lavapipe session")
    return result


def summarize(output_root, outcome):
    lines = [
        "# Hosted Lavapipe benchmark — observational only",
        "",
        "This session cannot replace either laptop policy result. Timing values",
        "and drift classifications do not determine correctness CI success.",
        "",
        f"Acquisition step outcome (before continue-on-error): `{outcome or 'not run'}`.",
        "",
    ]
    directory = None
    try:
        sessions = sorted(output_root.glob("matrix-*"))
        require(
            len(sessions) == 1, f"expected one session directory, found {len(sessions)}"
        )
        directory = sessions[0]
        require(outcome == "success", "acquisition step did not succeed")
        result = replay_hosted(directory)
    except REPLAY_ERRORS as error:
        lines += [
            "**INCOMPLETE — no admitted complete measurement.**",
            "",
            str(error),
            "",
        ]
        if directory is not None:
            try:
                partial = replay_hosted(directory, partial=True)
            except REPLAY_ERRORS as partial_error:
                lines += [f"No verified partial replay: {partial_error}", ""]
            else:
                lines += [markdown(partial)]
        lines += [
            "Keep acquisition.log and all partial artifacts in linux-benchmark-evidence.",
            "An interrupted/unsealed ledger is not completion and cannot be resumed.",
            "",
        ]
        return "\n".join(lines), False
    lines += ["**COMPLETE — raw evidence replay verified.**", "", markdown(result)]
    return "\n".join(lines), True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument(
        "--outcome", required=True, help="GitHub step outcome, not conclusion"
    )
    args = parser.parse_args()
    text, complete = summarize(args.output_root, args.outcome)
    args.output_root.mkdir(parents=True, exist_ok=True)
    (args.output_root / "summary.md").write_text(text)
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(summary).open("a") as stream:
            stream.write(text)
    print(text, end="")
    if not complete:
        print(
            "::warning title=Hosted benchmark incomplete::See summary and linux-benchmark-evidence; no complete measurement admitted."
        )
    # Reporting an incomplete observation is successful reporting, not a clean
    # acquisition. The original step outcome, warning and banner retain failure.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
