from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB_TEST = ROOT / "Lib/test"
CHUNK_SEPARATOR = "#---"

# 기대 파일이 반드시 있어야 하는 디렉터리와 확장자
REQUIRED_COMPANIONS = {
    "test_format": (".fmt",),
    "test_lint": (".lint",),
    "test_debug": (".in",),
    "test_java": (".java",),
}

COMPANION_SUFFIXES = (".out", ".err", ".fmt", ".lint", ".in", ".java")

# REPL 테스트는 .ast 없이 대화 기록(.in/.out)만 사용한다.
ORPHAN_EXEMPT_DIRS = {"test_repl"}


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


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def short_error(result: subprocess.CompletedProcess) -> str:
    lines = result.stderr.strip().splitlines()
    if not lines:
        return "(no output)"
    return " / ".join(line.strip() for line in lines[:3])


def split_chunks(text: str) -> list[str]:
    return [chunk.strip() for chunk in text.split(CHUNK_SEPARATOR) if chunk.strip()]


def check_chunks(binary: Path, directory: Path, expect_ok: bool, verbose: bool) -> tuple[int, list[tuple[str, list[str]]]]:
    failures: list[tuple[str, list[str]]] = []
    total = 0
    for path in sorted(directory.glob("*.ast")):
        for index, chunk in enumerate(split_chunks(path.read_text(encoding="utf-8"))):
            total += 1
            name = f"{rel(path)} #{index}"
            result = run(binary, ["--ast", "-"], chunk + "\n")
            if expect_ok and result.returncode != 0:
                failures.append((name, [f"parse failed: {short_error(result)}"]))
            elif not expect_ok and result.returncode == 0:
                failures.append((name, ["expected a parse error but parsing succeeded"]))
            elif verbose:
                kind = "valid" if expect_ok else "invalid"
                print(f"  ok {kind} {name}")
    return total, failures


def check_parse(binary: Path, paths: list[Path], verbose: bool) -> tuple[int, list[tuple[str, list[str]]]]:
    failures: list[tuple[str, list[str]]] = []
    for path in paths:
        result = run(binary, ["--ast", str(path)])
        if result.returncode != 0:
            failures.append((rel(path), [f"parse failed: {short_error(result)}"]))
        elif verbose:
            print(f"  ok parse {rel(path)}")
    return len(paths), failures


def check_typecheck(binary: Path, paths: list[Path], verbose: bool) -> tuple[int, list[tuple[str, list[str]]]]:
    failures: list[tuple[str, list[str]]] = []
    for path in paths:
        result = run(binary, ["--check", str(path)])
        if result.returncode != 0:
            failures.append((rel(path), [f"type check failed: {short_error(result)}"]))
        elif verbose:
            print(f"  ok check {rel(path)}")
    return len(paths), failures


def check_companions(ast_paths: list[Path], verbose: bool) -> tuple[int, list[tuple[str, list[str]]]]:
    failures: list[tuple[str, list[str]]] = []
    total = 0

    by_dir: dict[str, list[Path]] = {}
    for path in ast_paths:
        by_dir.setdefault(path.parent.name, []).append(path)

    run_dir = LIB_TEST / "test_run"
    for path in sorted(run_dir.glob("*.ast")):
        total += 1
        if not path.with_suffix(".out").is_file() and not path.with_suffix(".err").is_file():
            failures.append((rel(path), ["missing .out or .err companion"]))
        elif verbose:
            print(f"  ok companion {rel(path)}")

    for dirname, suffixes in REQUIRED_COMPANIONS.items():
        for path in by_dir.get(dirname, []):
            total += 1
            missing = [path.with_suffix(suffix) for suffix in suffixes if not path.with_suffix(suffix).is_file()]
            if missing:
                names = ", ".join(item.name for item in missing)
                failures.append((rel(path), [f"missing {names}"]))
            elif verbose:
                print(f"  ok companion {rel(path)}")

    for path in sorted(LIB_TEST.rglob("*")):
        if not path.is_file() or path.suffix not in COMPANION_SUFFIXES:
            continue
        relative = path.relative_to(LIB_TEST)
        if relative.parts and relative.parts[0] in ORPHAN_EXEMPT_DIRS:
            continue
        if not path.with_suffix(".ast").is_file():
            failures.append((rel(path), ["no matching .ast file"]))

    return total, failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Aster library test corpus validation")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print every passing check")
    args = parser.parse_args()

    if not LIB_TEST.is_dir():
        sys.exit(f"directory not found: {LIB_TEST}")

    binary = find_binary()
    if args.verbose:
        print(f"binary: {binary}")

    ast_paths = sorted(LIB_TEST.rglob("*.ast"))
    grammar_dir = LIB_TEST / "test_grammar"
    grammar_paths = [path for path in ast_paths if grammar_dir in path.parents]
    plain_paths = [path for path in ast_paths if grammar_dir not in path.parents]

    valid_total, valid_failures = check_chunks(binary, grammar_dir / "valid", True, args.verbose)
    invalid_total, invalid_failures = check_chunks(binary, grammar_dir / "invalid", False, args.verbose)
    failures = valid_failures + invalid_failures

    parse_total, parse_failures = check_parse(binary, plain_paths, args.verbose)
    failures += parse_failures

    typecheck_paths = [
        path
        for path in sorted((LIB_TEST / "test_run").glob("*.ast"))
        if not path.with_suffix(".err").is_file()
    ]
    typecheck_paths += sorted((LIB_TEST / "test_java").glob("*.ast"))
    typecheck_paths += sorted((LIB_TEST / "test_debug").glob("*.ast"))
    check_total, check_failures = check_typecheck(binary, typecheck_paths, args.verbose)
    failures += check_failures

    companion_total, companion_failures = check_companions(ast_paths, args.verbose)
    failures += companion_failures

    for name, problems in failures:
        print(f"FAIL: {name}")
        for problem in problems:
            print(f"    {problem}")

    print()
    print(f"corpus:     {len(ast_paths)} .ast files")
    print(f"valid:      {valid_total - len(valid_failures)}/{valid_total} chunks parsed")
    print(f"invalid:    {invalid_total - len(invalid_failures)}/{invalid_total} chunks rejected")
    print(f"parse:      {parse_total - len(parse_failures)}/{parse_total} files parsed")
    print(f"typecheck:  {check_total - len(check_failures)}/{check_total} files passed")
    print(f"companions: {companion_total - len(companion_failures)}/{companion_total} checked")

    if failures:
        print(f"\n{len(failures)} failure(s)")
        return 1
    print("\nAll library corpus checks passed")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
