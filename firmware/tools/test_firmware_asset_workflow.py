#!/usr/bin/env python3
"""Offline contracts for firmware asset PR authentication and admission."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import textwrap
import unittest


WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/build-firmware-assets.yml"
TOKEN = "${{ secrets.FIRMWARE_PR_TOKEN }}"


def steps():
    """Read the workflow's consistently indented steps without extra dependencies."""
    return re.findall(r"(?ms)^      - name: .*?(?=^      - name: |^  [a-z][\w-]*:|\Z)", WORKFLOW.read_text())


class FirmwareAssetWorkflowTests(unittest.TestCase):
    def test_both_main_pr_paths_use_dedicated_token_and_report_failures(self):
        pr_steps = [step for step in steps() if "uses: peter-evans/create-pull-request@v7" in step]
        self.assertEqual(len(pr_steps), 2, "cover automatic and manual-on-main PR creation")
        for step in pr_steps:
            with self.subTest(step=step.splitlines()[0]):
                self.assertIn(f"          token: {TOKEN}\n", step)
                self.assertIn("          base: main\n", step)
                self.assertIn("          branch: automation/update-firmware-assets\n", step)
                self.assertNotIn("continue-on-error:", step)
        self.assertNotIn("gh pr merge", WORKFLOW.read_text())
        self.assertNotIn("enable-pull-request-automerge", WORKFLOW.read_text())

    def test_preflight_rejects_missing_secret_without_printing_present_value(self):
        preflight = [step for step in steps() if "name: Check firmware PR token" in step]
        self.assertEqual(len(preflight), 1, "admit main PR work before starting expensive build jobs")
        step = preflight[0]
        self.assertIn("github.ref_name == 'main'", step)
        self.assertIn("github.event_name == 'push' || inputs.commit_artifacts == true", step)
        self.assertIn("steps.plan.outputs.has_envs == 'true'", step)
        self.assertIn(f"          FIRMWARE_PR_TOKEN: {TOKEN}\n", step)
        self.assertLess(WORKFLOW.read_text().index("name: Plan selected variants"),
                        WORKFLOW.read_text().index("name: Check firmware PR token"))
        self.assertLess(WORKFLOW.read_text().index("name: Check firmware PR token"),
                        WORKFLOW.read_text().index("  build-manual-artifacts:"))
        script = textwrap.dedent(step.split("        run: |\n", 1)[1])
        with tempfile.TemporaryDirectory() as directory:
            summary = Path(directory) / "summary.md"
            env = {**os.environ, "GITHUB_STEP_SUMMARY": str(summary)}
            env.pop("FIRMWARE_PR_TOKEN", None)
            missing = subprocess.run(["bash", "-eu", "-c", script], env=env,
                                     text=True, capture_output=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("FIRMWARE_PR_TOKEN", missing.stdout + missing.stderr)
            self.assertIn("FIRMWARE_PR_TOKEN", summary.read_text())
            # Inert presence marker, not a credential or authentication simulation.
            marker = "offline-presence-marker-not-a-token"
            env["FIRMWARE_PR_TOKEN"] = marker
            present = subprocess.run(["bash", "-eu", "-c", script], env=env,
                                     text=True, capture_output=True)
            self.assertEqual(present.returncode, 0, present.stderr)
            self.assertNotIn(marker, present.stdout + present.stderr + summary.read_text())


if __name__ == "__main__":
    unittest.main()
