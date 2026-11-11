"""문법 이탈 검사. 사용: python Tools/check_conformance.py

  * Grammar/Aster.asdl      <-> syntax/nodes.py       (노드 클래스, 필드, 위치 속성)
  * Grammar/aster.gram      <-> parser/parser.py      (규칙 이름, 하드/소프트 키워드)
  * tests/grammar/valid     통과해야 하는 예제,  tests/grammar/invalid  거부되어야 하는 예제
(토큰 대응은 tests/test_tokenizer.py가 검사한다.)
"""
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASDL_FILE = ROOT / "Grammar" / "Aster.asdl"
GRAM_FILE = ROOT / "Grammar" / "aster.gram"
EXAMPLES = ROOT / "tests" / "grammar"
BUILTIN_TYPES = {"identifier", "int", "constant", "string", "bytes", "object"}


# ------------------------------------------------------------ ASDL 읽기

@dataclass
class Field:
    type: str
    name: str
    seq: bool = False      # type*
    opt: bool = False      # type?


@dataclass
class Constructor:
    name: str
    fields: list[Field]


@dataclass
class Sum:
    name: str
    constructors: list[Constructor]
    attributes: list[Field]


@dataclass
class Product:
    name: str
    fields: list[Field]
    attributes: list[Field]


_ASDL_TOKEN = re.compile(r"\s+|--[^\n]*|([A-Za-z_]\w*|[=|(){}*?,])")


def _asdl_tokens(text: str) -> list[str]:
    tokens, pos = [], 0
    while pos < len(text):
        m = _ASDL_TOKEN.match(text, pos)
        if not m:
            raise ValueError(f"ASDL 어휘 오류: {text[pos:pos + 20]!r}")
        if m.group(1):
            tokens.append(m.group(1))
        pos = m.end()
    return tokens


class _AsdlParser:
    def __init__(self, tokens: list[str]) -> None:
        self.tokens = tokens
        self.i = 0

    def peek(self) -> str | None:
        return self.tokens[self.i] if self.i < len(self.tokens) else None

    def next(self) -> str:
        tok = self.peek()
        if tok is None:
            raise ValueError("ASDL이 예상보다 일찍 끝났습니다")
        self.i += 1
        return tok

    def expect(self, tok: str) -> None:
        got = self.next()
        if got != tok:
            raise ValueError(f"ASDL: {tok!r}가 필요한데 {got!r}가 나왔습니다")

    def module(self) -> tuple[str, dict[str, Sum | Product]]:
        self.expect("module")
        name = self.next()
        self.expect("{")
        dfns: dict[str, Sum | Product] = {}
        while self.peek() != "}":
            dname = self.next()
            if dname in dfns:
                raise ValueError(f"ASDL: {dname!r}가 두 번 정의되었습니다")
            self.expect("=")
            dfns[dname] = self.definition(dname)
        self.expect("}")
        return name, dfns

    def definition(self, name: str) -> Sum | Product:
        if self.peek() == "(":
            return Product(name, self.fields(), self.attributes())
        constructors = [self.constructor()]
        while self.peek() == "|":
            self.next()
            constructors.append(self.constructor())
        return Sum(name, constructors, self.attributes())

    def constructor(self) -> Constructor:
        name = self.next()
        return Constructor(name, self.fields() if self.peek() == "(" else [])

    def attributes(self) -> list[Field]:
        if self.peek() == "attributes":
            self.next()
            return self.fields()
        return []

    def fields(self) -> list[Field]:
        self.expect("(")
        out = [self.field()]
        while self.peek() == ",":
            self.next()
            out.append(self.field())
        self.expect(")")
        return out

    def field(self) -> Field:
        type_ = self.next()
        seq = opt = False
        if self.peek() == "*":
            self.next()
            seq = True
        elif self.peek() == "?":
            self.next()
            opt = True
        return Field(type_, self.next(), seq, opt)


