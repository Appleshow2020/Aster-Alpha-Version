from __future__ import annotations

from enum import IntEnum, auto
from typing import NamedTuple

class TokenType(IntEnum):
    ENDMARKER = auto()
    NAME = auto()
    INT = auto()
    FLOAT = auto()
    STRING = auto()

    LPAR = auto()
    RPAR = auto()
    LBRACE = auto()
    RBRACE = auto()
    SEMI = auto()
    COMMA = auto()
    DOT = auto()
    COLON = auto()
    AT = auto()

    ARROW = auto()
    FATARROW = auto()

    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    DOUBLESLASH = auto()
    PERCENT = auto()
    DOUBLESTAR = auto()

    EQEQUAL = auto()
    NOTEQUAL = auto()
    LESS = auto()
    GREATER = auto()
    LESSEQUAL = auto()
    GREATEREQUAL = auto()

    EQUAL = auto()
    PLUSEQUAL = auto()
    MINEQUAL = auto()
    STAREQUAL = auto()
    SLASHEQUAL = auto()
    DOUBLESLASHEQUAL = auto()
    PERCENTEQUAL = auto()
    DOUBLESTAREQUAL = auto()

    DOTDOT = auto()
    PIPE = auto()
    DOUBLEQUESTION = auto()
    QUESTION = auto()
    AMPER = auto()

EXACT_TOKEN_TYPES: dict[str, TokenType] = {
    "(": TokenType.LPAR,
    ")": TokenType.RPAR,
    "{": TokenType.LBRACE,
    "}": TokenType.RBRACE,
    ";": TokenType.SEMI,
    ",": TokenType.COMMA,
    ".": TokenType.DOT,
    ":": TokenType.COLON,
    "@": TokenType.AT,
    "->": TokenType.ARROW,
    "=>": TokenType.FATARROW,
    "+": TokenType.PLUS,
    "-": TokenType.MINUS,
    "*": TokenType.STAR,
    "/": TokenType.SLASH,
    "//": TokenType.DOUBLESLASH,
    "%": TokenType.PERCENT,
    "**": TokenType.DOUBLESTAR,
    "==": TokenType.EQEQUAL,
    "!=": TokenType.NOTEQUAL,
    "<": TokenType.LESS,
    ">": TokenType.GREATER,
    "<=": TokenType.LESSEQUAL,
    ">=": TokenType.GREATEREQUAL,
    "=": TokenType.EQUAL,
    "+=": TokenType.PLUSEQUAL,
    "-=": TokenType.MINEQUAL,
    "*=": TokenType.STAREQUAL,
    "/=": TokenType.SLASHEQUAL,
    "//=": TokenType.DOUBLESLASHEQUAL,
    "%=": TokenType.PERCENTEQUAL,
    "**=": TokenType.DOUBLESTAREQUAL,
    "..": TokenType.DOTDOT,
    "|>": TokenType.PIPE,
    "??": TokenType.DOUBLEQUESTION,
    "?": TokenType.QUESTION,
    "&": TokenType.AMPER,
}

MAX_OPERATOR_LENGTH = max(len(op) for op in EXACT_TOKEN_TYPES)

class Token(NamedTuple):
    type: TokenType
    string: str
    start: tuple[int, int]
    end: tuple[int, int]
