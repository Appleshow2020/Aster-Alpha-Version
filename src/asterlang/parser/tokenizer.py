from __future__ import annotations

import unicodedata
from bisect import bisect_right
from collections.abc import Iterator

from .errors import TokenizerError
from .tokens import EXACT_TOKEN_TYPES, MAX_OPERATOR_LENGTH, Token, TokenType

_BOM = "\ufeff"
_WHITESPACE = " \t\n\f"
_OCTAL = "01234567"
_HEX = "0123456789abcdefABCDEF"
_SIMPLE_ESCAPES = {
    "n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v",
    "\\": "\\", "'": "'", '"': '"',
}

_FORBIDDEN_CHAR_HINTS = {
    "!": "'!' is not used. Use 'not' for logical negation ('!=' is allowed)",
    "|": "'|' cannot be used by itself (only '|>' is allowed)",
    "[": "'[' is not supported (arrays are not available yet)",
    "]": "']' is not supported (arrays are not available yet)",
}
_FORBIDDEN_PAIR_HINTS = {
    "&&": "'&&' is not used. Use 'and' for logical conjunction",
    "||": "'||' is not used. Use 'or' for logical disjunction",
}


class _EscapeError(ValueError):
    pass

def _read_escape(s: str, j: int) -> tuple[str, int]:
    n = len(s)
    k = j + 1
    if k >= n or s[k] == "\n":
        raise _EscapeError("There is no character immediately after '\\'")
    c = s[k]
    if c in _SIMPLE_ESCAPES:
        return _SIMPLE_ESCAPES[c], k + 1
    if c in _OCTAL:
        m = k + 1
        while m < n and m < k + 3 and s[m] in _OCTAL:
            m += 1
        return chr(int(s[k:m], 8)), m
    if c in "xuU":
        width = {"x": 2, "u": 4, "U": 8}[c]
        digits = s[k + 1:k + 1 + width]
        if len(digits) != width or any(d not in _HEX for d in digits):
            raise _EscapeError(f"'\\{c}' must be followed by {width} hexadecimal digits")
        value = int(digits, 16)
        if value > 0x10FFFF:
            raise _EscapeError(f"'\\{c}{digits}' is outside the Unicode range")
        return chr(value), k + 1 + width
    if c == "N":
        if k + 1 >= n or s[k + 1] != "{":
            raise _EscapeError("'\\N' must be followed by '{name}'")
        end = s.find("}", k + 2)
        newline = s.find("\n", k + 2)
        if end == -1 or (newline != -1 and newline < end):
            raise _EscapeError("'\\N{' is not closed by '}'")
        name = s[k + 2:end]
        try:
            return unicodedata.lookup(name), end + 1
        except KeyError:
            raise _EscapeError(f"Unknown Unicode name: {name!r}") from None
    raise _EscapeError(f"Unknown escape sequence: '\\{c}'")


def decode_string(raw: str) -> str:
    if len(raw) < 2 or raw[0] != raw[-1] or raw[0] not in "\"'":
        raise ValueError(f"Not a string literal: {raw!r}")
    body = raw[1:-1]
    out: list[str] = []
    j = 0
    while j < len(body):
        if body[j] == "\\":
            text, j = _read_escape(body, j)
            out.append(text)
        else:
            out.append(body[j])
            j += 1
    return "".join(out)


class Tokenizer:
    def __init__(self, source: str, filename: str = "<string>") -> None:
        self.filename = filename
        self.src = source.replace("\r\n", "\n").replace("\r", "\n")
        self.lines = self.src.split("\n")
        self._line_starts = [0]
        for i, ch in enumerate(self.src):
            if ch == "\n":
                self._line_starts.append(i + 1)

    def position(self, index: int) -> tuple[int, int]:
        line = bisect_right(self._line_starts, index)
        return line, index - self._line_starts[line - 1]

    def _error(self, msg: str, start: int, end: int | None = None) -> TokenizerError:
        line, col = self.position(start)
        text = self.lines[line - 1]
        span = 1 if end is None else max(1, end - start)
        span = min(span, max(1, len(text) - col))
        return TokenizerError(msg, self.filename, line, col, text, col + span)

    def __iter__(self) -> Iterator[Token]:
        src = self.src
        n = len(src)
        i = 0
        while True:
            i = self._skip_trivia(i)
            if i >= n:
                break
            ch = src[i]
            if ch.isidentifier():
                j = self._scan_name(i)
                kind = TokenType.NAME
            elif "0" <= ch <= "9":
                j, kind = self._scan_number(i)
            elif ch in "\"'":
                j = self._scan_string(i)
                kind = TokenType.STRING
            else:
                j = self._scan_operator(i)
                kind = EXACT_TOKEN_TYPES[src[i:j]]
            yield Token(kind, src[i:j], self.position(i), self.position(j))
            i = j
        end = self.position(n)
        yield Token(TokenType.ENDMARKER, "", end, end)

    def _skip_trivia(self, i: int) -> int:
        src, n = self.src, len(self.src)
        while i < n:
            ch = src[i]
            if ch in _WHITESPACE or (ch == _BOM and i == 0):
                i += 1
            elif ch == "#":
                while i < n and src[i] != "\n":
                    i += 1
            elif src.startswith("/*", i):
                end = src.find("*/", i + 2)
                if end == -1:
                    raise self._error("Unterminated block comment", i, i + 2)
                i = end + 2
            else:
                break
        return i

    def _scan_name(self, i: int) -> int:
        src, n = self.src, len(self.src)
        j = i + 1
        while j < n and ("a" + src[j]).isidentifier():
            j += 1
        return j

    def _scan_number(self, i: int) -> tuple[int, TokenType]:
        src, n = self.src, len(self.src)
        j = i
        while j < n and "0" <= src[j] <= "9":
            j += 1
        kind = TokenType.INT
        if j + 1 < n and src[j] == "." and "0" <= src[j + 1] <= "9":
            j += 1
            while j < n and "0" <= src[j] <= "9":
                j += 1
            kind = TokenType.FLOAT
        if j < n and ("a" + src[j]).isidentifier():
            raise self._error("An identifier character cannot immediately follow a number", i, j + 1)
        return j, kind

    def _scan_string(self, i: int) -> int:
        src, n = self.src, len(self.src)
        quote = src[i]
        j = i + 1
        while True:
            if j >= n or src[j] == "\n":
                raise self._error("Unterminated string literal", i, j)
            c = src[j]
            if c == quote:
                return j + 1
            if c == "\\":
                try:
                    _, j = _read_escape(src, j)
                except _EscapeError as e:
                    raise self._error(str(e), j, j + 2) from None
            else:
                j += 1

    def _scan_operator(self, i: int) -> int:
        src = self.src
        pair = src[i:i + 2]
        if pair in _FORBIDDEN_PAIR_HINTS:
            raise self._error(_FORBIDDEN_PAIR_HINTS[pair], i, i + 2)

        for length in range(MAX_OPERATOR_LENGTH, 0, -1):
            text = src[i:i + length]
            if len(text) == length and text in EXACT_TOKEN_TYPES:
                return i + length
        ch = src[i]
        raise self._error(_FORBIDDEN_CHAR_HINTS.get(ch, f"Unknown character: {ch!r}"), i)


def tokenize(source: str, filename: str = "<string>") -> Iterator[Token]:
    return iter(Tokenizer(source, filename))