def parse_asdl(text: str) -> tuple[str, dict[str, Sum | Product]]:
    """ASDL 텍스트를 (모듈 이름, {정의 이름: Sum | Product})로 읽는다."""
    return _AsdlParser(_asdl_tokens(text)).module()


# ------------------------------------------------------ ASDL <-> nodes.py

def _all_subclasses(cls: type) -> set[type]:
    found: set[type] = set()
    for sub in cls.__subclasses__():
        found.add(sub)
        found |= _all_subclasses(sub)
    return found


def check_asdl() -> list[str]:
    """문제 목록을 돌려준다. 비어 있으면 일치한다."""
    from dev.src.asterlang.syntax import nodes

    _, dfns = parse_asdl(ASDL_FILE.read_text(encoding="utf-8"))
    problems: list[str] = []

    # 1. ASDL 자체의 일관성: 필드 타입이 정의되어 있고, 이름이 겹치지 않는다.
    seen: dict[str, str] = {}
    for dname, d in dfns.items():
        names = [(dname, "타입")]
        if isinstance(d, Sum):
            names += [(c.name, "생성자") for c in d.constructors]
        for n, kind in names:
            if n in seen:
                problems.append(f"ASDL: 이름 {n!r}가 중복됩니다 ({seen[n]}, {kind})")
            seen[n] = kind
        all_fields = (d.fields if isinstance(d, Product) else
                      [f for c in d.constructors for f in c.fields]) + d.attributes
        for f in all_fields:
            if f.type not in BUILTIN_TYPES and f.type not in dfns:
                problems.append(f"ASDL: {dname}.{f.name}의 타입 {f.type!r}가 정의되어 있지 않습니다")

    # 2. 기대하는 클래스: 이름 -> (기반 클래스 이름, 필드, 위치 속성)
    expected: dict[str, tuple[str, list[str], list[str]]] = {}
    for dname, d in dfns.items():
        attrs = [a.name for a in d.attributes]
        if isinstance(d, Sum):
            expected[dname] = ("AST", [], attrs)
            for c in d.constructors:
                expected[c.name] = (dname, [f.name for f in c.fields], attrs)
        else:
            expected[dname] = ("AST", [f.name for f in d.fields], attrs)

    # 3. nodes.py와 비교
    for cname, (base, fields, attrs) in expected.items():
        cls = getattr(nodes, cname, None)
        if cls is None:
            problems.append(f"nodes.py에 {cname}가 없습니다")
            continue
        if cls.__bases__ != (getattr(nodes, base),):
            problems.append(f"{cname}의 기반 클래스가 {base}가 아닙니다")
        if cls._fields != tuple(fields):
            problems.append(f"{cname}._fields {cls._fields} != ASDL {tuple(fields)}")
        if cls._attributes != tuple(attrs):
            problems.append(f"{cname}._attributes {cls._attributes} != ASDL {tuple(attrs)}")

    # 4. nodes.py에만 있는 클래스는 문법 이탈이다.
    for cls in _all_subclasses(nodes.AST):
        if cls.__name__ not in expected:
            problems.append(f"nodes.py의 {cls.__name__}가 ASDL에 없습니다")
    return sorted(problems)

# ------------------------------------------------ aster.gram <-> parser.py

def gram_rules(text: str) -> list[str]:
    """줄 맨 앞에서 시작하는 'name:' 이 규칙이다 (주석은 '#'으로 시작하므로 걸리지 않는다)."""
    return re.findall(r"^([a-z_][a-z0-9_]*):", text, re.MULTILINE)


def gram_keywords(text: str) -> tuple[set[str], set[str]]:
    """머리말의 '# 하드 키워드:' 목록과 '# 소프트 키워드:' 줄을 읽는다."""
    hard: set[str] = set()
    soft: set[str] = set()
    in_hard = False
    for line in text.splitlines():
        if line.startswith("# 하드 키워드:"):
            in_hard = True
        elif line.startswith("# 소프트 키워드:"):
            in_hard = False
            soft = set(line.split(":", 1)[1].split())
        elif in_hard and line.startswith("#   "):
            hard |= set(line[1:].split())
        elif in_hard:
            in_hard = False
    return hard, soft


