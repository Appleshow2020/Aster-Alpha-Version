#!/usr/bin/env python3
"""Compare the aster.* java.base port against a real JDK.

Every NAME.ast file in Lib/test/test_java is executed by the Aster binary, and
NAME.java is executed by the locally installed JDK. Both programs must exit
successfully and print identical stdout. The JDK is a test-time reference only:
the interpreter never depends on Java at runtime.

If no `java` executable is found the checks are skipped, unless --require-jdk is
given. Each test file is self-contained: it imports the ported API, exercises
the same calls as its Java twin, and prints the results.

Usage:
    python3 Tools/check_java.py
    python3 Tools/check_java.py --require-jdk
    python3 Tools/check_java.py -v
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ORACLE_DIR = ROOT / "Lib/test/test_java"
JAVA_OPTS = ["-Duser.timezone=UTC", "-Duser.language=en", "-Duser.country=US"]

sys.path.insert(0, str(Path(__file__).resolve().parent))
import java_oracle  # noqa: E402
import random_suite  # noqa: E402


def find_binary() -> Path:
    names = ("aster.exe", "aster") if os.name == "nt" else ("aster", "aster.exe")
    for name in names:
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    sys.exit("Aster binary not found. Run `make` first.")


def run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=120,
    )


def first_difference(expected: str, got: str) -> str:
    want_lines = expected.splitlines()
    got_lines = got.splitlines()
    for index, (want, actual) in enumerate(zip(want_lines, got_lines)):
        if want != actual:
            return f"first difference at line {index + 1}: got={actual!r} want={want!r}"
    if len(want_lines) != len(got_lines):
        return f"line count differs: got={len(got_lines)} want={len(want_lines)}"
    return "no difference"


def check_pair(binary: Path, java: str, aster_path: Path) -> list[str]:
    java_path = aster_path.with_suffix(".java")
    if not java_path.is_file():
        return ["missing Java twin"]

    aster = run([str(binary), str(aster_path)])
    if aster.returncode != 0:
        detail = " / ".join(aster.stderr.strip().splitlines()[:3])
        return [f"Aster exit code {aster.returncode}"] + ([detail] if detail else [])

    reference = run([java, *JAVA_OPTS, str(java_path)])
    if reference.returncode != 0:
        detail = " / ".join(reference.stderr.strip().splitlines()[:3])
        return [f"Java exit code {reference.returncode}"] + ([detail] if detail else [])

    if aster.stdout != reference.stdout:
        return [first_difference(reference.stdout, aster.stdout)]
    return []


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument(
        "--require-jdk",
        action="store_true",
        help="fail instead of skipping when no JDK is installed",
    )
    parser.add_argument("--seed", type=int, default=None, help="Random oracle seed (default: newly generated each run)")
    parser.add_argument("--no-random", action="store_true", help="Skip random oracle validation")
    args = parser.parse_args()

    java = shutil.which("java")
    if java is None:
        message = "java not found on PATH: skipping Java oracle tests"
        require_jdk = args.require_jdk or os.environ.get("ASTER_REQUIRE_JDK") == "1"
        if require_jdk:
            sys.exit(f"{message} (a JDK is required)")
        print(f"{message} (install a JDK to enable them)")
        return 0

    binary = find_binary()
    if args.verbose:
        print(f"binary: {binary}")
        print(f"java: {java}")

    if not ORACLE_DIR.is_dir():
        sys.exit(f"directory not found: {ORACLE_DIR}")

    total = 0
    failures = []
    for path in sorted(ORACLE_DIR.glob("*.ast")):
        total += 1
        problems = check_pair(binary, java, path)
        if problems:
            failures.append((path.name, problems))
        elif args.verbose:
            print(f"  ok {path.name}")

    for name, problems in failures:
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"    {problem}")

    print()
    print(f"java oracle: {total - len(failures)}/{total} passed")

    random_failed = False
    if not args.no_random:
        seed = args.seed if args.seed is not None else random_suite.default_seed()
        ok = java_oracle.run_random(binary, java, seed, verbose=args.verbose)
        if ok:
            print(f"java oracle random: passed (seed={seed})")
        random_failed = not ok

    if failures or random_failed:
        return 1
    print("All Java oracle tests passed")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
