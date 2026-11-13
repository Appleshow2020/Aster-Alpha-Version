"""ASDL(Abstract Syntax Definition Language) 읽기. CPython의 Parser/asdl.py와 같은 역할이다.

지원하는 문법:  module NAME { NAME = (필드들) | 생성자 | 생성자(필드들) ... [attributes (필드들)] }
필드: 타입[*|?] 이름
"""
import re
from dataclasses import dataclass


@dataclass
class Field:
    type: str
    name: str
    seq: bool = False      # type*
    opt: bool = False      # type?


@dataclass
class Constructor:
    name: str
    fields: list


@dataclass
class Sum:
    name: str
    constructors: list
    attributes: list

    @property
    def simple(self):      # 모든 생성자에 필드가 없고 위치 속성도 없는 sum은 C의 enum이 된다
        return not self.attributes and all(not c.fields for c in self.constructors)


@dataclass
class Product:
    name: str
    fields: list
    attributes: list

    simple = False


BUILTIN_TYPES = {"identifier", "int", "constant", "string", "bytes", "object"}
_TOKEN = re.compile(r"\s+|--[^\n]*|([A-Za-z_]\w*|[=|(){}*?,])")


def _tokens(text):
    out, pos = [], 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m:
            raise ValueError(f"ASDL lexical error: {text[pos:pos + 20]!r}")
        if m.group(1):
            out.append(m.group(1))
        pos = m.end()
    return out


class _Parser:
    def __init__(self, tokens):
        self.t, self.i = tokens, 0

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def next(self):
        tok = self.peek()
        if tok is None:
            raise ValueError("ASDL ended unexpectedly")
        self.i += 1
        return tok

    def expect(self, tok):
        got = self.next()
        if got != tok:
            raise ValueError(f"ASDL: expected {tok!r} but got {got!r}")

    def module(self):
        self.expect("module")
        name = self.next()
        self.expect("{")
        dfns = {}
        while self.peek() != "}":
            dname = self.next()
            if dname in dfns:
                raise ValueError(f"ASDL: {dname!r} is defined twice")
            self.expect("=")
            dfns[dname] = self.definition(dname)
        self.expect("}")
        return name, dfns

    def definition(self, name):
        if self.peek() == "(":
            return Product(name, self.fields(), self.attributes())
        cons = [self.constructor()]
        while self.peek() == "|":
            self.next()
            cons.append(self.constructor())
        return Sum(name, cons, self.attributes())

    def constructor(self):
        name = self.next()
        return Constructor(name, self.fields() if self.peek() == "(" else [])

    def attributes(self):
        if self.peek() == "attributes":
            self.next()
            return self.fields()
        return []

    def fields(self):
        self.expect("(")
        out = [self.field()]
        while self.peek() == ",":
            self.next()
            out.append(self.field())
        self.expect(")")
        return out

    def field(self):
        typ = self.next()
        seq = opt = False
        if self.peek() == "*":
            self.next()
            seq = True
        elif self.peek() == "?":
            self.next()
            opt = True
        return Field(typ, self.next(), seq, opt)


def parse_asdl(text):
    """(모듈 이름, {정의 이름: Sum | Product})"""
    name, dfns = _Parser(_tokens(text)).module()
    for d in dfns.values():
        fields = d.fields if isinstance(d, Product) else [f for c in d.constructors for f in c.fields]
        for f in fields + d.attributes:
            if f.type not in BUILTIN_TYPES and f.type not in dfns:
                raise ValueError(f"ASDL: {d.name}.{f.name} has undefined type {f.type!r}")
    return name, dfns