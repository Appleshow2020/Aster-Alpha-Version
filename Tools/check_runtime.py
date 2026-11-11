#!/usr/bin/env python3
"""Aster 실행기 검증 도구.

Lib/test/test_run의 각 .ast를 실행해 표준 출력을 같은 이름의 .out과 비교한다.
.err 파일이 있으면 0이 아닌 종료 코드로 끝나야 하고, .err의 비어 있지 않은
각 줄이 표준 에러에 포함되어야 한다.

사용법:
    python3 Tools/check_runtime.py
    python3 Tools/check_runtime.py -v
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN_DIR = ROOT / "Lib/test/test_run"


def find_binary() -> Path:
    names = ("aster.exe", "aster") if os.name == "nt" else ("aster", "aster.exe")
    for name in names:
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    sys.exit("aster 바이너리가 없습니다. 먼저 `make`를 실행하세요.")


def run_program(binary: Path, path: Path, stdin_text: str | None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(binary), str(path)],
        input=stdin_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def first_difference(expected: str, got: str) -> str:
    want_lines = expected.splitlines()
    got_lines = got.splitlines()
    for index, (want, actual) in enumerate(zip(want_lines, got_lines)):
        if want != actual:
            return f"첫 차이 {index + 1}행: got={actual!r} want={want!r}"
    if len(want_lines) != len(got_lines):
        return f"줄 수가 다름: got={len(got_lines)} want={len(want_lines)}"
    return "차이 없음"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    binary = find_binary()
    if args.verbose:
        print(f"binary: {binary}")

    if not RUN_DIR.is_dir():
        sys.exit(f"{RUN_DIR} 디렉터리가 없습니다")

    total = 0
    failures = []
    for path in sorted(RUN_DIR.glob("*.ast")):
        total += 1
        out_path = path.with_suffix(".out")
        err_path = path.with_suffix(".err")
        in_path = path.with_suffix(".in")
        expected_out = out_path.read_text(encoding="utf-8") if out_path.is_file() else ""
        expected_err = err_path.read_text(encoding="utf-8") if err_path.is_file() else None
        stdin_text = in_path.read_text(encoding="utf-8") if in_path.is_file() else None
        result = run_program(binary, path, stdin_text)
        problems = []

        if expected_err is not None:
            if result.returncode == 0:
                problems.append("실패해야 하는데 종료 코드가 0입니다")
            for line in expected_err.splitlines():
                if line.strip() and line not in result.stderr:
                    problems.append(f"stderr에 없음: {line!r}")
        else:
            if result.returncode != 0:
                problems.append(f"종료 코드 {result.returncode}")
                detail = " / ".join(result.stderr.strip().splitlines()[:3])
                if detail:
                    problems.append(detail)

        if expected_out and result.stdout != expected_out:
            problems.append(first_difference(expected_out, result.stdout))

        if problems:
            failures.append((path.name, problems))
        elif args.verbose:
            print(f"  ok {path.name}")

    for name, problems in failures:
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"    {problem}")

    print()
    print(f"실행: {total - len(failures)}/{total} 통과")
    if failures:
        return 1
    print("모든 실행 테스트 통과")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
