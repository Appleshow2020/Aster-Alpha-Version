#!/usr/bin/env python3
"""Aster REPL 검증 도구.

Lib/test/test_repl의 각 .in 파일을 REPL 표준 입력으로 넣고 표준 출력을
같은 이름의 .out과 비교한다. .err 파일이 있으면 비어 있지 않은 각 줄이
표준 에러에 포함되어야 한다. REPL은 항상 종료 코드 0으로 끝나야 한다.

사용법:
    python3 Tools/check_repl.py
    python3 Tools/check_repl.py -v
    python3 Tools/check_repl.py --seed 1234   # 무작위 검증 재현
    python3 Tools/check_repl.py --no-random   # 무작위 검증 생략
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPL_DIR = ROOT / "Lib/test/test_repl"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import random_suite  # noqa: E402


def find_binary() -> Path:
    names = ("aster.exe", "aster") if os.name == "nt" else ("aster", "aster.exe")
    for name in names:
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    sys.exit("Aster binary not found. Run `make` first.")


def first_difference(expected: str, got: str) -> str:
    want_lines = expected.splitlines()
    got_lines = got.splitlines()
    for index, (want, actual) in enumerate(zip(want_lines, got_lines)):
        if want != actual:
            return f"first difference at line {index + 1}: got={actual!r} want={want!r}"
    if len(want_lines) != len(got_lines):
        return f"line count differs: got={len(got_lines)} want={len(want_lines)}"
    return "no difference"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--seed", type=int, default=None, help="Random validation seed (default: newly generated each run)")
    parser.add_argument("--no-random", action="store_true", help="Skip random validation")
    args = parser.parse_args()

    binary = find_binary()
    if not REPL_DIR.is_dir():
        sys.exit(f"directory not found: {REPL_DIR}")

    total = 0
    failures = []
    for path in sorted(REPL_DIR.glob("*.in")):
        total += 1
        out_path = path.with_suffix(".out")
        err_path = path.with_suffix(".err")
        expected_out = out_path.read_text(encoding="utf-8") if out_path.is_file() else ""
        expected_err = err_path.read_text(encoding="utf-8") if err_path.is_file() else None
        result = subprocess.run(
            [str(binary)],
            input=path.read_text(encoding="utf-8"),
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        problems = []

        if result.returncode != 0:
            problems.append(f"exit code {result.returncode}")
        if result.stdout != expected_out:
            problems.append(first_difference(expected_out, result.stdout))
        if expected_err is not None:
            for line in expected_err.splitlines():
                if line.strip() and line not in result.stderr:
                    problems.append(f"missing from stderr: {line!r}")

        if problems:
            failures.append((path.name, problems))
        elif args.verbose:
            print(f"  ok {path.name}")

    for name, problems in failures:
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"    {problem}")

    print()
    print(f"REPL: {total - len(failures)}/{total} passed")

    random_failed = 0
    if not args.no_random:
        random_failed = random_suite.run_repl_random(
            binary,
            count=3,
            seed=args.seed,
            verbose=args.verbose,
            repro="python3 Tools/check_repl.py",
        )

    if failures or random_failed:
        return 1
    print("All REPL tests passed")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
