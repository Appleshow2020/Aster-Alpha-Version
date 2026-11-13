#!/usr/bin/env python3
"""Aster 무작위 검증 도구.

검사 항목 구성은 고정되어 있고, 검사 대상 Aster 프로그램의 내용만 실행할 때마다
새 시드로 다시 만든다. 각 프로그램에 대해 파싱(--ast), 포매터 고정점(--format),
타입 검사(--check), 실행 결과를 Python 의미 모델과 비교하고, 포맷된 프로그램을
다시 실행해 결과가 같은지도 확인한다.

실패는 build/test-failures/에 저장되고, seed를 고정하면 같은 케이스를
그대로 다시 만들 수 있다.

사용법:
    python3 Tools/check_random.py
    python3 Tools/check_random.py --count 40 --categories control,patterns
    python3 Tools/check_random.py --seed 1234 --keep
    python3 Tools/check_random.py --property-only   # 파싱/포매팅만 검사
"""

from __future__ import annotations

import argparse
import os
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
    sys.exit("aster 바이너리가 없습니다. 먼저 `make`를 실행하세요.")


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description="Aster 무작위 검증")
    parser.add_argument("--seed", type=int, default=None, help="시드 (기본: 매번 새로 뽑음)")
    parser.add_argument("--count", type=int, default=20, help="생성할 케이스 수 (기본 20)")
    parser.add_argument(
        "--categories",
        default=None,
        help="쉼표로 구분한 카테고리: " + ", ".join(random_suite.CATEGORY_ORDER),
    )
    parser.add_argument("--property-only", action="store_true",
                        help="파싱·포매팅 고정점만 검사한다")
    parser.add_argument("--no-negative", action="store_true",
                        help="컴파일러가 거부해야 하는 부정 케이스 검사를 생략한다")
    parser.add_argument("--keep", action="store_true", help="생성한 케이스를 남긴다")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    if args.count < 1:
        sys.exit("--count는 1 이상이어야 합니다")

    categories = None
    if args.categories:
        categories = [item.strip() for item in args.categories.split(",") if item.strip()]
        unknown = [item for item in categories if item not in random_suite.CATEGORIES]
        if unknown:
            sys.exit(f"알 수 없는 카테고리: {', '.join(unknown)}")

    binary = find_binary()
    seed = args.seed if args.seed is not None else random_suite.default_seed()
    if args.verbose:
        print(f"binary: {binary}")
        print(f"seed:   {seed}")

    failed = random_suite.run_random(
        binary,
        count=args.count,
        seed=seed,
        categories=categories,
        property_only=args.property_only,
        verbose=args.verbose,
        keep=args.keep,
        label="무작위 검증",
    )

    negative_failed = 0
    if not args.no_negative:
        negative_failed = random_suite.run_negative(
            binary, seed=seed, verbose=args.verbose, keep=args.keep
        )

    return 1 if failed or negative_failed else 0


if __name__ == "__main__":
    sys.exit(main())
