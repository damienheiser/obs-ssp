"""Exercise the real build-number module without downloading build dependencies."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "common/buildnumber.cmake"


class BuildNumberTest(unittest.TestCase):
    def check_number(self, expected, cached=None, ci=False, run_id=None, override=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "cmake").mkdir()
            cache = root / "cmake/.CMakeBuildNumber"
            if cached is not None:
                cache.write_text(cached)
            script = root / "check.cmake"
            script.write_text(
                f'set(CMAKE_CURRENT_SOURCE_DIR "{root}")\n'
                + (f'set(PLUGIN_BUILD_NUMBER "{override}")\n' if override else "")
                + f'include("{MODULE}")\n'
                + f'if(NOT PLUGIN_BUILD_NUMBER STREQUAL "{expected}")\n'
                + '  message(FATAL_ERROR "Unexpected number: ${PLUGIN_BUILD_NUMBER}")\n'
                + 'endif()\n'
            )
            env = {k: v for k, v in os.environ.items()
                   if k not in ("CI", "GITHUB_RUN_ID", "GITLAB_RUN_ID")}
            if ci:
                env["CI"] = "true"
            if run_id:
                env["GITHUB_RUN_ID"] = run_id
            subprocess.run(["cmake", "-P", str(script)], env=env, check=True)

    def test_missing_submodule_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            thirdparty = MODULE.parents[2] / "thirdpty/CMakeLists.txt"
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(check NONE)\n'
                + thirdparty.read_text()
            )
            result = subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("git submodule update --init --recursive", result.stderr)

    def test_first_local_configure(self):
        self.check_number("1")

    def test_empty_cache(self):
        self.check_number("1", cached="")

    def test_reconfigure(self):
        self.check_number("8", cached="7")

    def test_ci_number(self):
        self.check_number("123", ci=True, run_id="123")

    def test_ci_fallback(self):
        self.check_number("1", ci=True)

    def test_override(self):
        self.check_number("42", override="42")


if __name__ == "__main__":
    unittest.main()
