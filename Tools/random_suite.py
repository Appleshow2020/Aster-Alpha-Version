#!/usr/bin/env python3
"""Aster 무작위 검증 라이브러리.

검사 항목 구성은 고정하고, 검사 대상 프로그램의 내용(식, 값, 이름, 구조)만
시드로 다시 만든다. 각 케이스는 Python 쪽 의미 모델과 실행 결과를 비교하고,
포매터 고정점과 포맷 결과의 실행 결과까지 확인한다.

이 모듈은 직접 실행하지 않고 check_random.py와 다른 검사 스크립트가 가져다 쓴다.
시드를 고정하면 같은 내용을 그대로 재현할 수 있다.

    python3 Tools/check_random.py --seed 1234 --keep
"""

from __future__ import annotations

import os
import random
import shutil
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_TMP = ROOT / "build" / "tmp"
FAILURE_DIR = ROOT / "build" / "test-failures"

WORDS = [
    "alpha", "beta", "gamma", "delta", "epsilon", "aster", "orbit",
    "lumen", "nova", "pixel", "quartz", "rave", "solar", "tango", "한글",
]
KEYS = ["name", "role", "kind", "size", "color", "tag", "mode", "level"]


# ------------------------------------------------------------------ 값 표현


def _fmt_str(s: str) -> str:
    return repr(s)


def fmt(value, top: bool = False) -> str:
    """Aster의 println/표현식 출력과 같은 문자열을 만든다.

    top이면 문자열을 그대로, 아니면 컨테이너 안에서처럼 따옴표로 감싼다.
    생성하는 문자열에는 따옴표/역슬래시/개행을 넣지 않는다.
    """
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    if isinstance(value, str):
        return value if top else _fmt_str(value)
    if isinstance(value, list):
        return "[" + ", ".join(fmt(item) for item in value) + "]"
    if isinstance(value, tuple):
        body = ", ".join(fmt(item) for item in value)
        return "(" + body + ("," if len(value) == 1 else "") + ")"
    if isinstance(value, dict):
        return "{" + ", ".join(
            f"{fmt(key)}: {fmt(item)}" for key, item in value.items()
        ) + "}"
    raise TypeError(f"모델에 없는 값: {value!r}")


def trunc_div(a: int, b: int) -> int:
    # Aster의 정수 `/`는 0 방향 절단이다(Python의 //와 다름).
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


# ------------------------------------------------------------------ 케이스


@dataclass
class Case:
    name: str
    files: dict[str, str]
    expected: str


class Prog:
    """Aster 소스와 기대 출력을 나란히 쌓는다."""

    def __init__(self, imports: tuple[str, ...] = ("import System.stdout.*;",)):
        self.lines: list[str] = []
        self.want: list[str] = []
        for item in imports:
            self.lines.append(item)
        if imports:
            self.lines.append("")

    def add(self, line: str = "") -> None:
        self.lines.append(line)

    def block(self, *lines: str) -> None:
        self.lines.extend(lines)

    def println(self, expression: str, value) -> None:
        self.lines.append(f"println({expression});")
        self.want.append(fmt(value, top=True))

    def expect(self, value) -> None:
        self.want.append(fmt(value, top=True))

    def source(self) -> str:
        lines = list(self.lines)
        while lines and not lines[-1].strip():
            lines.pop()
        return "\n".join(lines) + "\n"

    def expected(self) -> str:
        return "".join(line + "\n" for line in self.want)


class Names:
    def __init__(self, rng: random.Random, prefix: str):
        self.rng = rng
        self.prefix = prefix
        self.used: set[str] = set()
        self.counter = 0

    def __call__(self, kind: str) -> str:
        self.counter += 1
        name = f"{kind}{self.prefix}{self.counter}"
        self.used.add(name)
        return name


# ------------------------------------------------------------------ 식 생성


INT_LEAVES = [0, 1, 2, 3, 5, 7, 9, 10, 12, 25, 42, 99, 100, -1, -2, -3, -7, -10]
FLOAT_LEAVES = [0.5, 1.5, 2.25, 3.0, 0.125, 4.0, 10.0, -0.5, -2.5, 100.0, 7.75]


def gen_int(rng: random.Random, depth: int, env=None) -> tuple[str, int]:
    env = env or []
    if depth <= 0 or rng.random() < 0.28:
        int_env = [e for e in env if isinstance(e[1], int) and not isinstance(e[1], bool)]
        if int_env and rng.random() < 0.5:
            return rng.choice(int_env)
        value = rng.choice(INT_LEAVES)
        # 음수 리터럴은 `-7 ** 2`가 -(7 ** 2)로 읽히므로 미리 묶는다.
        return (f"({value})" if value < 0 else str(value)), value

    op = rng.choice(["+", "-", "*", "*", "//", "%", "/", "**"])
    left, lv = gen_int(rng, depth - 1, env)
    right, rv = gen_int(rng, depth - 1, env)
    if op in ("//", "%", "/") and rv == 0:
        op = "+"
    if op == "/":
        value = trunc_div(lv, rv)
    elif op == "//":
        value = lv // rv
    elif op == "%":
        value = lv % rv
    elif op == "**":
        if abs(lv) > 12 or not 0 <= rv <= 3:
            op = "+"
            value = lv + rv
        else:
            value = lv ** rv
    else:
        value = lv + rv if op == "+" else lv - rv if op == "-" else lv * rv

    text = f"({left} {op} {right})"
    if abs(value) > 10 ** 6:
        return f"({text} - {text} + 7)", 7
    return text, value


def gen_float(rng: random.Random, depth: int, env=None) -> tuple[str, float]:
    env = env or []
    if depth <= 0 or rng.random() < 0.3:
        # 정수 변수를 그대로 쓰면 Aster에서 정수 연산이 되어 모델과 어긋난다.
        float_env = [e for e in env if isinstance(e[1], float)]
        if float_env and rng.random() < 0.5:
            return rng.choice(float_env)
        value = rng.choice(FLOAT_LEAVES)
        return repr(value), value

    op = rng.choice(["+", "-", "*", "/", "+", "-"])
    left, lv = gen_float(rng, depth - 1, env)
    if rng.random() < 0.25:
        right, rv = gen_int(rng, depth - 1)
        rv = float(rv)
    else:
        right, rv = gen_float(rng, depth - 1, env)
    if op == "/" and rv == 0.0:
        op = "+"
    if op == "+":
        value = lv + rv
    elif op == "-":
        value = lv - rv
    elif op == "*":
        value = lv * rv
    else:
        value = lv / rv
    text = f"({left} {op} {right})"
    if abs(value) > 1e9:
        return "2.5", 2.5
    return text, value


def gen_bool(rng: random.Random, depth: int, env=None) -> tuple[str, bool]:
    if depth <= 0 or rng.random() < 0.3:
        if rng.random() < 0.35:
            value = rng.random() < 0.5
            return ("true" if value else "false"), value
        if rng.random() < 0.5:
            left, lv = gen_int(rng, 1, env)
            right, rv = gen_int(rng, 1, env)
            op = rng.choice(["<", "<=", ">", ">=", "==", "!="])
            return f"({left} {op} {right})", {
                "<": lv < rv, "<=": lv <= rv, ">": lv > rv,
                ">=": lv >= rv, "==": lv == rv, "!=": lv != rv,
            }[op]
        left, lv = gen_float(rng, 1, env)
        right, rv = gen_float(rng, 1, env)
        op = rng.choice(["<", ">", "==", "!="])
        return f"({left} {op} {right})", {
            "<": lv < rv, ">": lv > rv, "==": lv == rv, "!=": lv != rv,
        }[op]

    kind = rng.random()
    if kind < 0.25:
        inner, value = gen_bool(rng, depth - 1, env)
        return f"(not {inner})", not value
    left, lv = gen_bool(rng, depth - 1, env)
    right, rv = gen_bool(rng, depth - 1, env)
    op = rng.choice(["and", "or", "and", "or"])
    value = (lv and rv) if op == "and" else (lv or rv)
    return f"({left} {op} {right})", value


