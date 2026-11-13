#!/usr/bin/env python3
"""Generate paired Aster/Java programs and compare their output.

The same random operations are emitted twice: once using the aster.* java.base
port and once using the real JDK API. Running both programs and comparing stdout
turns the JDK into an oracle for arithmetic, bit operations, temporal types and
formatting without adding a runtime dependency to the interpreter.

This module is imported by check_java.py; it is not meant to be run directly.
A fixed seed reproduces the same operation sequence.
"""

from __future__ import annotations

import random
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_TMP = ROOT / "build" / "tmp"
FAILURE_DIR = ROOT / "build" / "test-failures"
JAVA_OPTS = ["-Duser.timezone=UTC", "-Duser.language=en", "-Duser.country=US"]

MODES = ["UP", "DOWN", "CEILING", "FLOOR", "HALF_UP", "HALF_DOWN", "HALF_EVEN"]


def _rand_bigint(rng: random.Random, max_digits: int) -> str:
    digits = rng.randint(1, max_digits)
    if digits == 1:
        value = str(rng.randint(1, 9))
    else:
        value = str(rng.randint(10 ** (digits - 1), 10 ** digits - 1))
    if rng.random() < 0.5:
        return "-" + value
    return value


def _rand_decimal(rng: random.Random) -> str:
    integer = rng.randint(1, 9999)
    scale = rng.randint(0, 3)
    if scale == 0:
        value = str(integer)
    else:
        value = f"{integer}.{rng.randint(0, 10 ** scale - 1):0{scale}d}"
    if rng.random() < 0.5:
        return "-" + value
    return value


def _bigint_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    for index in range(count):
        a = _rand_bigint(rng, 40)
        b = _rand_bigint(rng, 25)
        kind = rng.choice(["add", "subtract", "multiply", "divide", "remainder", "mod", "gcd"])
        operand = b
        if kind in ("mod", "gcd"):
            operand = b.lstrip("-")
        ast = f'println(new BigInteger("{a}").{kind}(new BigInteger("{operand}")).toString());'
        java = f'System.out.println(new BigInteger("{a}").{kind}(new BigInteger("{operand}")).toString());'
        ops.append((f"bigint {index + 1} {kind}", ast, java))
    return ops


def _bigdecimal_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    for index in range(count):
        a = _rand_decimal(rng)
        b = _rand_decimal(rng)
        kind = rng.choice(["add", "subtract", "multiply", "divide", "setScale", "compareTo", "pow"])
        if kind in ("add", "subtract", "multiply"):
            call = f'.{kind}(new BigDecimal("{b}"))'
            ast = f'println(new BigDecimal("{a}"){call}.toString());'
            java = f'System.out.println(new BigDecimal("{a}"){call}.toString());'
        elif kind == "compareTo":
            call = f'.compareTo(new BigDecimal("{b}"))'
            ast = f'println(new BigDecimal("{a}"){call});'
            java = f'System.out.println(new BigDecimal("{a}"){call});'
        elif kind == "divide":
            scale = rng.randint(0, 6)
            mode = rng.choice(MODES)
            call = f'.divide(new BigDecimal("{b}"), {scale}, RoundingMode.{mode})'
            ast = f'println(new BigDecimal("{a}"){call}.toString());'
            java = f'System.out.println(new BigDecimal("{a}"){call}.toString());'
        elif kind == "setScale":
            scale = rng.randint(0, 6)
            mode = rng.choice(MODES)
            call = f'.setScale({scale}, RoundingMode.{mode})'
            ast = f'println(new BigDecimal("{a}"){call}.toString());'
            java = f'System.out.println(new BigDecimal("{a}"){call}.toString());'
        else:
            exponent = rng.randint(0, 5)
            call = f".pow({exponent})"
            ast = f'println(new BigDecimal("{a}"){call}.toString());'
            java = f'System.out.println(new BigDecimal("{a}"){call}.toString());'
        ops.append((f"bigdecimal {index + 1} {kind}", ast, java))
    return ops


