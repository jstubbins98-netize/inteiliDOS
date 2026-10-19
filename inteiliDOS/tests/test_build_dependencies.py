#!/usr/bin/env python3
"""Hermetic build-prompt tests; no packages are installed and no OS is compiled."""
from pathlib import Path
import os
import pty
import select
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which("bash")
SOURCE = (ROOT / "build.sh").read_text()
FUNCTIONS = SOURCE.split('if [[ "$USE_DEFAULTS" -eq 1 || ! -t 0 ]]; then')[0]


class DependencyPromptTests(unittest.TestCase):
    def run_prompt(self, answer, fail=False, saved_env=False, prefix_bin=False):
        with tempfile.TemporaryDirectory(prefix="willburd deps ") as folder:
            root = Path(folder)
            prefix = root / "custom toolchain"
            prefix.mkdir()
            if prefix_bin:
                (prefix / "bin").mkdir()
            if saved_env:
                (prefix / "env.sh").write_text('export PATH="$PREFIX/new-tools:$PATH"\n')
            (root / "install-deps.sh").write_text(
                '#!/usr/bin/env bash\n'
                'echo INSTALLER-RAN\n'
                + ('exit 7\n' if fail else
                   'mkdir -p "$PREFIX"\n'
                   'printf \'export PATH="$PREFIX/new-tools:$PATH"\\n\' > "$PREFIX/env.sh"\n'))
            # Use actual functions, including ask_menu, without executing the build.
            harness = FUNCTIONS + '\nSCRIPT_DIR="$TEST_ROOT"\nprepare_dependencies\nprintf "FINAL_PATH=%s\\n" "$PATH"\n'
            script = root / "prompt.sh"
            script.write_text(harness)
            result = subprocess.run([BASH, str(script)], input=answer, text=True,
                                    capture_output=True, timeout=10,
                                    env=dict(os.environ, HOME=str(root), PREFIX=str(prefix),
                                             TEST_ROOT=str(root)))
            return result, str(prefix)

    def test_no_runs_installer_and_loads_new_path(self):
        result, prefix = self.run_prompt("2\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Have you installed the build dependencies?", result.stdout)
        self.assertEqual(result.stdout.count("INSTALLER-RAN"), 1)
        self.assertIn("FINAL_PATH=" + prefix + "/new-tools:", result.stdout)

    def test_yes_skips_installer_and_loads_existing_path(self):
        result, prefix = self.run_prompt("1\n", saved_env=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("INSTALLER-RAN", result.stdout)
        self.assertIn("FINAL_PATH=" + prefix + "/new-tools:", result.stdout)

    def test_default_and_eof_never_install(self):
        for answer in ("\n", ""):
            result, _ = self.run_prompt(answer)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("INSTALLER-RAN", result.stdout)

    def test_help_and_invalid_choice_before_install(self):
        result, _ = self.run_prompt("H\ninvalid\n2\n")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("administrator access", result.stdout)
        self.assertIn("Choose a number", result.stdout)
        self.assertEqual(result.stdout.count("INSTALLER-RAN"), 1)

    def test_failed_install_stops_build(self):
        result, _ = self.run_prompt("2\n", fail=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Dependency installation failed", result.stderr)
        self.assertNotIn("FINAL_PATH=", result.stdout)

    def test_existing_linux_prefix_without_env_file(self):
        result, prefix = self.run_prompt("1\n", prefix_bin=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FINAL_PATH=" + prefix + "/bin:", result.stdout)

    def test_defaults_config_only_never_prompts_or_installs(self):
        with tempfile.TemporaryDirectory(prefix="willburd config ") as folder:
            root = Path(folder)
            build = root / "build.sh"
            build.write_text(SOURCE)
            (root / "install-deps.sh").write_text("echo UNEXPECTED-INSTALL >&2\nexit 9\n")
            result = subprocess.run([BASH, str(build), "--defaults", "--config-only"],
                                    text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("UNEXPECTED-INSTALL", result.stderr)
            self.assertNotIn("Have you installed", result.stdout)
            self.assertTrue((root / "configurations/config.h").is_file())

    def test_full_interactive_wizard_runs_installer_before_tool_checks(self):
        with tempfile.TemporaryDirectory(prefix="willburd full ") as folder:
            root = Path(folder)
            build = root / "build.sh"
            build.write_text(SOURCE)
            tools = root / "tools"
            tools.mkdir()
            for name in ("cmake", "nasm", "i686-elf-ld", "i686-elf-objcopy",
                         "grub-mkrescue", "xorriso"):
                tool = tools / name
                tool.write_text("#!" + BASH + "\necho STUB-" + name + "\n")
                tool.chmod(0o755)
            installer = root / "install-deps.sh"
            installer.write_text(
                'echo INSTALLER-RAN\nmkdir -p "$PREFIX/new-tools"\n'
                'printf \'export PATH="$PREFIX/new-tools:$PATH"\\n\' > "$PREFIX/env.sh"\n'
                'printf \'#!/usr/bin/env bash\\nexit 0\\n\' > "$PREFIX/new-tools/i686-elf-gcc"\n'
                'chmod +x "$PREFIX/new-tools/i686-elf-gcc"\n')
            env = dict(os.environ, HOME=str(root), PREFIX=str(root / "prefix"),
                       PATH=str(tools) + ":" + os.environ["PATH"])
            master, slave = pty.openpty()
            proc = subprocess.Popen([BASH, str(build), "--configure-only"], stdin=slave,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
            output, pending = "", ""
            deadline = time.monotonic() + 15
            try:
                while proc.poll() is None:
                    self.assertLess(time.monotonic(), deadline, output)
                    if select.select([proc.stdout], [], [], .1)[0]:
                        chunk = os.read(proc.stdout.fileno(), 65536).decode()
                        output += chunk
                        pending += chunk
                        if any(marker in pending for marker in ("Choice [", "Number [", "Name [")):
                            answer = b"2\n" if "Have you installed the build dependencies?" in pending else b"\n"
                            os.write(master, answer)
                            pending = ""
                output += proc.stdout.read().decode()
                self.assertEqual(proc.returncode, 0, output)
                self.assertEqual(output.count("INSTALLER-RAN"), 1)
                self.assertLess(output.index("INSTALLER-RAN"), output.index("Now I shall inspect"))
                self.assertIn("STUB-cmake", output)
                # Piped/default builds must not invoke an installer implicitly.
                for flags in ([], ["--defaults"]):
                    result = subprocess.run([BASH, str(build), "--configure-only", *flags],
                                            input="", text=True, capture_output=True, env=env, timeout=10)
                    self.assertNotIn("INSTALLER-RAN", result.stdout)
                    self.assertNotIn("Have you installed the build dependencies?", result.stdout)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdout.close()
                os.close(master)
                os.close(slave)


if __name__ == "__main__":
    unittest.main()
