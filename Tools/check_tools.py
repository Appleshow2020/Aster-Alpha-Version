#!/usr/bin/env python3
"""Aster 도구(포매터/린터/디버거) 검증 도구.

test_format: X.ast를 --format한 결과가 X.fmt와 같아야 하고,
             X.fmt를 다시 --format해도 같아야 한다 (고정점).
test_lint:   X.ast를 --lint한 경고가 X.lint의 줄들을 모두 포함해야 하며,
             경고가 하나라도 있으면 종료 코드는 1, 없으면 0이다.
test_debug:  X.in을 표준 입력으로 --debug를 실행해 stdout이 X.out과 같고,
             X.err의 각 줄이 stderr에 있어야 한다.

사용법:
    python3 Tools/check_tools.py
    python3 Tools/check_tools.py -v
    python3 Tools/check_tools.py --seed 1234   # 무작위 검증 재현
    python3 Tools/check_tools.py --no-random   # 무작위 검증 생략
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(Path(__file__).resolve().parent))
import random_suite  # noqa: E402


def find_binary() -> Path:
    names = ("aster.exe", "aster") if os.name == "nt" else ("aster", "aster.exe")
    for name in names:
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    sys.exit("Aster binary not found. Run `make` first.")


def run(binary: Path, args: list[str], stdin_text: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(binary), *args],
        input=stdin_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def check_format(binary: Path, verbose: bool) -> list[tuple[str, list[str]]]:
    directory = ROOT / "Lib/test/test_format"
    failures = []
    total = 0
    for path in sorted(directory.glob("*.ast")):
        total += 1
        expected_path = path.with_suffix(".fmt")
        if not expected_path.is_file():
            failures.append((path.name, [f"missing {expected_path.name}"]))
            continue
        expected = read(expected_path)
        result = run(binary, ["--format", str(path)])
        problems = []
        if result.returncode != 0:
            problems.append(f"exit code {result.returncode}: {result.stderr.strip()[:200]}")
        if result.stdout != expected:
            problems.append("--format output differs from .fmt")
        again = run(binary, ["--format", "-"], expected)
        if again.returncode != 0:
            problems.append(f"fixed-point reformat failed: {again.stderr.strip()[:200]}")
        elif again.stdout != expected:
            problems.append("formatting is not a fixed point (second result differs)")
        if problems:
            failures.append((path.name, problems))
        elif verbose:
            print(f"  ok format/{path.name}")

    return failures, total


def check_lint(binary: Path, verbose: bool) -> list[tuple[str, list[str]]]:
    directory = ROOT / "Lib/test/test_lint"
    failures = []
    total = 0
    for path in sorted(directory.glob("*.ast")):
        total += 1
        expected_path = path.with_suffix(".lint")
        if not expected_path.is_file():
            failures.append((path.name, [f"missing {expected_path.name}"]))
            continue
        expected_lines = [line for line in read(expected_path).splitlines() if line.strip()]
        result = run(binary, ["--lint", str(path)])
        problems = []
        got_lines = [line for line in result.stderr.splitlines() if "warning:" in line]
        if len(got_lines) != len(expected_lines):
            problems.append(f"warning count: got={len(got_lines)} want={len(expected_lines)}")
        for line in expected_lines:
            if line not in result.stderr:
                problems.append(f"missing from stderr: {line!r}")
        want_code = 1 if expected_lines else 0
        if result.returncode != want_code:
            problems.append(f"exit code: got={result.returncode} want={want_code}")
        if problems:
            failures.append((path.name, problems))
        elif verbose:
            print(f"  ok lint/{path.name}")

    return failures, total


def check_debug(binary: Path, verbose: bool) -> list[tuple[str, list[str]]]:
    directory = ROOT / "Lib/test/test_debug"
    failures = []
    total = 0
    for path in sorted(directory.glob("*.ast")):
        total += 1
        in_path = path.with_suffix(".in")
        out_path = path.with_suffix(".out")
        err_path = path.with_suffix(".err")
        if not in_path.is_file():
            failures.append((path.name, [f"missing {in_path.name}"]))
            continue
        expected_out = read(out_path) if out_path.is_file() else ""
        expected_err = read(err_path) if err_path.is_file() else ""
        result = run(binary, ["--debug", str(path)], read(in_path))
        problems = []
        if result.returncode != 0:
            problems.append(f"exit code {result.returncode}")
        if expected_out and result.stdout != expected_out:
            problems.append(f"stdout mismatch: got={result.stdout!r} want={expected_out!r}")
        for line in expected_err.splitlines():
            if line.strip() and line not in result.stderr:
                problems.append(f"missing from stderr: {line!r}")
        if problems:
            failures.append((path.name, problems))
        elif verbose:
            print(f"  ok debug/{path.name}")

    return failures, total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--seed", type=int, default=None, help="Random validation seed (default: newly generated each run)")
    parser.add_argument("--no-random", action="store_true", help="Skip random validation")
    args = parser.parse_args()

    binary = find_binary()
    if args.verbose:
        print(f"binary: {binary}")

    failures: list[tuple[str, list[str]]] = []
    total = 0
    for check in (check_format, check_lint, check_debug):
        found, count = check(binary, args.verbose)
        failures.extend(found)
        total += count

    for name, problems in failures:
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"    {problem}")

    print()
    print(f"tool tests: {total - len(failures)}/{total} passed")

    random_failed = 0
    if not args.no_random:
        seed = args.seed if args.seed is not None else random_suite.default_seed()
        random_failed = random_suite.run_random(
            binary,
            count=len(random_suite.CATEGORIES),
            seed=seed,
            property_only=True,
            verbose=args.verbose,
            label="Tool random",
            repro="python3 Tools/check_tools.py",
        )

    if failures or random_failed:
        return 1
    print("All tool tests passed")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
