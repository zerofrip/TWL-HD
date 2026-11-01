#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Tests for tools/emulator/desmume_capture.sh.

Drives the capture script with a fake DESMUME_CLI and tiny timeouts so the
in-script watchdog can be exercised without a real emulator.

Runs under `make test`:

    PYTHONDONTWRITEBYTECODE=1 python3 -B -I -m unittest discover -s tools
"""

import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
EMULATOR_DIR = os.path.join(TOOLS_DIR, "emulator")
SCRIPT = os.path.join(EMULATOR_DIR, "desmume_capture.sh")

SLOT2_LINE = "Slot 2: MPCF Flash Card Device"
DLDI_LINE = "AUTO-PATCHING DLDI"


def _command_words(script):
    """Yield bare command words from *script*, ignoring comments and quoted
    string content.

    A word is yielded when it appears in command position: at the start of a
    command list, after ;, &&, ||, |, &, $(, `, or an assignment prefix such
    as VAR=value, and after the shell keywords if/then/elif/else/do/while/
    until/time/command/builtin/nohup/xargs/env/exec.  An unescaped newline
    starts a new command; an escaped newline (backslash + newline) does not.
    """
    COMMAND_KEYWORDS = frozenset((
        "if", "then", "elif", "else", "do", "while", "until",
        "time", "command", "builtin", "nohup", "xargs", "env", "exec",
    ))
    in_quote = None
    current = []
    command_position = True
    i = 0
    n = len(script)

    def flush():
        nonlocal command_position
        if not current:
            return None
        word = "".join(current)
        del current[:]
        yielded = word if command_position else None
        if "=" in word and not word.startswith("="):
            command_position = True
        elif word in COMMAND_KEYWORDS:
            command_position = True
        else:
            command_position = False
        return yielded

    while i < n:
        c = script[i]

        if in_quote == "'":
            if c == "'":
                in_quote = None
            i += 1
            continue
        if in_quote == '"':
            if c == "\\" and i + 1 < n:
                i += 2
                continue
            if c == '"':
                in_quote = None
            i += 1
            continue

        if c == "\\" and i + 1 < n and script[i + 1] == "\n":
            # Escaped newline does not start a new command.
            i += 2
            continue

        if c == "#":
            while i < n and script[i] != "\n":
                i += 1
            continue

        if c in "'\"":
            in_quote = c
            i += 1
            continue

        if c == "\n":
            word = flush()
            if word is not None:
                yield word
            command_position = True
            i += 1
            continue

        if c.isspace():
            word = flush()
            if word is not None:
                yield word
            i += 1
            continue

        if c == ";":
            flush()
            command_position = True
            i += 1
            continue

        if c == "&":
            flush()
            command_position = True
            i += 2 if i + 1 < n and script[i + 1] == "&" else 1
            continue

        if c == "|":
            flush()
            command_position = True
            i += 2 if i + 1 < n and script[i + 1] == "|" else 1
            continue

        if c == "$" and i + 1 < n and script[i + 1] == "(":
            flush()
            command_position = True
            i += 2
            continue

        if c == "`":
            flush()
            command_position = True
            i += 1
            continue

        if c == "!":
            if i + 1 >= n or script[i + 1] != "=":
                flush()
                command_position = True
                i += 1
                continue
            current.append("!")
            i += 1
            continue

        if c in "({":
            flush()
            command_position = True
            i += 1
            continue

        if c in ")}":
            flush()
            i += 1
            continue

        current.append(c)
        i += 1

    word = flush()
    if word is not None:
        yield word


class EmulatorCaptureScriptTest(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.mkdtemp(prefix="twlhd-emu-capture-")
        self.addCleanup(shutil.rmtree, self.tmpdir)

        self.rom = os.path.join(self.tmpdir, "fake.nds")
        open(self.rom, "w", encoding="utf-8").close()
        self.outdir = os.path.join(self.tmpdir, "out")
        self.pidfile = os.path.join(self.tmpdir, "fake.pid")
        self.fake_cli = os.path.join(self.tmpdir, "desmume-fake")
        # Default executable fake (overwritten by write_fake when needed).
        with open(self.fake_cli, "w", encoding="utf-8") as handle:
            handle.write("#!/bin/bash\n")
            handle.write('echo "$$" > "${TWLHD_FAKE_PID_FILE}"\n')
            handle.write("sleep 60\n")
        os.chmod(self.fake_cli,
                 stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

        missing = [t for t in ("bash", "mkfs.fat", "7z", "stdbuf")
                   if shutil.which(t) is None]
        if missing:
            self.fail("required tool(s) not found: %s" % ", ".join(missing))

    def write_fake(self, body):
        with open(self.fake_cli, "w", encoding="utf-8") as handle:
            handle.write("#!/bin/bash\n")
            handle.write('echo "$$" > "${TWLHD_FAKE_PID_FILE}"\n')
            handle.write(body)
        os.chmod(self.fake_cli,
                 stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

    def run_script(self, env=None, extra_timeout=30):
        base_env = os.environ.copy()
        base_env["DESMUME_TIMEOUT"] = "2"
        base_env["DESMUME_INT_GRACE"] = "2"
        base_env["TWLHD_FAKE_PID_FILE"] = self.pidfile
        base_env["DESMUME_CLI"] = self.fake_cli
        if env:
            base_env.update(env)
        return subprocess.run(
            ["bash", SCRIPT, self.rom, self.outdir],
            env=base_env, capture_output=True, text=True,
            timeout=extra_timeout)

    def assert_no_fake_left(self):
        if not os.path.exists(self.pidfile):
            return
        with open(self.pidfile, "r", encoding="utf-8") as handle:
            pid_text = handle.read().strip()
        if not pid_text:
            return
        try:
            os.kill(int(pid_text), 0)
        except ProcessLookupError:
            return
        self.fail("fake emulator process %s is still alive" % pid_text)

    def test_successful_sigint_path_fails_later_at_missing_capture(self):
        self.write_fake(
            'echo "%s"\n'
            'echo "%s"\n'
            'trap "exit 0" INT\n'
            'while true; do :; done\n' % (SLOT2_LINE, DLDI_LINE))
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("twlhd_frame_v2.bin was not written to the FAT image",
                      completed.stderr)
        self.assert_no_fake_left()

    def test_sigint_ignored_escalates_to_sigkill_and_reports_corruption(self):
        self.write_fake(
            'echo "%s"\n'
            'echo "%s"\n'
            'trap "" INT\n'
            'while true; do :; done\n' % (SLOT2_LINE, DLDI_LINE))
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("did not exit on SIGINT", completed.stderr)
        self.assertIn("may be unflushed/corrupt", completed.stderr)
        self.assert_no_fake_left()

    def test_early_exit_reports_status(self):
        self.write_fake(
            'echo "%s"\n'
            'echo "%s"\n'
            'exit 3\n' % (SLOT2_LINE, DLDI_LINE))
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("DeSmuME exited with status 3", completed.stderr)
        self.assert_no_fake_left()

    def test_sigint_reports_real_emulator_exit_status(self):
        # The emulator exits with status 7 in response to SIGINT. This test
        # covers the path where the watchdog fires, SIGINT is sent, and the
        # emulator exits (status 7) during the grace period. It requires the
        # reported status to be the emulator's (from wait "$emulator_pid"),
        # catching either a hard-coded status or one taken from the grace
        # sleeper. It runs that branch but does NOT reliably trigger the race
        # where the sleeper is reaped first.
        self.write_fake(
            'echo "%s"\n'
            'echo "%s"\n'
            'trap "exit 7" INT\n'
            'while true; do :; done\n' % (SLOT2_LINE, DLDI_LINE))
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("DeSmuME exited with status 7", completed.stderr)
        self.assert_no_fake_left()

    def test_sigint_without_device_lines_fails_device_check(self):
        self.write_fake(
            'trap "exit 0" INT\n'
            'while true; do :; done\n')
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("did not report both the Slot-2 MPCF device and",
                      completed.stderr)
        self.assert_no_fake_left()

    def test_only_one_device_line_fails_device_check(self):
        self.write_fake(
            'echo "%s"\n'
            'trap "exit 0" INT\n'
            'while true; do :; done\n' % SLOT2_LINE)
        completed = self.run_script()
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("did not report both the Slot-2 MPCF device and",
                      completed.stderr)
        self.assert_no_fake_left()

    def _assert_env_rejected(self, env_update, message):
        completed = self.run_script(env=env_update)
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertIn(message, completed.stderr)
        self.assertFalse(os.path.exists(self.pidfile))

    def test_invalid_timeout_abc_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "abc", "DESMUME_INT_GRACE": "2"},
            "DESMUME_TIMEOUT must be a positive integer")

    def test_invalid_timeout_zero_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "0", "DESMUME_INT_GRACE": "2"},
            "DESMUME_TIMEOUT must be a positive integer")

    def test_invalid_timeout_negative_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "-1", "DESMUME_INT_GRACE": "2"},
            "DESMUME_TIMEOUT must be a positive integer")

    def test_invalid_grace_abc_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "2", "DESMUME_INT_GRACE": "abc"},
            "DESMUME_INT_GRACE must be a positive integer")

    def test_invalid_grace_zero_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "2", "DESMUME_INT_GRACE": "0"},
            "DESMUME_INT_GRACE must be a positive integer")

    def test_invalid_grace_negative_is_rejected(self):
        self._assert_env_rejected(
            {"DESMUME_TIMEOUT": "2", "DESMUME_INT_GRACE": "-1"},
            "DESMUME_INT_GRACE must be a positive integer")

    def test_desmume_cli_unset(self):
        completed = self.run_script(env={"DESMUME_CLI": ""})
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("DESMUME_CLI is not set", completed.stderr)

    def test_desmume_cli_not_executable(self):
        not_exec = os.path.join(self.tmpdir, "not-executable")
        open(not_exec, "w", encoding="utf-8").close()
        completed = self.run_script(env={"DESMUME_CLI": not_exec})
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("is not executable", completed.stderr)

    def test_script_does_not_invoke_timeout_command(self):
        with open(SCRIPT, "r", encoding="utf-8") as handle:
            text = handle.read()
        # Keep the cheap textual guard as a first line of defence, but tabs
        # are whitespace too.
        self.assertIsNone(re.search(r"timeout[ \t]", text))
        for word in _command_words(text):
            self.assertNotEqual(word, "timeout",
                                "script invokes the timeout command")

    def test_timeout_command_checker(self):
        # Positive cases: timeout must be detected as a command word.
        self.assertIn("timeout", list(_command_words("timeout 5 sleep 1")))
        self.assertIn("timeout",
                      list(_command_words("exec timeout -k 1 sleep")))
        self.assertIn("timeout",
                      list(_command_words("x=$(timeout 1 true)")))
        # Reviewer probes: newline, shell keywords, !, {, function body, tab.
        self.assertIn("timeout",
                      list(_command_words("echo hi\ntimeout 5 sleep 1")))
        self.assertIn("timeout",
                      list(_command_words("if timeout 5 true; then :; fi")))
        self.assertIn("timeout",
                      list(_command_words("! timeout 1 x")))
        self.assertIn("timeout",
                      list(_command_words("{ timeout 1 x; }")))
        self.assertIn("timeout",
                      list(_command_words("f() {\n  timeout 1 y\n}")))
        self.assertIn("timeout", list(_command_words("timeout\t1 x")))
        # Negative cases: timeout is not in command position here.
        self.assertNotIn("timeout",
                         list(_command_words("echo timeout 5")))
        self.assertNotIn("timeout",
                         list(_command_words("# timeout 5 sleep 1")))
        self.assertNotIn("timeout",
                         list(_command_words('echo "timeout is ok"')))
        self.assertNotIn("timeout",
                         list(_command_words("TIMEOUT_VAL=5; echo $TIMEOUT_VAL")))
        # False positive: [ "$a" != timeout ] is rejected by the textual
        # guard (timeout followed by space/tab) even though _command_words
        # sees it as a bare word; this fails safe.
        self.assertIn("timeout", list(_command_words('[ "$a" != timeout ]')))

    def test_timeout_checker_known_gap_and_textual_guard(self):
        # _command_words alone does not see every way a timeout invocation can
        # be written; the textual regex guard `timeout[ \t]` catches these
        # known cases and is pinned here as the compensating defence.
        hidden_forms = (
            "stdbuf -oL timeout 1 x",
            "env -u X timeout 1 x",
            '"$(timeout 1 x)"',
            "/usr/bin/timeout 1 x",
        )
        for form in hidden_forms:
            with self.subTest(form=form):
                # The checker misses the bare command word...
                self.assertNotIn("timeout", list(_command_words(form)))
                # ...but the textual regex guard catches it.
                self.assertIsNotNone(re.search(r"timeout[ \t]", form))

    def test_real_script_command_words_sane_and_no_timeout(self):
        with open(SCRIPT, "r", encoding="utf-8") as handle:
            text = handle.read()
        words = list(_command_words(text))
        # Line starts are seen: these should appear at command position.
        for sane in ("set", "if", "echo", "exit"):
            self.assertIn(sane, words)
        # The real script must never invoke the timeout command.
        self.assertNotIn("timeout", words)

    def test_script_passes_bash_syntax_check(self):
        completed = subprocess.run(
            ["bash", "-n", SCRIPT], capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