def _integer_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    for index in range(count):
        value = rng.randint(-(2 ** 31), 2 ** 31 - 1)
        other = rng.randint(-(2 ** 31), 2 ** 31 - 1)
        kind = rng.choice([
            "bitCount", "highestOneBit", "lowestOneBit", "toHexString",
            "toOctalString", "toBinaryString", "toStringRadix", "compareUnsigned",
        ])
        if kind == "toStringRadix":
            radix = rng.randint(2, 36)
            call = f'Integer.toString({value}, {radix})'
        elif kind == "compareUnsigned":
            call = f'Integer.compareUnsigned({value}, {other})'
        else:
            call = f'Integer.{kind}({value})'
        ast = f"println({call});"
        java = f"System.out.println({call});"
        ops.append((f"integer {index + 1} {kind}", ast, java))
    return ops


def _long_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    for index in range(count):
        value = rng.randint(-(2 ** 63) + 1, 2 ** 63 - 1)
        other = rng.randint(-(2 ** 63) + 1, 2 ** 63 - 1)
        kind = rng.choice([
            "bitCount", "toHexString", "toOctalString", "toBinaryString",
            "toStringRadix", "compareUnsigned", "signum",
        ])
        if kind == "toStringRadix":
            radix = rng.randint(2, 36)
            ast = f'println(Long.toString({value}, {radix}));'
            java = f'System.out.println(Long.toString({value}L, {radix}));'
        elif kind == "compareUnsigned":
            ast = f'println(Long.compareUnsigned({value}, {other}));'
            java = f'System.out.println(Long.compareUnsigned({value}L, {other}L));'
        else:
            ast = f'println(Long.{kind}({value}));'
            java = f'System.out.println(Long.{kind}({value}L));'
        ops.append((f"long {index + 1} {kind}", ast, java))
    return ops


def _date_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    for index in range(count):
        year = rng.randint(1900, 2100)
        month = rng.randint(1, 12)
        day = rng.randint(1, 28)
        kind = rng.choice(["plusDays", "minusDays", "plusMonths", "toEpochDay", "getDayOfYear", "getDayOfWeek"])
        base = f"LocalDate.of({year}, {month}, {day})"
        if kind in ("plusDays", "minusDays", "plusMonths"):
            amount = rng.randint(0, 500)
            ast = f"println({base}.{kind}({amount}).toString());"
            java = f"System.out.println({base}.{kind}({amount}).toString());"
        elif kind == "getDayOfWeek":
            ast = f"println({base}.getDayOfWeek().name);"
            java = f"System.out.println({base}.getDayOfWeek().name());"
        else:
            ast = f"println({base}.{kind}());"
            java = f"System.out.println({base}.{kind}());"
        ops.append((f"date {index + 1} {kind}", ast, java))
    return ops


def _time_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    nanos = [0, 1000000, 123000000, 999999999]
    for index in range(count):
        hour = rng.randint(0, 23)
        minute = rng.randint(0, 59)
        second = rng.randint(0, 59)
        nano = rng.choice(nanos)
        kind = rng.choice(["plusSeconds", "plusNanos", "toSecondOfDay", "toNanoOfDay"])
        base = f"LocalTime.of({hour}, {minute}, {second}, {nano})"
        if kind == "plusSeconds":
            offset = rng.randint(-100000, 100000)
            ast = f"println({base}.plusSeconds({offset}).toString());"
            java = f"System.out.println({base}.plusSeconds({offset}).toString());"
        elif kind == "plusNanos":
            offset = rng.randint(-1000000000, 1000000000)
            ast = f"println({base}.plusNanos({offset}).toString());"
            java = f"System.out.println({base}.plusNanos({offset}).toString());"
        else:
            ast = f"println({base}.{kind}());"
            java = f"System.out.println({base}.{kind}());"
        ops.append((f"time {index + 1} {kind}", ast, java))
    return ops


def _instant_ops(rng: random.Random, count: int) -> list[tuple[str, str, str]]:
    ops = []
    nanos = [0, 1000000, 123456789, 999999999]
    for index in range(count):
        epoch = rng.randint(0, 2000000000)
        nano = rng.choice(nanos)
        kind = rng.choice(["plusSeconds", "minusMillis", "toEpochMilli", "toString"])
        base = f"Instant.ofEpochSecond({epoch}, {nano})"
        if kind == "plusSeconds":
            offset = rng.randint(-100000, 100000)
            ast = f"println({base}.plusSeconds({offset}).toString());"
            java = f"System.out.println({base}.plusSeconds({offset}).toString());"
        elif kind == "minusMillis":
            offset = rng.randint(0, 100000000)
            ast = f"println({base}.minusMillis({offset}).toString());"
            java = f"System.out.println({base}.minusMillis({offset}).toString());"
        elif kind == "toEpochMilli":
            ast = f"println({base}.toEpochMilli());"
            java = f"System.out.println({base}.toEpochMilli());"
        else:
            ast = f"println({base}.toString());"
            java = f"System.out.println({base}.toString());"
        ops.append((f"instant {index + 1} {kind}", ast, java))
    return ops


