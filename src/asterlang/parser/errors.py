from __future__ import annotations

class AsterSyntaxError(Exception):

    def __init__(self, msg: str, filename: str, lineno: int, offset: int,
                 text: str = "", end_offset: int | None = None) -> None:
        super().__init__(msg)
        self.msg = msg
        self.filename = filename
        self.lineno = lineno
        self.offset = offset
        self.text = text
        self.end_offset = offset + 1 if end_offset is None else end_offset

    def __str__(self) -> str:
        return f"{self.filename}:{self.lineno}:{self.offset + 1}: {self.msg}"

    def format(self) -> str:
        lines = [str(self)]
        if self.text:
            width = max(1, self.end_offset - self.offset)
            lines.append("    " + self.text)
            lines.append("    " + " " * self.offset + "^" * width)
        return "\n".join(lines)


class TokenizerError(AsterSyntaxError):
    pass