#!/usr/bin/env python3
"""Aster C 프런트 엔드 검증 도구.

Lib/test/test_grammar/valid의 모든 문법 조각이 파싱되는지, invalid의 모든
조각이 거부되는지 확인한다. 실행할 때마다 무작위로 생성한 프로그램으로
파싱·포매팅 고정점도 함께 검사한다.

소스는 임시 파일 대신 표준 입력으로 넘기므로 MSYS/WSL 등 경로 변환이
다른 환경에서도 동작한다.

사용법:
    python3 Tools/check_frontend.py
    python3 Tools/check_frontend.py --seed 1234   # 무작위 검증 재현
    python3 Tools/check_frontend.py --no-random   # 무작위 검증 생략
    python3 Tools/check_frontend.py -v
"""

from __future__ import annotations

import argparse
import subprocess
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALID_DIR = ROOT / "Lib/test/test_grammar/valid"
INVALID_DIR = ROOT / "Lib/test/test_grammar/invalid"
CHUNK_SEPARATOR = "#---"

sys.path.insert(0, str(Path(__file__).resolve().parent))
import random_suite  # noqa: E402


class Failure:
    def __init__(self, kind: str, filename: str, index: int, detail: str, chunk: str):
        self.kind = kind
        self.filename = filename
        self.index = index
        self.detail = detail
        self.chunk = chunk

    def __str__(self) -> str:
        first_line = self.chunk.strip().splitlines()[0] if self.chunk.strip() else ""
        return (
            f"{self.kind}: {self.filename} #{self.index}\n"
            f"    {self.detail}\n"
            f"    | {first_line}"
        )


def find_binary() -> Path:
    names = ("aster.exe", "aster") if os.name == "nt" else ("aster", "aster.exe")
    for name in names:
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    sys.exit("aster 바이너리가 없습니다. 먼저 `make`를 실행하세요.")


def split_chunks(text: str) -> list[str]:
    return [chunk.strip() for chunk in text.split(CHUNK_SEPARATOR) if chunk.strip()]


def iter_chunks(directory: Path):
    for path in sorted(directory.glob("*.ast")):
        for index, chunk in enumerate(split_chunks(path.read_text(encoding="utf-8"))):
            yield path.name, index, chunk


def run_binary(binary: Path, flag: str | None, source: str) -> subprocess.CompletedProcess:
    argv = [str(binary)]
    if flag:
        argv.append(flag)
    argv.append("-")
    return subprocess.run(
        argv,
        input=source,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )


def short_error(result: subprocess.CompletedProcess) -> str:
    lines = result.stderr.strip().splitlines()
    if not lines:
        return "(출력 없음)"
    return " / ".join(line.strip() for line in lines[:3])


def check_valid(binary: Path, verbose: bool) -> tuple[int, list[Failure]]:
    failures: list[Failure] = []
    total = 0
    for filename, index, chunk in iter_chunks(VALID_DIR):
        total += 1
        source = chunk + "\n"

        result = run_binary(binary, "--ast", source)
        if result.returncode != 0:
            failures.append(Failure("PARSE FAIL", filename, index, short_error(result), chunk))
            continue

        token_run = run_binary(binary, "--tokens", source)
        if token_run.returncode != 0:
            failures.append(Failure("TOKENIZE FAIL", filename, index, short_error(token_run), chunk))
            continue

        if verbose:
            print(f"  ok valid {filename} #{index}")

    return total, failures


def check_invalid(binary: Path, verbose: bool) -> tuple[int, list[Failure]]:
    failures: list[Failure] = []
    total = 0
    for filename, index, chunk in iter_chunks(INVALID_DIR):
        total += 1
        source = chunk + "\n"

        result = run_binary(binary, "--ast", source)
        if result.returncode == 0:
            failures.append(Failure("ACCEPTED (should fail)", filename, index, "파싱이 성공했습니다", chunk))
            continue

        if verbose:
            print(f"  ok invalid {filename} #{index}")

    return total, failures


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description="Aster C 프런트 엔드 검증")
    parser.add_argument("--seed", type=int, default=None, help="무작위 검증 시드 (기본: 매번 새로 뽑음)")
    parser.add_argument("--no-random", action="store_true", help="무작위 검증을 생략한다")
    parser.add_argument("-v", "--verbose", action="store_true", help="통과한 조각과 실행 환경도 출력한다")
    args = parser.parse_args()

    if not VALID_DIR.is_dir() or not INVALID_DIR.is_dir():
        sys.exit(f"테스트 코퍼스를 찾을 수 없습니다: {VALID_DIR.parent}")

    binary = find_binary()
    if args.verbose:
        print(f"binary:      {binary}")
        print(f"interpreter: {sys.executable} ({sys.platform})")

    valid_total, valid_failures = check_valid(binary, args.verbose)
    invalid_total, invalid_failures = check_invalid(binary, args.verbose)

    random_failed = 0
    negative_failed = 0
    seed = args.seed if args.seed is not None else random_suite.default_seed()
    if not args.no_random:
        # 카테고리마다 한 번씩 생성해 모든 문법 조각이 파서/포매터를 지나가게 한다.
        random_failed = random_suite.run_random(
            binary,
            count=len(random_suite.CATEGORIES),
            property_only=True,
            seed=seed,
            verbose=args.verbose,
            label="프런트엔드 무작위",
            repro="python3 Tools/check_frontend.py",
        )
        negative_failed = random_suite.run_negative(
            binary,
            seed=seed,
            verbose=args.verbose,
            label="프런트엔드 부정",
            repro="python3 Tools/check_frontend.py",
        )

    for failure in valid_failures + invalid_failures:
        print(failure)
        print()

    valid_ok = valid_total - len(valid_failures)
    invalid_ok = invalid_total - len(invalid_failures)
    print(f"valid:   {valid_ok}/{valid_total} 파싱 성공")
    print(f"invalid: {invalid_ok}/{invalid_total} 거부됨")

    failures = valid_failures + invalid_failures
    if failures or random_failed or negative_failed:
        if failures:
            print(f"\n실패 {len(failures)}건")
        return 1
    print("\n모든 검증 통과")
    return 0


if __name__ == "__main__":
    sys.exit(main())