ASTER_IMPORTS = [
    "import System.stdout.*;",
    "import aster.math.BigInteger;",
    "import aster.math.BigDecimal;",
    "import aster.math.RoundingMode;",
    "import aster.lang.Integer;",
    "import aster.lang.Long;",
    "import aster.time.LocalDate;",
    "import aster.time.LocalTime;",
    "import aster.time.Instant;",
]

JAVA_IMPORTS = [
    "import java.math.BigInteger;",
    "import java.math.BigDecimal;",
    "import java.math.RoundingMode;",
    "import java.time.Instant;",
    "import java.time.LocalDate;",
    "import java.time.LocalTime;",
]


def build_sources(seed: int, count: int) -> tuple[str, str]:
    rng = random.Random(seed)
    generators = [
        _bigint_ops, _bigdecimal_ops, _integer_ops,
        _long_ops, _date_ops, _time_ops, _instant_ops,
    ]
    aster: list[str] = []
    java: list[str] = []
    for generator in generators:
        for label, ast_line, java_line in generator(rng, count):
            aster.append(f'println("{label}");')
            aster.append(ast_line)
            java.append(f'System.out.println("{label}");')
            java.append(java_line)
    aster_source = "\n".join(ASTER_IMPORTS + ["", *aster, ""])
    java_source = "\n".join(
        JAVA_IMPORTS + ["", "public class OracleRandom {", "    public static void main(String[] args) {",
                        *(f"        {line}" for line in java), "    }", "}", ""]
    )
    return aster_source, java_source


def _run(command: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=120)


def run_random(binary: Path, java: str, seed: int, count: int = 8, verbose: bool = False) -> bool:
    aster_source, java_source = build_sources(seed, count)
    work = BUILD_TMP / "java-oracle"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    aster_path = work / "random.ast"
    java_path = work / "OracleRandom.java"
    aster_path.write_text(aster_source, encoding="utf-8", newline="\n")
    java_path.write_text(java_source, encoding="utf-8", newline="\n")

    aster = _run([str(binary), str(aster_path)])
    reference = _run([java, *JAVA_OPTS, str(java_path)])
    problems = []
    if aster.returncode != 0:
        problems.append(f"Aster exit code {aster.returncode}")
    if reference.returncode != 0:
        problems.append(f"Java exit code {reference.returncode}")
    if not problems:
        if aster.stdout != reference.stdout:
            want = reference.stdout.splitlines()
            got = aster.stdout.splitlines()
            index = next((i for i, (w, g) in enumerate(zip(want, got)) if w != g), min(len(want), len(got)))
            want_line = want[index] if index < len(want) else "<missing>"
            got_line = got[index] if index < len(got) else "<missing>"
            problems.append(f"line {index + 1}: got={got_line!r} want={want_line!r}")

    if not problems:
        shutil.rmtree(work, ignore_errors=True)
        if verbose:
            print(f"  ok java oracle random (seed={seed})")
        return True

    FAILURE_DIR.mkdir(parents=True, exist_ok=True)
    saved = FAILURE_DIR / f"java-oracle-{seed}"
    if saved.exists():
        shutil.rmtree(saved)
    shutil.copytree(work, saved)
    print(f"FAIL: java oracle random (seed={seed})")
    for problem in problems:
        print(f"    {problem}")
    if aster.stderr.strip():
        print(f"    aster stderr: {aster.stderr.strip().splitlines()[0]}")
    if reference.stderr.strip():
        print(f"    java stderr: {reference.stderr.strip().splitlines()[0]}")
    print(f"    saved: {saved}")
    print(f"    reproduce: python3 Tools/check_java.py --seed {seed}")
    return False