def check_rules() -> list[str]:
    """문제 목록을 돌려준다. 비어 있으면 일치한다.

    문제: 문법에 없는 규칙을 구현했거나, 문법의 규칙을 구현하지 않았거나,
    키워드 목록이 다르거나, 문법에 같은 규칙이 두 번 있다.
    """
    from dev.src.asterlang.parser import parser

    text = GRAM_FILE.read_text(encoding="utf-8")
    rules = gram_rules(text)
    problems: list[str] = []
    for name in sorted({r for r in rules if rules.count(r) > 1}):
        problems.append(f"aster.gram에 규칙 {name!r}가 두 번 정의되어 있습니다")
    for name in sorted(parser.RULE_NAMES - set(rules)):
        problems.append(f"parser.py의 규칙 {name!r}가 aster.gram에 없습니다")
    hard, soft = gram_keywords(text)
    if hard != set(parser.HARD_KEYWORDS):
        problems.append("하드 키워드가 다릅니다: 문법에만 "
                        f"{sorted(hard - parser.HARD_KEYWORDS)}, 파서에만 {sorted(parser.HARD_KEYWORDS - hard)}")
    if soft != set(parser.SOFT_KEYWORDS):
        problems.append("소프트 키워드가 다릅니다: 문법에만 "
                        f"{sorted(soft - parser.SOFT_KEYWORDS)}, 파서에만 {sorted(parser.SOFT_KEYWORDS - soft)}")
    pending = sorted(set(rules) - parser.RULE_NAMES)
    if pending:
        problems.append(f"구현되지 않은 문법 규칙 {len(pending)}개: {', '.join(pending)}")
    return problems


# ------------------------------------------------ valid / invalid 예제

def split_cases(text: str) -> list[str]:
    """'#---' 줄로 예제를 나눈다. ('#'은 주석이므로 합친 파일도 올바른 Aster 소스다.)"""
    cases, current = [], []
    for line in text.splitlines():
        if line.startswith("#---"):
            cases.append("\n".join(current))
            current = []
        else:
            current.append(line)
    cases.append("\n".join(current))
    return [c for c in cases if c.strip()]


def _first_code_line(case: str) -> str:
    for line in case.splitlines():
        if line.strip() and not line.lstrip().startswith("#"):
            return line.strip()[:50]
    return ""


def check_examples() -> list[str]:
    """valid 예제는 통과하고, invalid 예제는 AsterSyntaxError로 거부되어야 한다."""
    from dev.src.asterlang.parser.errors import AsterSyntaxError
    from dev.src.asterlang.parser.parser import parse

    problems: list[str] = []
    for kind in ("valid", "invalid"):
        files = sorted((EXAMPLES / kind).glob("*.ast"))
        if not files:
            problems.append(f"tests/grammar/{kind}에 예제 파일이 없습니다")
        for path in files:
            for number, case in enumerate(split_cases(path.read_text(encoding="utf-8")), 1):
                where = f"{kind}/{path.name} #{number} ({_first_code_line(case)!r})"
                if not _first_code_line(case):
                    problems.append(f"{where}: 코드가 없는 예제입니다")
                    continue
                try:
                    parse(case, path.name)
                except AsterSyntaxError as e:
                    if kind == "valid":
                        problems.append(f"{where}: 통과해야 하는데 거부되었습니다: "
                                        f"{e.msg} ({e.lineno}줄 {e.offset + 1}열)")
                else:
                    if kind == "invalid":
                        problems.append(f"{where}: 거부되어야 하는데 통과했습니다")
    return problems


def main() -> int:
    problems = check_asdl() + check_rules() + check_examples()
    for p in problems:
        print("불일치:", p)
    print("OK: ASDL/nodes.py, aster.gram/parser.py, 예제 파일이 모두 일치합니다" if not problems
          else f"{len(problems)}개 문제")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())