def gen_str(rng: random.Random, depth: int, env=None) -> tuple[str, str]:
    env = env or []
    if depth <= 0 or rng.random() < 0.3:
        str_env = [e for e in env if isinstance(e[1], str)]
        if str_env and rng.random() < 0.5:
            return rng.choice(str_env)
        word = rng.choice(WORDS)
        return f'"{word}"', word

    kind = rng.randrange(7)
    if kind == 0:
        left, lv = gen_str(rng, depth - 1, env)
        right, rv = gen_str(rng, depth - 1, env)
        return f"({left} + {right})", lv + rv
    if kind == 1:
        left, lv = gen_str(rng, depth - 1, env)
        times = rng.randrange(0, 4)
        return f"({left} * {times})", lv * times
    if kind == 2:
        left, lv = gen_str(rng, depth - 1, env)
        if len(lv) >= 2:
            start = rng.randrange(0, len(lv) - 1)
            stop = rng.randrange(start + 1, len(lv) + 1)
            return f"({left})[{start}:{stop}]", lv[start:stop]
        return f"({left}).upper()", lv.upper()
    if kind == 3:
        left, lv = gen_str(rng, depth - 1, env)
        if rng.random() < 0.5:
            return f"({left}).upper()", lv.upper()
        return f"({left}).lower()", lv.lower()
    if kind == 4:
        inner, value = gen_int(rng, depth - 1)
        return f"str({inner})", str(value)
    if kind == 5:
        sep = rng.choice(["-", ",", "/", " "])
        left, lv = gen_str(rng, depth - 1, env)
        right, rv = gen_str(rng, depth - 1, env)
        return f'"{sep}".join([{left}, {right}])', sep.join([lv, rv])
    word = rng.choice(WORDS)
    left, lv = gen_str(rng, depth - 1, env)
    return f'({left}).replace("{word}", "{word.upper()}")', lv.replace(word, word.upper())


def gen_fstring(rng: random.Random, env=None) -> tuple[str, str]:
    env = env or []
    kind = rng.randrange(3)
    if kind == 0:
        left, lv = gen_int(rng, 1)
        right, rv = gen_float(rng, 1)
        return f'f"{{{left}}} {{{right}}}"', f"{fmt(lv, True)} {fmt(rv, True)}"
    if kind == 1:
        left, lv = gen_bool(rng, 1)
        right, rv = gen_int(rng, 1)
        return f'f"{{{left}}}:{{{right}}}"', f"{fmt(lv, True)}:{fmt(rv, True)}"
    # f-string 안에는 따옴표가 든 식을 넣을 수 없으므로 이름만 끼운다.
    for name, value in env:
        if isinstance(value, str):
            return f'f"[{{{name}}}]"', f"[{value}]"
    inner, iv = gen_int(rng, 1)
    return f'f"n={{{inner}}}"', f"n={fmt(iv, True)}"


# ------------------------------------------------------------------ 카테고리


def case_expr(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"e{index}")
    prog = Prog()
    a, b, c = rng.randrange(-9, 15), rng.randrange(-9, 15), rng.randrange(-5, 10)
    av = n("i")
    prog.add(f"int {av} = {a};")
    prog.println(av, a)
    env = [(av, a)]

    for _ in range(6):
        pick = rng.randrange(5)
        if pick == 0:
            text, value = gen_int(rng, rng.randrange(1, 4), env)
        elif pick == 1:
            text, value = gen_float(rng, rng.randrange(1, 3), env)
        elif pick == 2:
            text, value = gen_bool(rng, rng.randrange(1, 3), env)
        elif pick == 3:
            text, value = gen_str(rng, rng.randrange(1, 3))
        else:
            text, value = gen_fstring(rng, env)
        prog.println(text, value)

    cond, cv = gen_bool(rng, 2, env)
    xtext, xv = gen_int(rng, 2, env)
    ytext, yv = gen_int(rng, 2, env)
    prog.println(f"({cond} ? {xtext} : {ytext})", xv if cv else yv)
    prog.println(f"max({a}, {b})", max(a, b))
    prog.println(f"abs({c})", abs(c))
    prog.println(f"min({a}, {b}, {c})", min(a, b, c))
    return Case(f"expr_{index}", {"main.ast": prog.source()}, prog.expected())


def case_strings(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"s{index}")
    prog = Prog()
    word = rng.choice(WORDS)
    name = n("s")
    prog.add(f'str {name} = "{word}";')
    env = [(name, word)]
    prog.println(name, word)
    prog.println(f"len({name})", len(word))
    stop = rng.randrange(1, len(word) + 1)
    prog.println(f"{name}[0:{stop}]", word[:stop])
    idx = rng.randrange(len(word))
    prog.println(f"{name}[{idx}]", word[idx])
    prog.println(f"{name}.upper()", word.upper())
    prog.println(f"{name}.lower()", word.lower())
    other = rng.choice([w for w in WORDS if w != word])
    prog.println(f'{name}.replace("{word}", "{other}")', other)
    prog.println(f'{name}.startswith("{word[:1]}")', word.startswith(word[:1]))
    prog.println(f'{name}.endswith("{word[-1]}")', word.endswith(word[-1]))
    sep = rng.choice(["-", ":", "_"])
    prog.println(f'"{sep}".join([{name}, "{other}"])', sep.join([word, other]))
    prog.println(f'"{word},{other}".split(",")', [word, other])
    e1, v1 = gen_str(rng, 2, env)
    e2, v2 = gen_str(rng, 2, env)
    prog.println(f"({e1} + {e2})", v1 + v2)
    prog.println(f"({e1} == {e2})", v1 == v2)
    total = rng.randrange(0, 4)
    prog.println(f"({e1} * {total})", v1 * total)
    for _ in range(2):
        text, value = gen_fstring(rng, env)
        prog.println(text, value)
    return Case(f"strings_{index}", {"main.ast": prog.source()}, prog.expected())


