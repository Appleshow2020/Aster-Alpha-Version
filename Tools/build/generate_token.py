#!/usr/bin/env python3
"""Grammar/Tokens에서 Include/internal/token.h 와 Parser/token.c 를 만든다 (CPython의 generate_token.py와 같은 역할).

사용: python3 Tools/build/generate_token.py Grammar/Tokens Include/internal/token.h Parser/token.c
"""
import re
import sys
from pathlib import Path


def read_tokens(path):
    names, exact = [], []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"([A-Z_]+)(?:\s+'([^']+)')?", line)
        if not m:
            raise SystemExit(f"Tokens: 읽을 수 없는 줄: {line!r}")
        names.append(m.group(1))
        if m.group(2):
            exact.append((m.group(2), m.group(1)))
    return names, exact


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(tokens, header, source):
    names, exact = read_tokens(tokens)
    exact.sort(key=lambda t: (-len(t[0]), t[0]))      # 가장 긴 연산자부터 일치시킨다
    h = ["/* 자동 생성: Tools/build/generate_token.py (원본: Grammar/Tokens). 직접 고치지 마세요. */",
         "#ifndef ASTER_TOKEN_H", "#define ASTER_TOKEN_H", "",
         "enum TokenType {"]
    h += [f"    TOK_{n}," for n in names]
    h += ["    TOK_N_TOKENS", "};", "",
          "typedef struct { const char *text; int type; } ExactToken;", "",
          "extern const char *const _AsterToken_Names[];",
          "extern const ExactToken _AsterToken_Exact[];",
          "extern const int _AsterToken_ExactCount;",
          f"#define ASTER_MAX_OPERATOR_LENGTH {max(len(t) for t, _ in exact)}", "",
          "#endif"]
    c = ["/* 자동 생성: Tools/build/generate_token.py (원본: Grammar/Tokens). 직접 고치지 마세요. */",
         '#include "internal/token.h"', "", "const char *const _AsterToken_Names[] = {"]
    c += [f"    {c_string(n)}," for n in names]
    c += ["};", "", "const ExactToken _AsterToken_Exact[] = {"]
    c += [f"    {{{c_string(t)}, TOK_{n}}}," for t, n in exact]
    c += ["};", f"const int _AsterToken_ExactCount = {len(exact)};"]
    Path(header).write_text("\n".join(h) + "\n", encoding="utf-8")
    Path(source).write_text("\n".join(c) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])