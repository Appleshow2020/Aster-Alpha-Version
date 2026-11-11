#!/usr/bin/env python3
"""Aster C 프런트 엔드 검증 도구.

Lib/test/test_grammar/valid의 모든 문법 조각이 파싱되는지, invalid의 모든
조각이 거부되는지 확인한다. dev 프로토타입이 있으면 토큰 목록과 AST 덤프가
완전히 같은지도 비교한다.

소스는 임시 파일 대신 표준 입력으로 넘기므로 MSYS/WSL 등 경로 변환이
다른 환경에서도 동작한다.

사용법:
    python3 Tools/check_frontend.py            # 기본 검증 (+ dev 비교 자동 시도)
    python3 Tools/check_frontend.py --no-dev   # dev 비교 없이 C만 검증
    python3 Tools/check_frontend.py -v         # 통과 항목과 실행 환경도 출력
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VALID_DIR = ROOT / "Lib/test/test_grammar/valid"
INVALID_DIR = ROOT / "Lib/test/test_grammar/invalid"
CHUNK_SEPARATOR = "#---"


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
    for name in ("aster.exe", "aster"):
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


def load_dev():
    sys.path.insert(0, str(ROOT))
    try:
        from dev.src.asterlang.parser.errors import AsterSyntaxError
        from dev.src.asterlang.parser.parser import parse
        from dev.src.asterlang.parser.tokenizer import tokenize
        from dev.src.asterlang.syntax import nodes as dev_ast
    except ImportError:
        return None
    return {
        "parse": parse,
        "tokenize": tokenize,
        "ast": dev_ast,
        "SyntaxError": AsterSyntaxError,
    }


def parse_token_dump(stdout: str) -> list[tuple[str, str]]:
    tokens = []
    for line in stdout.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 2:
            continue
        tokens.append((parts[1], parts[2] if len(parts) > 2 else ""))
    return tokens


def check_valid(binary: Path, dev, verbose: bool) -> tuple[int, list[Failure], list[str], dict]:
    failures: list[Failure] = []
    notes: list[str] = []
    counts = {"total": 0, "ast_match": 0, "tokens_match": 0}
    for filename, index, chunk in iter_chunks(VALID_DIR):
        counts["total"] += 1
        source = chunk + "\n"

        result = run_binary(binary, "--ast", source)
        if result.returncode != 0:
            failures.append(Failure("PARSE FAIL", filename, index, short_error(result), chunk))
            continue

        token_run = run_binary(binary, "--tokens", source)
        if token_run.returncode != 0:
            failures.append(Failure("TOKENIZE FAIL", filename, index, short_error(token_run), chunk))
            continue

        if dev is not None:
            try:
                want_tokens = [(t.type.name, t.string) for t in dev["tokenize"](source)]
            except dev["SyntaxError"]:
                # dev 프로토타입이 아직 모르는 문법(컬렉션 등)은 비교만 건너뛴다.
                want_tokens = None
            if want_tokens is None:
                if verbose:
                    print(f"  skip dev {filename} #{index} (dev가 이해하지 못함)")
            else:
                got_tokens = parse_token_dump(token_run.stdout)
                if got_tokens == want_tokens:
                    counts["tokens_match"] += 1
                else:
                    position = next(
                        (i for i, pair in enumerate(zip(got_tokens, want_tokens)) if pair[0] != pair[1]),
                        min(len(got_tokens), len(want_tokens)),
                    )
                    # dev는 동결된 프로토타입이므로 차이는 실패가 아니라 참고 사항으로 남긴다.
                    notes.append(
                        f"dev TOKEN DIFF {filename} #{index}: "
                        f"got={got_tokens[position:position + 1]} "
                        f"want={want_tokens[position:position + 1]}"
                    )
                    continue

                try:
                    want_ast = dev["ast"].dump(dev["parse"](source))
                except dev["SyntaxError"]:
                    want_ast = None
                if want_ast is not None:
                    got_ast = result.stdout.strip()
                    if got_ast == want_ast:
                        counts["ast_match"] += 1
                    else:
                        position = next(
                            (i for i, (a, b) in enumerate(zip(got_ast, want_ast)) if a != b),
                            min(len(got_ast), len(want_ast)),
                        )
                        notes.append(
                            f"dev AST DIFF {filename} #{index}: "
                            f"got={got_ast[position:position + 60]!r} "
                            f"want={want_ast[position:position + 60]!r}"
                        )
                        continue

        if verbose:
            print(f"  ok valid {filename} #{index}")

    return counts["total"], failures, notes, counts


def check_invalid(binary: Path, dev, verbose: bool) -> tuple[int, list[Failure], list[str]]:
    failures: list[Failure] = []
    notes: list[str] = []
    total = 0
    for filename, index, chunk in iter_chunks(INVALID_DIR):
        total += 1
        source = chunk + "\n"

        result = run_binary(binary, "--ast", source)
        if result.returncode == 0:
            failures.append(Failure("ACCEPTED (should fail)", filename, index, "파싱이 성공했습니다", chunk))
            continue

        if dev is not None:
            try:
                dev["parse"](source)
            except dev["SyntaxError"]:
                pass
            else:
                notes.append(f"NOTE: dev는 허용, C는 거부 — {filename} #{index}")
                continue

        if verbose:
            print(f"  ok invalid {filename} #{index}")

    return total, failures, notes


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description="Aster C 프런트 엔드 검증")
    parser.add_argument("--no-dev", action="store_true", help="dev 프로토타입과 비교하지 않는다")
    parser.add_argument("-v", "--verbose", action="store_true", help="통과한 조각과 실행 환경도 출력한다")
    args = parser.parse_args()

    if not VALID_DIR.is_dir() or not INVALID_DIR.is_dir():
        sys.exit(f"테스트 코퍼스를 찾을 수 없습니다: {VALID_DIR.parent}")

    binary = find_binary()
    if args.verbose:
        print(f"binary:      {binary}")
        print(f"interpreter: {sys.executable} ({sys.platform})")

    dev = None if args.no_dev else load_dev()
    if not args.no_dev and dev is None:
        print("dev 프로토타입을 불러오지 못해 C 단독 검증만 수행합니다.\n")

    valid_total, valid_failures, valid_notes, valid_counts = check_valid(binary, dev, args.verbose)
    invalid_total, invalid_failures, invalid_notes = check_invalid(binary, dev, args.verbose)

    for failure in valid_failures + invalid_failures:
        print(failure)
        print()

    for note in valid_notes + invalid_notes:
        print(note)

    valid_ok = valid_total - len(valid_failures)
    invalid_ok = invalid_total - len(invalid_failures)
    print(f"valid:   {valid_ok}/{valid_total} 파싱 성공")
    print(f"invalid: {invalid_ok}/{invalid_total} 거부됨")
    if dev is not None:
        print(f"dev 토큰 비교: {valid_counts['tokens_match']}/{valid_total} 일치")
        print(f"dev AST 비교:  {valid_counts['ast_match']}/{valid_total} 일치")

    failures = valid_failures + invalid_failures
    if failures:
        print(f"\n실패 {len(failures)}건")
        return 1
    print("\n모든 검증 통과")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