def case_control(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"c{index}")
    prog = Prog()
    low = rng.randrange(-8, 4)
    high = low + rng.randrange(1, 8)
    value = rng.randrange(-12, 14)
    var = n("v")
    prog.add(f"int {var} = {value};")
    prog.add(f"if ({var} < {low}) {{")
    prog.add('    println("low");')
    prog.add(f"}} else if ({var} < {high}) {{")
    prog.add('    println("mid");')
    prog.add("} else {")
    prog.add('    println("high");')
    prog.add("}")
    label = "low" if value < low else "mid" if value < high else "high"
    prog.expect(label)

    start = rng.randrange(-5, 6)
    step = rng.choice([1, 2, 3, -1, -2])
    count = rng.randrange(1, 7)
    stop = start + step * count
    acc = n("sum")
    cnt = n("cnt")
    prog.add(f"var {acc} = 0;")
    prog.add(f"var {cnt} = 0;")
    prog.add(f"for (int idx; idx = {start}..{stop}..{step}) {{")
    prog.add(f"    {acc} += idx;")
    prog.add(f"    {cnt} += 1;")
    prog.add("}")
    seq = list(range(start, stop, step))
    prog.println(acc, sum(seq))
    prog.println(cnt, len(seq))

    items = [rng.randrange(-9, 10) for _ in range(rng.randrange(1, 6))]
    total = n("total")
    prog.add(f"var {total} = 0;")
    prog.add(f"for (int it in [{', '.join(map(str, items))}]) {{")
    prog.add(f"    {total} += it;")
    prog.add("}")
    prog.println(total, sum(items))

    wstart = rng.randrange(0, 5)
    wstep = rng.randrange(1, 4)
    wlimit = wstart + wstep * rng.randrange(1, 6)
    wvar = n("w")
    prog.add(f"int {wvar} = {wstart};")
    prog.add(f"while ({wvar} < {wlimit}) {{")
    prog.add(f"    {wvar} += {wstep};")
    prog.add("}")
    prog.println(wvar, wstart + ((wlimit - wstart + wstep - 1) // wstep) * wstep)

    subject = rng.randrange(-2, 8)
    cases = rng.sample(list(range(-2, 8)), k=3)
    prog.add(f"switch ({subject}) {{")
    matched = None
    for case_value in cases:
        guard = rng.random() < 0.3
        guard_limit = rng.randrange(-3, 6)
        if guard:
            prog.add(f"case {case_value} when {subject} > {guard_limit} {{")
            ok = subject == case_value and subject > guard_limit
        else:
            prog.add(f"case {case_value} {{")
            ok = subject == case_value
        prog.add(f'    println("case {case_value}");')
        prog.add("}")
        if ok and matched is None:
            matched = f"case {case_value}"
    prog.add("default {")
    prog.add('    println("default");')
    prog.add("}")
    prog.add("}")
    prog.expect(matched if matched is not None else "default")

    sword = rng.choice(WORDS[:8])
    scases = rng.sample(WORDS[:8], k=3)
    fn = n("label")
    prog.add(f"fn {fn}(str s) -> str {{")
    prog.add("    switch (s) {")
    for case_word in scases:
        prog.add(f'        case "{case_word}" {{ return "hit {case_word}"; }}')
    prog.add('        default { return "miss"; }')
    prog.add("    }")
    prog.add("}")
    prog.println(f'{fn}("{sword}")', f"hit {sword}" if sword in scases else "miss")
    return Case(f"control_{index}", {"main.ast": prog.source()}, prog.expected())


def case_collections(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"l{index}")
    prog = Prog()
    values = [rng.randrange(-20, 21) for _ in range(rng.randrange(2, 7))]
    name = n("xs")
    prog.add(f"var {name} = [{', '.join(map(str, values))}];")
    prog.println(name, values)
    prog.println(f"len({name})", len(values))
    prog.println(f"{name}[0]", values[0])
    prog.println(f"{name}[-1]", values[-1])
    prog.println(f"{name}[1:]", values[1:])
    prog.println(f"{name}[:2]", values[:2])
    added = rng.randrange(-9, 10)
    prog.add(f"{name}.append({added});")
    values.append(added)
    prog.println(name, values)
    prog.add(f"{name}.pop();")
    values.pop()
    prog.println(f"len({name})", len(values))
    new_first = rng.randrange(-9, 10)
    prog.add(f"{name}[0] = {new_first};")
    values[0] = new_first
    prog.println(name, values)
    prog.println(f"({name} == {name})", values == values)

    keys = rng.sample(KEYS, k=rng.randrange(2, 5))
    dvals = [rng.randrange(-30, 31) for _ in keys]
    dname = n("d")
    body = ", ".join(f'"{k}": {v}' for k, v in zip(keys, dvals))
    prog.add(f"var {dname} = {{{body}}};")
    model = dict(zip(keys, dvals))
    prog.println(dname, model)
    prog.println(f"{dname}[\"{keys[0]}\"]", model[keys[0]])
    prog.println(f'{dname}.get("{keys[0]}")', model[keys[0]])
    prog.println(f'{dname}.get("missing", -1)', -1)
    prog.println(f"len({dname})", len(model))
    new_key = rng.choice([k for k in KEYS if k not in model])
    new_value = rng.randrange(-30, 31)
    prog.add(f'{dname}["{new_key}"] = {new_value};')
    model[new_key] = new_value
    prog.println(dname, model)
    updated = rng.randrange(-30, 31)
    prog.add(f'{dname}["{keys[0]}"] = {updated};')
    model[keys[0]] = updated
    prog.println(dname, model)

    nested = [{"xs": [rng.randrange(-5, 6), rng.randrange(-5, 6)], "n": rng.randrange(0, 9)}]
    nested_text = ', '.join(
        f'{{"xs": [{item["xs"][0]}, {item["xs"][1]}], "n": {item["n"]}}}' for item in nested
    )
    prog.println(f"[{nested_text}]", nested)
    return Case(f"collections_{index}", {"main.ast": prog.source()}, prog.expected())


def case_functions(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"f{index}")
    prog = Prog()
    fact = n("fact")
    fib = n("fib")
    add3 = n("add3")
    show = n("show")
    showkw = n("showkw")
    pick = n("pick")
    sumall = n("sumall")
    prog.add(f"fn {fact}(int n) -> int {{")
    prog.add("    if (n <= 1) { return 1; }")
    prog.add(f"    return n * {fact}(n - 1);")
    prog.add("}")
    prog.add(f"fn {fib}(int n) -> int {{")
    prog.add("    if (n < 2) { return n; }")
    prog.add(f"    return {fib}(n - 1) + {fib}(n - 2);")
    prog.add("}")
    prog.add(f"fn {add3}(int a, int b = 5, int c = 9) -> int {{ return a + b + c; }}")
    prog.add(f"fn {show}(vararg int xs) -> void {{ println(xs); }}")
    prog.add(f"fn {showkw}(varkwarg int kw) -> void {{ println(kw); }}")
    prog.add(f'fn {pick}(int x) -> str {{ return "int " + str(x * 2); }}')
    prog.add(f'fn {pick}(str x) -> str {{ return "str " + x; }}')
    prog.add(f"fn {sumall}(vararg int xs) -> int {{")
    prog.add("    var total = 0;")
    prog.add("    for (int x in xs) { total += x; }")
    prog.add("    return total;")
    prog.add("}")
    prog.add("")

    nfact = rng.randrange(0, 8)
    prog.println(f"{fact}({nfact})", _fact(nfact))
    nfib = rng.randrange(0, 13)
    prog.println(f"{fib}({nfib})", _fib(nfib))
    a, b, c = rng.randrange(-9, 10), rng.randrange(-9, 10), rng.randrange(-9, 10)
    prog.println(f"{add3}({a})", a + 5 + 9)
    prog.println(f"{add3}({a}, {b})", a + b + 9)
    prog.println(f"{add3}({a}, c = {c})", a + 5 + c)
    prog.println(f"{add3}(c = {c}, b = {b}, a = {a})", a + b + c)

    for count in range(0, 4):
        args = [rng.randrange(-9, 10) for _ in range(count)]
        prog.println(f"{sumall}({', '.join(map(str, args))})", sum(args))
        prog.add(f"{show}({', '.join(map(str, args))});")
        prog.expect(args)
    kw_items = [(rng.choice(["alpha", "beta", "gama", "delta"]), rng.randrange(-9, 10))
                for _ in range(rng.randrange(1, 4))]
    # 키워드 이름이 겹치면 dict 삽입 순서가 흔들리므로 고유하게 만든다.
    seen = set()
    unique_items = []
    for key, value in kw_items:
        if key not in seen:
            seen.add(key)
            unique_items.append((key, value))
    call = ", ".join(f"{k} = {v}" for k, v in unique_items)
    prog.add(f"{showkw}({call});")
    prog.expect(dict(unique_items))

    x = rng.randrange(-20, 21)
    word = rng.choice(WORDS)
    prog.println(f'{pick}({x})', f"int {x * 2}")
    prog.println(f'{pick}("{word}")', f"str {word}")
    return Case(f"functions_{index}", {"main.ast": prog.source()}, prog.expected())


def _fact(n: int) -> int:
    result = 1
    for i in range(2, n + 1):
        result *= i
    return result


def _fib(n: int) -> int:
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a


def case_generics(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"g{index}")
    prog = Prog(("import System.stdout.*;", "from aster.lang.Number import Number;"))
    find = n("findmax")
    box = n("Box")
    prog.add(f"fn {find}<T extends Number>(T a, T b) -> T {{ return max(a, b); }}")
    prog.add(f"class {box}<T extends Number> {{")
    prog.add("    private T value;")
    prog.add("    constructor(T v) { self->value = v; }")
    prog.add("    public fn get() -> T { return self->value; }")
    prog.add("}")
    prog.add("")
    a, b = rng.randrange(-50, 51), rng.randrange(-50, 51)
    prog.println(f"{find}({a}, {b})", max(a, b))
    fa, fb = rng.choice(FLOAT_LEAVES), rng.choice(FLOAT_LEAVES)
    prog.println(f"{find}({repr(fa)}, {repr(fb)})", max(fa, fb))
    value = rng.randrange(-30, 31)
    var = n("boxed")
    prog.add(f"{box}<int>* {var} = new {box}<int>({value});")
    prog.println(f"{var}->get()", value)
    return Case(f"generics_{index}", {"main.ast": prog.source()}, prog.expected())


def case_classes(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"o{index}")
    base = n("Base")
    sub = n("Sub")
    iface = n("Scorer")
    impl = n("Impl")
    enum = n("Tone")
    factor = rng.randrange(2, 6)
    prog = Prog()
    prog.add(f"class {base} {{")
    prog.add("    protected int value;")
    prog.add("    constructor(int value) { self->value = value; }")
    prog.add("    constructor(int value, int bonus) { self->value = value + bonus; }")
    prog.add("    public fn add(int n) -> int { return self->value + n; }")
    prog.add("    public fn get() -> int { return self->value; }")
    prog.add("}")
    prog.add(f"class {sub} extends {base} {{")
    prog.add("    constructor(int value) { super(value); }")
    prog.add("    public fn add(int n) -> int { return self->value * 3 + n; }")
    prog.add("}")
    prog.add(f"interface {iface} {{")
    prog.add("    fn score(int n) -> int;")
    prog.add("}")
    prog.add(f"class {impl} implements {iface} {{")
    prog.add(f"    public fn score(int n) -> int {{ return n * {factor}; }}")
    prog.add("}")
    prog.add(f"enum {enum} {{")
    prog.add("    Low(int x), Mid, High(int x, int y);")
    prog.add("    public fn weight() -> int {")
    prog.add("        switch (self) {")
    prog.add(f"            case {enum}.Low(x) {{ return x * 2; }}")
    prog.add(f"            case {enum}.Mid {{ return -1; }}")
    prog.add(f"            case {enum}.High(x, y) {{ return x + y; }}")
    prog.add("        }")
    prog.add("    }")
    prog.add("}")
    prog.add("")

    value = rng.randrange(-20, 21)
    addend = rng.randrange(-9, 10)
    bonus = rng.randrange(0, 8)
    a = n("a")
    b = n("b")
    s = n("s")
    p = n("p")
    prog.add(f"{base}* {a} = new {base}({value});")
    prog.println(f"{a}->get()", value)
    prog.println(f"{a}->add({addend})", value + addend)
    prog.add(f"{base}* {b} = new {base}({value}, {bonus});")
    prog.println(f"{b}->get()", value + bonus)
    prog.add(f"{sub}* {s} = new {sub}({value});")
    prog.println(f"{s}->get()", value)
    prog.println(f"{s}->add({addend})", value * 3 + addend)
    prog.add(f"{base}* {p} = {s};")
    prog.println(f"{p}->add({addend})", value * 3 + addend)

    sv = n("scorer")
    score_arg = rng.randrange(-9, 10)
    prog.add(f"{iface} {sv} = new {impl}();")
    prog.println(f"{sv}.score({score_arg})", score_arg * factor)

    e1 = n("e1")
    e2 = n("e2")
    e3 = n("e3")
    x, y = rng.randrange(-9, 10), rng.randrange(-9, 10)
    prog.add(f"{enum} {e1} = {enum}.Low({x});")
    prog.add(f"{enum} {e2} = {enum}.Mid;")
    prog.add(f"{enum} {e3} = {enum}.High({x}, {y});")
    prog.println(f"{e1}.weight()", x * 2)
    prog.println(f"{e2}.weight()", -1)
    prog.println(f"{e3}.weight()", x + y)
    prog.println(f"{e1}.name", "Low")
    prog.println(f"{e3}.ordinal", 2)
    return Case(f"classes_{index}", {"main.ast": prog.source()}, prog.expected())


def case_patterns(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"p{index}")
    prog = Prog()
    classify = n("classify")
    prog.add(f"fn {classify}(list<int> xs) -> str {{")
    prog.add("    switch (xs) {")
    prog.add('        case [] { return "empty"; }')
    prog.add('        case [only] { return "one:" + str(only); }')
    prog.add('        case [a, b] when a == 0 { return "zero-first"; }')
    prog.add('        case [a, b] { return "two:" + str(a + b); }')
    prog.add('        case [first, *rest] { return "many:" + str(len(rest)); }')
    prog.add("    }")
    prog.add('    return "?";')
    prog.add("}")
    prog.add("")
    for _ in range(5):
        values = [rng.randrange(-5, 6) for _ in range(rng.randrange(0, 6))]
        prog.println(f'{classify}([{", ".join(map(str, values))}])',
                     _classify_list(values))

    person = n("person")
    prog.add(f"fn {person}(dict<str, int> d) -> str {{")
    prog.add("    switch (d) {")
    prog.add('        case {"a": x, **rest} { return "a=" + str(x) + "/" + str(len(rest)); }')
    prog.add('        case {"b": x} { return "b=" + str(x); }')
    prog.add('        default { return "none"; }')
    prog.add("    }")
    prog.add("}")
    prog.add("")
    for _ in range(4):
        model = {}
        if rng.random() < 0.7:
            model["a"] = rng.randrange(-9, 10)
            if rng.random() < 0.5:
                model["b"] = rng.randrange(-9, 10)
            if rng.random() < 0.4:
                model["c"] = rng.randrange(-9, 10)
        elif rng.random() < 0.6:
            model["b"] = rng.randrange(-9, 10)
            if rng.random() < 0.4:
                model["c"] = rng.randrange(-9, 10)
        body = ", ".join(f'"{k}": {v}' for k, v in model.items())
        prog.println(f'{person}({{{body}}})', _classify_dict(model))

    bag = n("Bag")
    bagfn = n("bag")
    prog.add(f"enum {bag} {{")
    prog.add("    Items(list<str> names), EmptyBag;")
    prog.add("}")
    prog.add(f"fn {bagfn}({bag} b) -> str {{")
    prog.add("    switch (b) {")
    prog.add(f'        case {bag}.Items([]) {{ return "no items"; }}')
    prog.add(f'        case {bag}.Items([only]) {{ return only; }}')
    prog.add(f'        case {bag}.Items([first, *rest]) {{ return first + "+" + str(len(rest)); }}')
    prog.add(f'        case {bag}.EmptyBag {{ return "empty"; }}')
    prog.add("    }")
    prog.add('    return "?";')
    prog.add("}")
    prog.add("")
    for _ in range(4):
        if rng.random() < 0.2:
            prog.println(f"{bagfn}({bag}.EmptyBag)", "empty")
        else:
            items = [rng.choice(WORDS) for _ in range(rng.randrange(0, 4))]
            text = ", ".join(f'"{w}"' for w in items)
            prog.println(f"{bagfn}({bag}.Items([{text}]))",
                         _bag_label([f'"{w}"' for w in items]))
    return Case(f"patterns_{index}", {"main.ast": prog.source()}, prog.expected())


def _classify_list(values: list[int]) -> str:
    if len(values) == 0:
        return "empty"
    if len(values) == 1:
        return f"one:{values[0]}"
    if len(values) == 2 and values[0] == 0:
        return "zero-first"
    if len(values) == 2:
        return f"two:{values[0] + values[1]}"
    return f"many:{len(values) - 1}"


def _classify_dict(model: dict) -> str:
    if "a" in model:
        return f"a={model['a']}/{len(model) - 1}"
    if "b" in model:
        return f"b={model['b']}"
    return "none"


def _bag_label(items: list[str]) -> str:
    words = [item.strip('"') for item in items]
    if not words:
        return "no items"
    if len(words) == 1:
        return words[0]
    return f"{words[0]}+{len(words) - 1}"


def case_modules(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"m{index}")
    mod = f"rmod{index}"
    kvalue = rng.randrange(-20, 21)
    xvalue = rng.randrange(-15, 16)
    bonus = rng.randrange(-9, 10)
    word = rng.choice(WORDS)
    module = (
        f"export const int K = {kvalue};\n"
        f"export fn twice(int x) -> int {{ return x * 2; }}\n"
        f'export fn greet(str name) -> str {{ return "hi " + name; }}\n'
        f"export class Wrap {{\n"
        f"    public int n;\n"
        f"    constructor(int n) {{ self->n = n; }}\n"
        f"    public fn plus(int d) -> int {{ return self->n + d; }}\n"
        f"}}\n"
    )
    prog = Prog()
    prog.add(f"import {mod} as m;")
    prog.add(f"from {mod} import K, twice as dbl, Wrap;")
    prog.println("m.K", kvalue)
    prog.println(f"dbl({xvalue})", xvalue * 2)
    prog.println(f"m.twice({xvalue})", xvalue * 2)
    wrap = n("wrap")
    prog.add(f"Wrap* {wrap} = new Wrap({xvalue});")
    prog.println(f"{wrap}->plus({bonus})", xvalue + bonus)
    prog.println(f'm.greet("{word}")', f"hi {word}")
    return Case(f"modules_{index}", {"main.ast": prog.source(), f"{mod}.ast": module},
                prog.expected())


def case_cycle(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"y{index}")
    k1, k2, k3, k4 = (rng.randrange(-9, 10) for _ in range(4))
    factor = rng.randrange(2, 6)
    x = rng.randrange(-9, 10)
    a_src = (
        "import cyc.b;\n"
        "\n"
        f"export int VA = b.VB + {k1};\n"
        f"export int ORDER = {k2};\n"
        "\n"
        "export class A {\n"
        "    public int v;\n"
        "    constructor(int v) { self->v = v; }\n"
        "    public fn ping() -> int { return self->v; }\n"
        "}\n"
        "\n"
        f"export fn fa(int x) -> int {{ return b.fb(x) + {k3}; }}\n"
    )
    b_src = (
        "import cyc.a;\n"
        "\n"
        f"export int VB = a.ORDER + {k4};\n"
        "\n"
        "export class B extends A {\n"
        "    constructor(int v) { super(v); }\n"
        "    public fn doubled() -> int { return self->ping() * 2; }\n"
        "}\n"
        "\n"
        f"export fn fb(int x) -> int {{ return x * {factor}; }}\n"
    )
    prog = Prog()
    prog.add("import cyc.a;")
    prog.add("import cyc.b;")
    prog.add("from cyc.a import A;")
    prog.add("from cyc.b import B;")
    prog.println("a.VA", k4 + k1)
    prog.println("b.VB", k4)
    prog.println("a.ORDER", k2)
    prog.println(f"a.fa({x})", x * factor + k3)
    p = n("pa")
    q = n("qb")
    up = n("up")
    prog.add(f"A* {p} = new A({x});")
    prog.println(f"{p}->ping()", x)
    prog.add(f"B* {q} = new B({x});")
    prog.println(f"{q}->ping()", x)
    prog.println(f"{q}->doubled()", x * 2)
    prog.add(f"A* {up} = {q};")
    prog.println(f"{up}->ping()", x)
    return Case(f"cycle_{index}", {
        "main.ast": prog.source(),
        "cyc/a.ast": a_src,
        "cyc/b.ast": b_src,
    }, prog.expected())


def case_exceptions(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"x{index}")
    op = n("op")
    label = n("label")
    cleanup = n("cleanup")
    hold = n("hold")
    prog = Prog()
    prog.add("exception AppErr extends Exception;")
    prog.add("exception ParseErr extends AppErr;")
    prog.add("")
    prog.add(f"fn {op}(int n) -> int {{")
    prog.add('    if (n < 0) { throw ParseErr("neg"); }')
    prog.add('    if (n == 0) { throw "zero" as ValueError; }')
    prog.add('    if (n == 1) { throw AppErr("one"); }')
    prog.add("    return n * 3;")
    prog.add("}")
    prog.add("")
    prog.add(f"fn {label}(int n) -> str {{")
    prog.add("    try {")
    prog.add(f"        int v = {op}(n);")
    prog.add('        return "ok:" + str(v);')
    prog.add("    } catch (ParseErr e) {")
    prog.add('        return "parse:" + e.message;')
    prog.add("    } catch (AppErr e) {")
    prog.add('        return "app:" + e.name + ":" + e.message;')
    prog.add("    } catch (ValueError e) {")
    prog.add('        return "value:" + e.message;')
    prog.add("    } catch (Exception e) {")
    prog.add('        return "other:" + e.name;')
    prog.add("    }")
    prog.add("}")
    prog.add("")
    for _ in range(5):
        value = rng.randrange(-4, 5)
        prog.println(f"{label}({value})", _exception_label(value))

    prog.block(
        "try {",
        '    println("A");',
        "} finally {",
        '    println("B");',
        "}",
    )
    prog.expect("A")
    prog.expect("B")

    # catch 없이 finally만 쓰면 예외를 삼킨다.
    prog.block(
        "try {",
        '    throw "quiet";',
        "}",
        'println("after-quiet");',
    )
    prog.expect("after-quiet")

    # finally가 끝난 뒤에도 예외는 다시 던져진다.
    prog.add(f"fn {cleanup}() -> void {{")
    prog.add("    try {")
    prog.add('        throw "inner";')
    prog.add("    } finally {")
    prog.add('        println("cleanup");')
    prog.add("    }")
    prog.add("}")
    prog.add("try {")
    prog.add(f"    {cleanup}();")
    prog.add("} catch (Exception e) {")
    prog.add('    println("outer:" + e.message);')
    prog.add("}")
    prog.expect("cleanup")
    prog.expect("outer:inner")

    prog.block(
        'var stored = AppErr("saved");',
        "try {",
        "    throw stored;",
        "} catch (AppErr e) {",
        '    println("var:" + e.message);',
        "}",
    )
    prog.expect("var:saved")

    # return은 finally를 먼저 실행한 뒤 값을 돌려준다.
    big = rng.randrange(-5, 6)
    prog.add(f"fn {hold}(int n) -> str {{")
    prog.add("    try {")
    prog.add('        if (n > 2) { return "big"; }')
    prog.add('        return "small";')
    prog.add("    } finally {")
    prog.add('        println("cleanup2");')
    prog.add("    }")
    prog.add("}")
    prog.println(f"{hold}({big})", "big" if big > 2 else "small")
    # println은 인자를 먼저 평가하므로 cleanup2가 값보다 앞선다.
    prog.want.insert(len(prog.want) - 1, "cleanup2")
    return Case(f"exceptions_{index}", {"main.ast": prog.source()}, prog.expected())


def _exception_label(value: int) -> str:
    if value < 0:
        return "parse:neg"
    if value == 0:
        return "value:zero"
    if value == 1:
        return "app:AppErr:one"
    return f"ok:{value * 3}"


def _seq_values(limit: int, step: int, mod: int) -> list[int]:
    return [i * step for i in range(1, limit + 1) if i % mod != 0]


def case_generators(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"q{index}")
    seq = n("seq")
    upto = n("upto")
    letters = n("letters")
    countdown = n("Countdown")
    prog = Prog()
    prog.add(f"fn {seq}(int n, int step, int mod) -> list<int> {{")
    prog.add("    for (int i = 1; i <= n; i++) {")
    prog.add("        if (i % mod == 0) { continue; }")
    prog.add("        yield i * step;")
    prog.add("    }")
    prog.add("}")
    prog.add(f"fn {upto}(int n) -> list<int> {{")
    prog.add("    for (int i = 0; i < n; i++) { yield i; }")
    prog.add("}")
    prog.add(f"fn {letters}(str s) -> list<str> {{")
    prog.add("    for (int i = 0; i < len(s); i++) { yield s[i]; }")
    prog.add("}")
    prog.add(f"class {countdown} {{")
    prog.add("    private int left;")
    prog.add("    constructor(int n) { self->left = n; }")
    prog.add(f"    public fn __iter__() -> {countdown} {{ return self; }}")
    prog.add("    public fn __next__() -> int {")
    prog.add("        if (self->left <= 0) { throw StopIteration(); }")
    prog.add("        self->left -= 1;")
    prog.add("        return self->left;")
    prog.add("    }")
    prog.add("}")
    prog.add("")

    limit = rng.randrange(4, 10)
    step = rng.randrange(1, 5)
    mod = rng.randrange(2, 7)
    values = _seq_values(limit, step, mod)

    prog.add(f"for (int v in {seq}({limit}, {step}, {mod})) {{ println(v); }}")
    for value in values:
        prog.expect(value)

    total = n("total")
    prog.add(f"var {total} = 0;")
    prog.add(f"for (int v in {seq}({limit}, {step}, {mod})) {{ {total} += v; }}")
    prog.println(total, sum(values))

    g = n("g")
    prog.add(f"var {g} = {seq}({limit}, {step}, {mod});")
    prog.println(f"next({g})", values[0])
    prog.println(f"next({g})", values[1])
    prog.println(f"list({g})", values[2:])

    h = n("h")
    prog.add(f"var {h} = {upto}(3);")
    prog.add("try {")
    prog.add(f"    println(next({h}));")
    prog.add(f"    println(next({h}));")
    prog.add(f"    println(next({h}));")
    prog.add(f"    println(next({h}));")
    prog.add("} catch (StopIteration e) {")
    prog.add('    println("stop");')
    prog.add("}")
    for value in [0, 1, 2]:
        prog.expect(value)
    prog.expect("stop")

    word = rng.choice(WORDS)
    prog.println(f'list({letters}("{word}"))', list(word))
    joined = n("joined")
    prog.add(f'var {joined} = "";')
    prog.add(f'for (str ch in {letters}("{word}")) {{ {joined} = {joined} + ch; }}')
    prog.println(joined, word)

    start = rng.randrange(1, 6)
    prog.add(f"for (int v in new {countdown}({start})) {{ println(v); }}")
    for value in range(start - 1, -1, -1):
        prog.expect(value)
    return Case(f"generators_{index}", {"main.ast": prog.source()}, prog.expected())


def case_lambdas(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"z{index}")
    mapper = n("Mapper")
    runner = n("runner")
    chain = n("chain")
    twice = n("twice")
    addend = rng.randrange(1, 8)
    bias = rng.randrange(-6, 7)
    constant = rng.randrange(-20, 21)
    mapper_bias = rng.randrange(-9, 10)
    arg = rng.randrange(-12, 13)
    xs = [rng.randrange(-9, 10) for _ in range(rng.randrange(3, 7))]

    prog = Prog()
    prog.add(f"interface {mapper}<T> {{")
    prog.add("    fn apply(T x) -> T;")
    prog.add("}")
    prog.add(f"fn {runner}(fn(int) -> int f, int x) -> int {{ return f(x); }}")
    prog.add(f"fn {chain}(fn(int) -> int f, fn(int) -> int g, int x) -> int {{ return f(g(x)); }}")
    prog.add(f"fn {twice}({mapper}<int> m, int x) -> int {{ return m.apply(m.apply(x)); }}")
    prog.add("")
    prog.add("var dbl = (int x) -> int => x * 2;")
    prog.add(f"var inc = x -> int => x + {addend};")
    prog.add(f"var zero = () -> int => {constant};")
    prog.add("float half = (int x) -> float => (float)x / 2.0;")
    prog.add(f"{mapper}<int> plus = (int x) -> int => x + {mapper_bias};")
    prog.add(f'var show = (int x) -> str => "v" + str(x);')
    prog.add(f"quad = x => x * 4 -> int;")
    prog.add(f"list<int> xs = [{', '.join(map(str, xs))}];")
    prog.add("")

    prog.println(f"dbl({arg})", arg * 2)
    prog.println(f"inc({arg})", arg + addend)
    prog.println("zero()", constant)
    prog.println(f"half({arg})", arg / 2.0)
    prog.println(f"{runner}(dbl, {arg})", arg * 2)
    prog.println(f"{chain}(dbl, inc, {arg})", (arg + addend) * 2)
    prog.println(f"plus.apply({arg})", arg + mapper_bias)
    prog.println(f"{twice}(plus, {arg})", arg + mapper_bias * 2)
    prog.println(f"show({arg})", f"v{arg}")
    prog.println(f"quad({arg})", arg * 4)
    prog.println("xs.map(x -> int => x * 3)", [x * 3 for x in xs])
    prog.println("xs.filter(x -> bool => x % 2 == 0)", [x for x in xs if x % 2 == 0])
    prog.println("xs.map(x -> int => x + 1).filter(x -> bool => x > 0)",
                 [x + 1 for x in xs if x + 1 > 0])
    prog.add("xs.sort(x -> int => 0 - x);")
    prog.println("xs", sorted(xs, reverse=True))
    return Case(f"lambdas_{index}", {"main.ast": prog.source()}, prog.expected())


def case_structs(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"u{index}")
    counter = n("Counter")
    named = n("Named")
    pt = n("Pt")
    a, b, c, d = (rng.randrange(-12, 13) for _ in range(4))
    changed = rng.randrange(-12, 13)
    seed_value = rng.randrange(-9, 10)
    second = rng.randrange(-9, 10)

    prog = Prog()
    prog.add(f"class {counter} {{")
    prog.add("    static int total = 0;")
    prog.add(f"    public static fn next() -> int {{")
    prog.add(f"        {counter}.total = {counter}.total + 1;")
    prog.add(f"        return {counter}.total;")
    prog.add("    }")
    prog.add("}")
    prog.add(f"class {named} {{")
    prog.add("    private int _v;")
    prog.add("    public @property")
    prog.add("    int value;")
    prog.add("    getter value { return self->_v; }")
    prog.add("    setter value(int value) { self->_v = value; }")
    prog.add("    constructor(int v) { self->_v = v; }")
    prog.add("    public fn bump() -> void { self->value += 1; }")
    prog.add("}")
    prog.add(f"typedef struct {pt} {{")
    prog.add("    int x;")
    prog.add("    int y;")
    prog.add(f"    {pt}(int a, int b): x(a), y(b) {{}}")
    prog.add(f"}} {pt};")
    prog.add("")

    p = n("p")
    q = n("q")
    prog.add(f"struct {pt} {p} = {{ .x = {a}, .y = {b} }};")
    prog.add(f"{pt}* {q} = new {pt}({c}, {d});")
    prog.println(f"{p}.x", a)
    prog.println(f"{p}.y", b)
    prog.println(f"{q}->x", c)
    prog.println(f"{q}->y", d)
    prog.add(f"{p}.x = {changed};")
    prog.println(f"{p}.x", changed)
    prog.println(f"{p}.y", b)
    copy = n("copy")
    prog.add(f"struct {pt} {copy} = {p};")
    prog.add(f"{copy}.y = {second};")
    prog.println(f"{p}.y", b)
    prog.println(f"{copy}.y", second)
    prog.println(f"sizeof(struct {pt}) > 0", True)

    nm = n("nm")
    prog.add(f"{named}* {nm} = new {named}({seed_value});")
    prog.println(f"{nm}->value", seed_value)
    prog.add(f"{nm}->bump();")
    prog.println(f"{nm}->value", seed_value + 1)
    prog.add(f"{nm}->value = {second};")
    prog.println(f"{nm}->value", second)

    prog.println(f"{counter}.next()", 1)
    prog.println(f"{counter}.next()", 2)
    prog.println(f"{counter}.total", 2)
    return Case(f"structs_{index}", {"main.ast": prog.source()}, prog.expected())


def case_comprehensions(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"w{index}")
    xs = [rng.randrange(-9, 10) for _ in range(rng.randrange(3, 8))]
    factor = rng.randrange(2, 6)
    mod = rng.randrange(2, 6)
    threshold = rng.randrange(-5, 6)
    prefix = rng.randrange(-9, 10)
    suffix = rng.randrange(-9, 10)
    extra = rng.randrange(-9, 10)
    base_value = rng.randrange(-20, 21)
    added_value = rng.randrange(-20, 21)
    offset = rng.randrange(1, 6)

    prog = Prog()
    prog.add(f"var xs = [{', '.join(map(str, xs))}];")
    prog.println(f"[x * {factor} for x in xs if x % {mod} != 0]",
                 [x * factor for x in xs if x % mod != 0])
    prog.println(f"[x + {offset} for x in xs if x > {threshold}]",
                 [x + offset for x in xs if x > threshold])

    set_values = {x * 2 for x in xs}
    prog.add("var doubled = {x * 2 for x in xs};")
    prog.println("len(doubled)", len(set_values))
    prog.println(f"{extra} in doubled", extra in set_values)
    prog.println(f"{xs[0] * 2} in doubled", xs[0] * 2 in set_values)

    table = {x: x * x for x in xs}
    prog.add("var table = {x: x * x for x in xs};")
    prog.println("len(table)", len(table))
    prog.println(f"table[{xs[0]}]", table[xs[0]])
    prog.println(f"table[{xs[-1]}]", table[xs[-1]])
    prog.println("len({x: x + 1 for x in xs})", len({x: x + 1 for x in xs}))

    prog.println(f"[{prefix}, *xs, {suffix}]", [prefix, *xs, suffix])
    prog.add(f"var merged = {{*xs, {extra}}};")
    prog.println("len(merged)", len(set(xs) | {extra}))
    prog.println(f"{extra} in merged", True)
    prog.add('var base = {"a": ' + str(base_value) + "};")
    prog.println('{**base, "b": ' + str(added_value) + "}",
                 {"a": base_value, "b": added_value})
    prog.println("base[\"a\"]", base_value)
    prog.println("len(base)", 1)

    start = rng.randrange(-4, 5)
    step = rng.choice([1, 2, 3, -1, -2])
    count = rng.randrange(1, 6)
    stop = start + step * count
    total = n("total")
    items = n("items")
    prog.add(f"var {total} = 0;")
    prog.add(f"var {items} = [];")
    prog.add(f"for (int i; i = {start}..{stop}..{step}) {{")
    prog.add(f"    {total} += i;")
    prog.add(f"    {items}.append(i);")
    prog.add("}")
    prog.println(total, sum(range(start, stop, step)))
    prog.println(items, list(range(start, stop, step)))

    prog.println(f"xs.map(x -> int => x + {offset})", [x + offset for x in xs])
    prog.println(f"xs.filter(x -> bool => x != {threshold})",
                 [x for x in xs if x != threshold])
    prog.add(f"var (first, second) = [{xs[0]}, {xs[1]}];")
    prog.println("first", xs[0])
    prog.println("second", xs[1])
    return Case(f"comprehensions_{index}", {"main.ast": prog.source()}, prog.expected())


def case_nullable(rng: random.Random, index: int) -> Case:
    n = Names(rng, f"v{index}")
    node = n("Node")
    find = n("find")
    label = n("label")
    widen = n("widen")
    maybe_str = n("maybeStr")
    threshold = rng.randrange(0, 6)
    word = rng.choice(WORDS)
    fallback = rng.choice(["none", "nothing", "missing"])
    value = rng.randrange(-9, 10)
    default_int = rng.randrange(-9, 10)

    prog = Prog()
    prog.add(f"class {node} {{")
    prog.add("    public int value;")
    prog.add("    constructor(int v) { self->value = v; }")
    prog.add("    public fn get() -> int { return self->value; }")
    prog.add("}")
    prog.add(f"fn {find}(int k) -> {node}? {{")
    prog.add(f"    if (k > {threshold}) {{ return new {node}(k); }}")
    prog.add("    return null;")
    prog.add("}")
    prog.add(f"fn {label}(int k) -> str {{")
    prog.add(f"    {node}? found = {find}(k);")
    prog.add("    if (found == null) { return \"none\"; }")
    prog.add('    return "node:" + str(found->get());')
    prog.add("}")
    prog.add(f"fn {widen}(int k) -> str {{")
    prog.add(f"    {node}? found = {find}(k);")
    prog.add("    if (found != null) { return \"have:\" + str(found->value); }")
    prog.add('    return "miss";')
    prog.add("}")
    prog.add(f"fn {maybe_str}(int k) -> str? {{")
    prog.add(f'    if (k > {threshold}) {{ return "{word}"; }}')
    prog.add("    return null;")
    prog.add("}")
    prog.add("")

    below = threshold - rng.randrange(1, 4)
    above = threshold + rng.randrange(1, 4)
    prog.println(f"{label}({below})", "none")
    prog.println(f"{label}({above})", f"node:{above}")
    prog.println(f"{widen}({below})", "miss")
    prog.println(f"{widen}({above})", f"have:{above}")

    prog.println(f'{maybe_str}({below}) ?? "{fallback}"', fallback)
    prog.println(f"{maybe_str}({below}) == null", True)
    prog.println(f"{maybe_str}({above}) == null", False)
    prog.println(f"{maybe_str}({below})?.length() ?? -1", -1)
    prog.println(f"{maybe_str}({above})?.length() ?? -1", len(word))

    b = n("b")
    prog.add(f"str? {b} = {maybe_str}({above});")
    prog.add(f"if ({b} != null) {{")
    prog.add(f"    println({b}.upper());")
    prog.add("} else {")
    prog.add('    println("null");')
    prog.add("}")
    prog.expect(word.upper())

    count = n("count")
    prog.add("int? " + count + " = null;")
    prog.println(f"{count} ?? {default_int}", default_int)
    prog.add(f"{count} = {value};")
    prog.println(f"{count} ?? {default_int}", value)

    prog.println(f"{find}({below})?.get() ?? -1", -1)
    prog.println(f"{find}({above})?.get() ?? -1", above)
    prog.println(f"{find}({below}) == null", True)
    prog.println(f"{find}({above}) == null", False)

    prog.add(f'{node}* raw = new {node}({value});')
    prog.println(f"{find}({above})?.get() + raw->get()", above + value)
    prog.add("var nothing = null;")
    prog.println("nothing == null", True)
    return Case(f"nullable_{index}", {"main.ast": prog.source()}, prog.expected())


CATEGORIES = {
    "expr": case_expr,
    "strings": case_strings,
    "control": case_control,
    "collections": case_collections,
    "functions": case_functions,
    "generics": case_generics,
    "classes": case_classes,
    "patterns": case_patterns,
    "modules": case_modules,
    "cycle": case_cycle,
    "exceptions": case_exceptions,
    "generators": case_generators,
    "lambdas": case_lambdas,
    "structs": case_structs,
    "comprehensions": case_comprehensions,
    "nullable": case_nullable,
}

CATEGORY_ORDER = list(CATEGORIES)

# 확장 문법(예외/제너레이터/람다/구조체/컴프리헨션/null)만 모아 둔 순서.
EXTENDED_CATEGORIES = [
    "exceptions", "generators", "lambdas", "structs",
    "comprehensions", "nullable",
]


# ------------------------------------------------------------------ 실행


def default_seed() -> int:
    env = os.environ.get("ASTER_TEST_SEED")
    if env:
        try:
            return int(env)
        except ValueError:
            pass
    return random.SystemRandom().randrange(2 ** 32)


def generate_cases(seed: int, count: int, categories=None) -> list[Case]:
    # 어떤 검사를 하는지는 고정하고, 검사 대상 코드의 내용만 시드로 바꾼다.
    rng = random.Random(seed)
    names = list(categories) if categories else list(CATEGORY_ORDER)
    cases: list[Case] = []
    for i in range(count):
        category = names[i % len(names)]
        cases.append(CATEGORIES[category](rng, i))
    return cases


def _run(binary: Path, args: list[str], stdin_text: str | None = None):
    return subprocess.run(
        [str(binary), *args],
        input=stdin_text,
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=str(ROOT),
        timeout=60,
    )


def _write_files(directory: Path, files: dict[str, str]) -> Path:
    for name, text in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return directory / "main.ast"


def _write_case(directory: Path, case: Case) -> Path:
    return _write_files(directory, case.files)


def _save_failure_files(name: str, files: dict[str, str], problems: list[str],
                        seed: int) -> Path:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    target = FAILURE_DIR / f"{stamp}-{seed}-{name}"
    target.mkdir(parents=True, exist_ok=True)
    for filename, text in files.items():
        path = target / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    (target / "problems.txt").write_text("\n".join(problems) + "\n", encoding="utf-8")
    return target


def _save_failure(case: Case, problems: list[str], seed: int) -> Path:
    target = _save_failure_files(case.name, case.files, problems, seed)
    (target / "expected.txt").write_text(case.expected, encoding="utf-8")
    return target


def check_case(binary: Path, case: Case, directory: Path, property_only: bool) -> list[str]:
    main = _write_case(directory, case)
    problems: list[str] = []

    parsed = _run(binary, ["--ast", str(main)])
    if parsed.returncode != 0:
        problems.append(f"--ast 실패: {parsed.stderr.strip()[:400]}")

    formatted = _run(binary, ["--format", str(main)])
    if formatted.returncode != 0:
        problems.append(f"--format 실패: {formatted.stderr.strip()[:400]}")
    else:
        again = _run(binary, ["--format", "-"], formatted.stdout)
        if again.returncode != 0:
            problems.append(f"--format - 실패: {again.stderr.strip()[:400]}")
        elif again.stdout != formatted.stdout:
            problems.append("포매터 출력이 고정점이 아닙니다")

    if property_only:
        return problems

    result = _run(binary, [str(main)])
    if result.returncode != 0:
        problems.append(f"실행 실패(exit {result.returncode}): {result.stderr.strip()[:400]}")
    elif result.stdout != case.expected:
        problems.append(
            "실행 출력 불일치: " + _describe_output(case.expected, result.stdout)
        )

    checked = _run(binary, ["--check", str(main)])
    if checked.returncode != 0:
        problems.append(f"--check 실패: {checked.stderr.strip()[:400]}")

    if formatted.returncode == 0:
        fmt_path = directory / "formatted.ast"
        fmt_path.write_text(formatted.stdout, encoding="utf-8")
        rerun = _run(binary, [str(fmt_path)])
        if rerun.returncode != 0:
            problems.append(f"포맷본 실행 실패: {rerun.stderr.strip()[:400]}")
        elif rerun.stdout != case.expected:
            problems.append(
                "포맷본 실행 출력 불일치: " + _describe_output(case.expected, rerun.stdout)
            )

    return problems


def _describe_output(expected: str, got: str) -> str:
    want_lines = expected.splitlines()
    got_lines = got.splitlines()
    for index, (want, actual) in enumerate(zip(want_lines, got_lines)):
        if want != actual:
            return f"{index + 1}행 got={actual!r} want={want!r}"
    return f"줄 수 got={len(got_lines)} want={len(want_lines)}"


def run_cases(binary: Path, cases: list[Case], seed: int, property_only: bool = False,
              verbose: bool = False, keep: bool = False) -> tuple[int, list[tuple[str, list[str]]]]:
    BUILD_TMP.mkdir(parents=True, exist_ok=True)
    workdir = Path(tempfile.mkdtemp(prefix=f"random-{seed}-", dir=str(BUILD_TMP)))
    failures: list[tuple[str, list[str]]] = []
    passed = 0
    try:
        for case in cases:
            directory = workdir / case.name
            directory.mkdir(parents=True, exist_ok=True)
            problems = check_case(binary, case, directory, property_only)
            if problems:
                failures.append((case.name, problems))
                saved = _save_failure(case, problems, seed)
                print(f"FAIL: {case.name} (보존: {saved.relative_to(ROOT)})")
                for problem in problems:
                    print(f"    {problem}")
            else:
                passed += 1
                if verbose:
                    print(f"  ok random {case.name}")
    finally:
        if keep:
            print(f"무작위 케이스 보존: {workdir.relative_to(ROOT)}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)
    return passed, failures


def run_random(binary: Path, count: int, seed: int | None = None, categories=None,
               property_only: bool = False, verbose: bool = False,
               keep: bool = False, label: str = "무작위",
               repro: str = "python3 Tools/check_random.py") -> int:
    seed = default_seed() if seed is None else seed
    cases = generate_cases(seed, count, categories)
    passed, failures = run_cases(binary, cases, seed, property_only, verbose, keep)
    print()
    print(f"{label}: {passed}/{len(cases)} 통과 (seed={seed})")
    if failures:
        print(f"재현: {repro} --seed {seed}")
        return 1
    return 0


# ------------------------------------------------------------------ 부정 케이스
# 컴파일러가 반드시 거부해야 하는 프로그램. 거부되지 않거나 다른 이유로
# 실패하면 검증 실패로 본다. 내용(이름, 값)은 시드로 바꾸지만 검사 항목은 고정이다.


@dataclass
class NegativeCase:
    name: str
    files: dict[str, str]
    expect: str


def _negative(name: str, source: str, expect: str,
              extra: dict[str, str] | None = None) -> NegativeCase:
    files = {"main.ast": source}
    if extra:
        files.update(extra)
    return NegativeCase(name, files, expect)


def negative_cases(seed: int) -> list[NegativeCase]:
    rng = random.Random(seed ^ 0x5EED)
    word = rng.choice(WORDS)
    number = rng.randrange(-20, 21)
    suffix = f"{rng.randrange(100, 999)}"
    module = f"negmod{suffix}"
    cases = [
        _negative(
            "undefined_name",
            "import System.stdout.*;\n"
            f"int x = {number};\n"
            f"println(x + missing{suffix});\n",
            "undefined name",
        ),
        _negative(
            "init_type_mismatch",
            "import System.stdout.*;\n"
            f'int x = "{word}";\n'
            "println(x);\n",
            "variable initializer: expected",
        ),
        _negative(
            "missing_semicolon",
            "import System.stdout.*;\n"
            f"int x = {number}\n"
            "println(x);\n",
            "syntax error",
        ),
        _negative(
            "unknown_class_member",
            "import System.stdout.*;\n"
            "class Box {\n"
            "    public int n;\n"
            "    constructor(int n) { self->n = n; }\n"
            "}\n"
            f"Box* b = new Box({number});\n"
            f"println(b->secret{suffix});\n",
            "has no member",
        ),
        _negative(
            "wrong_arg_count",
            "import System.stdout.*;\n"
            "fn add(int a, int b) -> int { return a + b; }\n"
            f"println(add({number}));\n",
            "argument",
        ),
        _negative(
            "wrong_return_type",
            "import System.stdout.*;\n"
            f'fn bad(int n) -> int {{ return "{word}"; }}\n'
            f"println(bad({number}));\n",
            "return value: expected int but got str",
        ),
        _negative(
            "const_assign",
            "import System.stdout.*;\n"
            f"const int K{suffix} = {number};\n"
            f"K{suffix} = {number + 1};\n"
            f"println(K{suffix});\n",
            "cannot assign to const",
        ),
        _negative(
            "null_init",
            "import System.stdout.*;\n"
            f"int x = null;\n"
            f"println(x + {number});\n",
            "variable initializer",
        ),
        _negative(
            "private_access",
            "import System.stdout.*;\n"
            "class Hidden {\n"
            "    private int secret;\n"
            "    constructor(int v) { self->secret = v; }\n"
            "}\n"
            f"Hidden* h = new Hidden({number});\n"
            f"println(h->secret);\n",
            "is private",
        ),
        _negative(
            "module_unknown_member",
            "import System.stdout.*;\n"
            f"import {module} as m;\n"
            f"println(m.missing{suffix});\n",
            "has no member",
            {f"{module}.ast": f"export int K = {number};\n"},
        ),
        _negative(
            "arg_type_mismatch",
            "import System.stdout.*;\n"
            "fn ident(int n) -> int { return n; }\n"
            f'println(ident("{word}"));\n',
            "expected int but got str",
        ),
        _negative(
            "assign_type_mismatch",
            "import System.stdout.*;\n"
            f"int x = {number};\n"
            f'x = "{word}";\n'
            "println(x);\n",
            "assignment: expected int but got str",
        ),
        _negative(
            "list_index_type",
            "import System.stdout.*;\n"
            f"var xs = [{number}, {number + 1}];\n"
            'println(xs["0"]);\n',
            "index must be int",
        ),
        _negative(
            "list_append_type",
            "import System.stdout.*;\n"
            f"var xs = [{number}, {number + 1}];\n"
            f'xs.append("{word}");\n'
            "println(len(xs));\n",
            "expected int but got str",
        ),
        _negative(
            "dict_key_type",
            "import System.stdout.*;\n"
            f'var d = {{"k": {number}}};\n'
            f"println(d[{number}]);\n",
            "dict key",
        ),
        _negative(
            "set_subscript",
            "import System.stdout.*;\n"
            f"var s = {{{number}, {number + 1}}};\n"
            "println(s[0]);\n",
            "not subscriptable",
        ),
        _negative(
            "struct_unknown_member",
            "import System.stdout.*;\n"
            "typedef struct P {\n"
            "    int x;\n"
            "} P;\n"
            f"struct P p = {{ .x = {number} }};\n"
            f"println(p.y{suffix});\n",
            "has no member",
        ),
        _negative(
            "enum_unknown_member",
            "import System.stdout.*;\n"
            f"enum Color {{ RED{suffix}, GREEN{suffix} }}\n"
            f"println(Color.BLUE{suffix}.name);\n",
            "has no member",
        ),
        _negative(
            "missing_return",
            "import System.stdout.*;\n"
            "fn partial(int n) -> int {\n"
            "    if (n > 0) { return n; }\n"
            "}\n"
            f"println(partial({number}));\n",
            "missing return",
        ),
        _negative(
            "yield_outside_function",
            "import System.stdout.*;\n"
            f"yield {number};\n",
            "'yield' is only allowed inside functions",
        ),
        _negative(
            "break_outside_loop",
            "import System.stdout.*;\n"
            f"println({number});\n"
            "break;\n",
            "'break' is only allowed inside a loop",
        ),
        _negative(
            "continue_outside_loop",
            "import System.stdout.*;\n"
            f"println({number});\n"
            "continue;\n",
            "'continue' is only allowed inside a loop",
        ),
        _negative(
            "yield_type_mismatch",
            "import System.stdout.*;\n"
            "fn gen() -> list<int> {\n"
            f'    yield "{word}";\n'
            "}\n"
            "println(list(gen()));\n",
            "yield value: expected int but got str",
        ),
        _negative(
            "void_returns_value",
            "import System.stdout.*;\n"
            "fn nothing() -> void { return 1; }\n"
            "nothing();\n",
            "cannot return a value from a void function",
        ),
        _negative(
            "lambda_as_function_type",
            "import System.stdout.*;\n"
            "fn(int) -> int f = (int x) -> int => x;\n"
            "println(f(1));\n",
            "a lambda cannot be declared with a function type",
        ),
        _negative(
            "null_dereference",
            "import System.stdout.*;\n"
            "class Node {\n"
            "    public int v;\n"
            "    constructor(int v) { self->v = v; }\n"
            "}\n"
            "fn find(int k) -> Node? {\n"
            "    if (k > 0) { return new Node(k); }\n"
            "    return null;\n"
            "}\n"
            "Node? n = find(1);\n"
            "println(n->v);\n",
            "possible null access",
        ),
        _negative(
            "interface_not_implemented",
            "import System.stdout.*;\n"
            "interface Named {\n"
            "    fn name() -> str;\n"
            "}\n"
            "class Bad implements Named {\n"
            f'    public fn other() -> str {{ return "{word}"; }}\n'
            "}\n"
            'println("built");\n',
            "does not implement interface",
        ),
        _negative(
            "forin_element_type",
            "import System.stdout.*;\n"
            f'for (int v in ["{word}", "{word}"]) {{ println(v); }}\n',
            "for-in variable: expected int",
        ),
    ]
    return cases


def check_negative(binary: Path, case: NegativeCase, directory: Path) -> list[str]:
    main = _write_files(directory, case.files)
    result = _run(binary, ["--check", str(main)])
    problems: list[str] = []
    if result.returncode == 0:
        problems.append("컴파일이 성공했습니다 (오류를 기대)")
    elif case.expect and case.expect not in result.stderr:
        detail = " / ".join(result.stderr.strip().splitlines()[:2])
        problems.append(f"stderr에 {case.expect!r}가 없습니다: {detail}")
    return problems


def run_negative(binary: Path, seed: int | None = None, verbose: bool = False,
                 keep: bool = False, label: str = "부정 검증",
                 repro: str = "python3 Tools/check_random.py") -> int:
    seed = default_seed() if seed is None else seed
    cases = negative_cases(seed)
    BUILD_TMP.mkdir(parents=True, exist_ok=True)
    workdir = Path(tempfile.mkdtemp(prefix=f"negative-{seed}-", dir=str(BUILD_TMP)))
    passed = 0
    failed = 0
    try:
        for case in cases:
            directory = workdir / case.name
            directory.mkdir(parents=True, exist_ok=True)
            problems = check_negative(binary, case, directory)
            if problems:
                failed += 1
                saved = _save_failure_files(case.name, case.files, problems, seed)
                print(f"FAIL: {case.name} (보존: {saved.relative_to(ROOT)})")
                for problem in problems:
                    print(f"    {problem}")
            else:
                passed += 1
                if verbose:
                    print(f"  ok negative {case.name}")
    finally:
        if keep:
            print(f"부정 케이스 보존: {workdir.relative_to(ROOT)}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)
    print()
    print(f"{label}: {passed}/{len(cases)} 거부됨 (seed={seed})")
    if failed:
        print(f"재현: {repro} --seed {seed}")
        return 1
    return 0


# ------------------------------------------------------------------ REPL


def run_repl_random(binary: Path, count: int, seed: int | None = None,
                    verbose: bool = False,
                    repro: str = "python3 Tools/check_repl.py") -> int:
    seed = default_seed() if seed is None else seed
    rng = random.Random(seed)
    sessions = []
    for _ in range(count):
        lines = []
        expected = []
        for _ in range(rng.randrange(3, 6)):
            text, value = gen_int(rng, rng.randrange(1, 4))
            lines.append(text)
            expected.append(str(value))
        sessions.append(("\n".join(lines) + "\n", "\n".join(expected) + "\n"))

    failures = 0
    for index, (session, want) in enumerate(sessions):
        result = _run(binary, [], session)
        if result.returncode != 0 or result.stdout != want:
            failures += 1
            print(f"FAIL: repl_random_{index} (seed={seed})")
            print("    " + _describe_output(want, result.stdout))
        elif verbose:
            print(f"  ok random repl_{index}")
    print()
    print(f"REPL 무작위: {count - failures}/{count} 통과 (seed={seed})")
    if failures:
        print(f"재현: {repro} --seed {seed}")
        return 1
    return 0


__all__ = [
    "Case", "NegativeCase", "CATEGORIES", "CATEGORY_ORDER", "EXTENDED_CATEGORIES",
    "default_seed", "generate_cases", "negative_cases", "check_negative",
    "run_cases", "run_random", "run_negative", "run_repl_random",
]
