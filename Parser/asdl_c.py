#!/usr/bin/env python3
"""Aster.asdl에서 AST 노드의 C 코드를 만든다 (CPython의 Parser/asdl_c.py와 같은 역할).

만드는 파일
  Include/internal/ast.h    노드 구조체, 생성자 선언, 덤프 선언
  Python/Aster-ast.c        생성자(_Aster_Name 등)와 AST 덤프(Aster_AST_Dump)

규칙
  * sum 타입은 '종류(kind)' + union 구조체이고, 생성자마다 _Aster_<이름>() 함수가 있다.
  * 필드가 없는 sum(boolop, operator, access ...)은 값이 1부터 시작하는 enum이다. 0은 '없음'(선택 필드)이다.
  * 'T*'는 asdl_seq*, 'T?'는 NULL(또는 enum 0)을 허용한다. 필수 필드가 NULL이면 파서의 버그이므로 즉시 중단한다.
사용: python3 Parser/asdl_c.py Parser/Aster.asdl -H Include/internal/ast.h -C Python/Aster-ast.c
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from asdl import Product, Sum, parse_asdl  # noqa: E402

POS = ["lineno", "col_offset", "end_lineno", "end_col_offset"]


class Gen:
    def __init__(self, dfns):
        self.dfns = dfns

    # ---- 타입 도우미
    def is_simple(self, tname):
        d = self.dfns.get(tname)
        return isinstance(d, Sum) and d.simple

    def ctype(self, f):
        if f.seq:
            return "asdl_seq *"
        if f.type == "identifier":
            return "identifier"
        if f.type == "int":
            return "int"
        if f.type == "constant":
            return "Value"
        return f"{f.type}_ty"

    def is_pointer(self, f):
        return f.seq or f.type == "identifier" or (f.type in self.dfns and not self.is_simple(f.type))

    def needs_check(self, f):
        if f.opt or f.type in ("int", "constant"):
            return False
        return self.is_pointer(f) or self.is_simple(f.type)

    def decl(self, f):
        t = self.ctype(f)
        return f"{t}{'' if t.endswith('*') else ' '}{f.name}"

    # ---- 헤더
    def header(self):
        o = ["/* 자동 생성: Parser/asdl_c.py (원본: Parser/Aster.asdl). 직접 고치지 마세요. */",
             "#ifndef ASTER_AST_H", "#define ASTER_AST_H",
             '#include <stdio.h>', '#include "internal/arena.h"', '#include "internal/asdl.h"',
             '#include "internal/value.h"', ""]
        for name, d in self.dfns.items():
            if not d.simple:
                o.append(f"typedef struct _{name} *{name}_ty;")
        o.append("")
        for name, d in self.dfns.items():
            if d.simple:
                o.append(f"typedef enum _{name} {{ " +
                         ", ".join(f"{c.name} = {i}" for i, c in enumerate(d.constructors, 1)) +
                         f" }} {name}_ty;")
        o.append("")
        for name, d in self.dfns.items():
            if d.simple:
                continue
            if isinstance(d, Product):
                o.append(f"struct _{name} {{")
                o += [f"    {self.decl(f)};" for f in d.fields]
                o += [f"    int {a.name};" for a in d.attributes]
                o.append("};\n")
                continue
            o.append(f"enum _{name}_kind {{ " +
                     ", ".join(f"{c.name}_kind = {i}" for i, c in enumerate(d.constructors, 1)) + " };")
            o += [f"struct _{name} {{", f"    enum _{name}_kind kind;"]
            if any(c.fields for c in d.constructors):
                o.append("    union {")
                for c in d.constructors:
                    if c.fields:
                        o.append("        struct { " + " ".join(f"{self.decl(f)};" for f in c.fields) + f" }} {c.name};")
                o.append("    } v;")
            o += [f"    int {a.name};" for a in d.attributes]
            o.append("};\n")
        o.append("/* 생성자 */")
        for name, d in self.dfns.items():
            if d.simple:
                continue
            if isinstance(d, Product):
                o.append(self.proto(name, f"{name}_ty", d.fields, []) + ";")
            else:
                for c in d.constructors:
                    o.append(self.proto(c.name, f"{name}_ty", c.fields, d.attributes) + ";")
        o += ["", "/* AST를 파이썬 ast.dump와 같은 모양으로 출력한다 (테스트용) */",
              "void Aster_AST_Dump(FILE *f, mod_ty mod);", "", "#endif"]
        return "\n".join(o) + "\n"

    def proto(self, cname, rtype, fields, attrs):
        params = [self.decl(f) for f in fields] + [f"int {a.name}" for a in attrs] + ["Arena *arena"]
        return f"{rtype} _Aster_{cname}({', '.join(params)})"

    # ---- 소스
    def source(self):
        o = ["/* 자동 생성: Parser/asdl_c.py (원본: Parser/Aster.asdl). 직접 고치지 마세요. */",
             "#include <stdio.h>", "#include <stdlib.h>", '#include "internal/ast.h"',
             '#include "internal/object.h"', "",
             "static void missing(const char *field, const char *node) {",
             '    fprintf(stderr, "내부 오류: %s의 필수 필드 \'%s\'가 비어 있습니다\\n", node, field);',
             "    abort();", "}", ""]
        for name, d in self.dfns.items():
            if d.simple:
                continue
            if isinstance(d, Product):
                o += self.constructor(name, f"{name}_ty", name, d.fields, [], None)
            else:
                for c in d.constructors:
                    o += self.constructor(c.name, f"{name}_ty", name, c.fields, d.attributes, c.name + "_kind")
        o += self.dump_source()
        return "\n".join(o) + "\n"

    def constructor(self, cname, rtype, sname, fields, attrs, kind):
        o = [self.proto(cname, rtype, fields, attrs) + " {", f"    {rtype} p;"]
        for f in fields:
            if self.needs_check(f):
                o.append(f'    if (!{f.name}) missing("{f.name}", "{cname}");')
        o.append("    p = Arena_Alloc(arena, sizeof(*p));")
        if kind:
            o.append(f"    p->kind = {kind};")
        for f in fields:
            target = f"p->v.{cname}.{f.name}" if kind and fields else f"p->{f.name}"
            o.append(f"    {target} = {f.name};")
        o += [f"    p->{a.name} = {a.name};" for a in attrs]
        o += ["    return p;", "}", ""]
        return o

    # ---- 덤프
    def dump_fn(self, f):
        if f.seq:
            return f"dump_seq(out, n->{{}}, dumpv_{f.type})"
        return None

    def field_dump(self, f, expr):
        if f.seq:
            return f"dump_seq(out, {expr}, dumpv_{f.type});"
        return f"dump_{f.type}(out, {expr});"

    def dump_source(self):
        o = ["", "/* ---- 덤프 ---- */",
             "static void dump_identifier(FILE *out, identifier s) { if (!s) fputs(\"None\", out); else fprintf(out, \"'%s'\", s); }",
             "static void dump_int(FILE *out, int i) { fprintf(out, \"%d\", i); }",
             "static void dump_constant(FILE *out, Value v) { Aster_Value_Repr(out, v); }",
             "static void dumpv_identifier(FILE *out, void *p) { dump_identifier(out, (identifier)p); }",
             "static void dump_seq(FILE *out, asdl_seq *s, void (*fn)(FILE *, void *)) {",
             "    size_t i;",
             "    fputc('[', out);",
             "    for (i = 0; i < asdl_seq_LEN(s); i++) { if (i) fputs(\", \", out); fn(out, asdl_seq_GET(s, i)); }",
             "    fputc(']', out);",
             "}", ""]
        for name in self.dfns:
            t = f"{name}_ty"
            o.append(f"static void dump_{name}(FILE *out, {t} n);")
            o.append(f"static inline void dumpv_{name}(FILE *out, void *p) {{ dump_{name}(out, ({t})p); }}")
        o.append("")
        for name, d in self.dfns.items():
            t = f"{name}_ty"
            o.append(f"static void dump_{name}(FILE *out, {t} n) {{")
            if d.simple:
                o.append('    if (!n) { fputs("None", out); return; }')
                o.append("    switch (n) {")
                for c in d.constructors:
                    o.append(f'    case {c.name}: fputs("{c.name}()", out); break;')
                o += ["    }", "}", ""]
                continue
            o.append('    if (!n) { fputs("None", out); return; }')
            if isinstance(d, Product):
                o += self.dump_fields(name, d.fields, "n->")
                o += ["}", ""]
                continue
            o.append("    switch (n->kind) {")
            for c in d.constructors:
                o.append(f"    case {c.name}_kind:")
                o += ["    " + line for line in self.dump_fields(c.name, c.fields, f"n->v.{c.name}.")]
                o.append("        break;")
            o += ["    }", "}", ""]
        o += ["void Aster_AST_Dump(FILE *f, mod_ty mod) {", "    dump_mod(f, mod);", "    fputc('\\n', f);", "}"]
        return o

    def dump_fields(self, cname, fields, prefix):
        if not fields:
            return [f'    fputs("{cname}()", out);']
        o = [f'    fputs("{cname}(", out);']
        for i, f in enumerate(fields):
            o.append(f'    fputs("{", " if i else ""}{f.name}=", out);')
            o.append("    " + self.field_dump(f, prefix + f.name))
        o.append('    fputc(\')\', out);')
        return o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("asdl")
    ap.add_argument("-H", dest="header", required=True)
    ap.add_argument("-C", dest="source", required=True)
    a = ap.parse_args()
    _, dfns = parse_asdl(Path(a.asdl).read_text(encoding="utf-8"))
    g = Gen(dfns)
    Path(a.header).write_text(g.header(), encoding="utf-8")
    Path(a.source).write_text(g.source(), encoding="utf-8")


if __name__ == "__main__":
    main()
