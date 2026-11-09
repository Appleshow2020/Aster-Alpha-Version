"""토큰 스트림 -> AST.

Grammar/aster.gram의 규칙을 재귀 하강 + 메모이제이션(packrat)으로 구현한다.
  * @rule이 붙은 메서드 하나가 aster.gram의 규칙 하나이고, 이름이 같다.
    (Tools/check_conformance.py가 두 목록을 비교한다. 문법에 없는 규칙은 만들 수 없다.)
  * 규칙은 성공하면 None이 아닌 값(노드, 리스트, 토큰 수 ...)을, 실패하면 None을 돌려준다.
    실패하면 위치(self.pos)는 규칙을 시작하기 전으로 되돌아간다 (PEG의 되돌아가기).
  * 키워드는 NAME 토큰이다. 이름이 필요한 곳은 name()이, 키워드가 필요한 곳은 accept()가 가린다.
  * 오류 위치는 CPython처럼 '가장 멀리까지 읽은 곳'에 보고한다.
aster.gram의 모든 규칙을 구현한다 (타입, 식, 문장, 함수, 클래스, 인터페이스, 구조체, 제어문).
"""
from __future__ import annotations

import functools
import sys
from collections.abc import Callable, Iterable
from typing import Any

from ..syntax import nodes as ast
from .errors import AsterSyntaxError
from .tokenizer import Tokenizer, decode_string
from .tokens import Token, TokenType

# Grammar/aster.gram 머리말의 목록과 같아야 한다 (check_conformance가 검사).
HARD_KEYWORDS = frozenset("""
    package import from as fn class extends implements interface new self
    public protected private internal
    if else while for break continue return throw try catch finally
    typedef struct and or not is in true false sizeof
    int float bool str null void
""".split())
SOFT_KEYWORDS = frozenset("constructor getter setter property".split())

RULE_NAMES: set[str] = set()      # @rule이 붙은 메서드 이름 (= 구현된 문법 규칙)

_PRIMITIVES = {"int": ast.IntT, "float": ast.FloatT, "bool": ast.BoolT,
               "str": ast.StrT, "null": ast.NullT}
_CMP_OPS = (("==", ast.Eq), ("!=", ast.NotEq), ("<=", ast.LtE),
            (">=", ast.GtE), ("<", ast.Lt), (">", ast.Gt))
_ACCESS = (("public", ast.Public), ("protected", ast.Protected),
           ("private", ast.Private), ("internal", ast.Internal))
_ASSIGN_OPS = (("=", ast.AssignEq), ("+=", ast.AssignAdd), ("-=", ast.AssignSub),
               ("*=", ast.AssignMult), ("/=", ast.AssignDiv), ("//=", ast.AssignFloorDiv),
               ("%=", ast.AssignMod), ("**=", ast.AssignPow))
_ADD_OPS = {"+": ast.Add, "-": ast.Sub}
_MUL_OPS = {"*": ast.Mult, "/": ast.Div, "//": ast.FloorDiv, "%": ast.Mod}

_MAX_RECURSION = 20000            # 깊게 중첩된 식을 위해 잠시 올리는 파이썬 재귀 한도
_MAX_EXPECTED = 8                 # 오류 메시지에 보여 줄 '필요한 토큰'의 최대 개수
# 오류 메시지에서 앞에 보여 줄 토큰. 식이 끝난 자리에서는 연산자 수십 개가 가능하지만
# 사용자에게 가장 도움이 되는 힌트는 닫는 기호와 구분자다.
_PRIORITY_EXPECTED = ("')'", "'}'", "';'", "','")


def rule(fn: Callable[[Parser], Any]) -> Callable[[Parser], Any]:
    """문법 규칙 메서드. 결과를 (규칙, 위치, no_arrow)별로 기억한다 (packrat)."""
    name = fn.__name__
    RULE_NAMES.add(name)

    @functools.wraps(fn)
    def wrapper(self: Parser) -> Any:
        key = (name, self.pos, self.no_arrow)
        hit = self.memo.get(key)
        if hit is not None:
            result, end = hit
            self.pos = end
            return result
        start = self.pos
        result = fn(self)
        if result is None:
            self.pos = start
        self.memo[key] = (result, self.pos)
        return result

    return wrapper


class Parser:
    def __init__(self, source: str, filename: str = "<string>",
                 type_names: Iterable[str] = ()) -> None:
        tokenizer = Tokenizer(source, filename)
        self.filename = filename
        self.lines = tokenizer.lines
        self.tokens: list[Token] = list(tokenizer)
        self.pos = 0
        self.memo: dict[tuple[str, int, bool], tuple[Any, int]] = {}
        self.no_arrow = False                       # [D7] 람다 본문의 최상위에서 True
        self._typedef_alias: dict[int, str] = {}    # typedef struct의 태그 토큰 위치 -> 별칭
        self.type_names = set(type_names) | self._prescan()
        self.type_param_stack: list[set[str]] = []  # 제네릭 매개변수 이름 (T 등)
        self._struct_names: list[set[str]] = []     # 지금 읽는 구조체의 태그/별칭 (생성자 이름 검사용)
        self._farthest = 0
        self._expected: list[str] = []

    # ------------------------------------------------------------ 토큰 읽기

    @property
    def tok(self) -> Token:
        return self.tokens[self.pos]

    def _expect(self, what: str) -> None:
        """현재 위치에서 what을 기대했으나 실패했음을 기록한다 (오류 메시지용)."""
        if self.pos > self._farthest:
            self._farthest, self._expected = self.pos, [what]
        elif self.pos == self._farthest and what not in self._expected:
            self._expected.append(what)

    def accept(self, text: str) -> bool:
        """현재 토큰이 text(연산자 또는 키워드)이면 읽고 True를 돌려준다."""
        t = self.tokens[self.pos]
        if t.string == text and t.type is not TokenType.STRING:
            self.pos += 1
            return True
        self._expect(f"'{text}'")
        return False

    def at(self, text: str) -> bool:
        """읽지 않고 현재 토큰이 text인지만 본다."""
        t = self.tokens[self.pos]
        return t.string == text and t.type is not TokenType.STRING

    def name(self) -> str | None:
        """키워드가 아닌 NAME을 읽는다. (소프트 키워드는 이름으로 쓸 수 있다.)"""
        t = self.tokens[self.pos]
        if t.type is TokenType.NAME and t.string not in HARD_KEYWORDS:
            self.pos += 1
            return t.string
        self._expect("이름")
        return None

    def is_declared_type_name(self, name: str) -> bool:
        return name in self.type_names or any(name in s for s in self.type_param_stack)

    def _prescan(self) -> set[str]:
        """파일을 먼저 훑어 클래스/인터페이스/구조체/import 이름을 모은다.

        (타입)값 캐스트와 '(x) + 1'을 구분하려면 선언된 타입 이름을 알아야 하는데,
        선언이 사용보다 뒤에 있을 수도 있기 때문이다.
        typedef struct의 별칭은 닫는 '}' 뒤에 나오는데, 구조체 생성자의 이름 검사에 미리 필요해서
        여기서 태그 토큰의 위치와 함께 기록해 둔다.
        """
        toks, n = self.tokens, len(self.tokens)
        names: set[str] = set()
        for i, t in enumerate(toks):
            if t.type is not TokenType.NAME:
                continue
            nxt = toks[i + 1] if i + 1 < n else None
            if t.string in ("class", "interface") and nxt and nxt.type is TokenType.NAME:
                names.add(nxt.string)
            elif (t.string == "struct" and nxt and nxt.type is TokenType.NAME
                  and i + 2 < n and toks[i + 2].string == "{"):
                names.add(nxt.string)
                if i > 0 and toks[i - 1].string == "typedef":   # 닫는 '}' 뒤의 별칭
                    depth, j = 0, i + 2
                    while j < n:
                        depth += toks[j].string == "{"
                        depth -= toks[j].string == "}"
                        if depth == 0:
                            break
                        j += 1
                    if j + 1 < n and toks[j + 1].type is TokenType.NAME:
                        names.add(toks[j + 1].string)
                        self._typedef_alias[i + 1] = toks[j + 1].string
            elif t.string == "import":                          # 마지막 이름 (와일드카드 제외)
                j = i + 1
                while j < n and toks[j].string != ";" and toks[j].type is not TokenType.ENDMARKER:
                    j += 1
                if j - 1 > i and toks[j - 1].type is TokenType.NAME:
                    names.add(toks[j - 1].string)
        return names

    # ------------------------------------------------------- 노드와 오류

    def make(self, cls: type, start: int, *fields: Any) -> Any:
        """토큰 start부터 방금 읽은 토큰까지의 위치를 붙여 노드를 만든다."""
        if cls._attributes:
            first, last = self.tokens[start], self.tokens[self.pos - 1]
            return cls(*fields, first.start[0], first.start[1], last.end[0], last.end[1])
        return cls(*fields)

    def error(self, msg: str, index: int | None = None) -> AsterSyntaxError:
        t = self.tokens[self.pos if index is None else index]
        line, col = t.start
        text = self.lines[line - 1] if line - 1 < len(self.lines) else ""
        width = len(t.string) if t.end[0] == line and t.string else 1
        return AsterSyntaxError(msg, self.filename, line, col, text, col + max(1, width))

    def generic_error(self) -> AsterSyntaxError:
        """가장 멀리까지 읽은 위치에서 '무엇이 필요했는지'를 알려 주는 오류."""
        t = self.tokens[self._farthest]
        if (self._farthest > 0 and "';'" in self._expected 
            and t.start[0] > self.tokens[self._farthest - 1].end[0]):
            # 다음 줄에서 문제가 드러났다면 ';'가 빠진 곳은 앞 줄의 끝이다 (가장 흔한 실수).
            line, col = self.tokens[self._farthest - 1].end
            return AsterSyntaxError("문법 오류: 줄 끝에 ';'가 빠졌습니다", self.filename,
                                    line, col, self.lines[line - 1], col + 1)
        found = "파일 끝" if t.type is TokenType.ENDMARKER else repr(t.string)
        msg = f"문법 오류: 예상하지 못한 {found}"
        if self._expected:
            ordered = sorted(self._expected, key=lambda w: w not in _PRIORITY_EXPECTED)  # 안정 정렬
            shown = ", ".join(ordered[:_MAX_EXPECTED])
            more = " 등" if len(ordered) > _MAX_EXPECTED else ""
            msg += f" ({shown}{more} 중 하나가 필요합니다)"
        return self.error(msg, self._farthest)

    def run(self, start_rule: Callable[[], Any]) -> Any:
        """시작 규칙을 실행한다. 실패하면 AsterSyntaxError를 낸다."""
        old_limit = sys.getrecursionlimit()
        sys.setrecursionlimit(max(old_limit, _MAX_RECURSION))
        try:
            result = start_rule()
        except RecursionError:
            raise self.error("식이나 블록의 중첩이 너무 깊습니다") from None
        finally:
            sys.setrecursionlimit(old_limit)
        if result is None:
            raise self.generic_error()
        return result

    # ================================================================ 시작 규칙

    @rule
    def eval_input(self) -> Any:
        e = self.expression()
        if e is None:
            return None
        if self.tok.type is not TokenType.ENDMARKER:
            self._expect("파일 끝")
            return None
        return e

    @rule
    def type_input(self) -> Any:
        t = self.type()
        if t is None:
            return None
        if self.tok.type is not TokenType.ENDMARKER:
            self._expect("파일 끝")
            return None
        return t
    
        # ================================================================== 시작 규칙

    @rule
    def file(self) -> Any:
        package = self.package_stmt()
        body = []
        while True:
            s = self.statement()
            if s is None:
                break
            body.append(s)
        if self.tok.type is not TokenType.ENDMARKER:
            self._expect("파일 끝")
            return None
        return ast.Module([] if package is None else package, body)

    @rule
    def package_stmt(self) -> Any:
        if not self.accept("package"):
            return None
        path = self.dotted_name()
        return path if path is not None and self.accept(";") else None

    @rule
    def dotted_name(self) -> Any:
        first = self.name()
        if first is None:
            return None
        parts = [first]
        while True:
            mark = self.pos
            if not self.accept("."):
                return parts
            part = self.name()
            if part is None:                                # 'a.b.*' 의 '.*' 는 여기서 멈춘다
                self.pos = mark
                return parts
            parts.append(part)

    # ================================================================== 문장

    @rule
    def statement(self) -> Any:
        s = self.compound_stmt()
        return s if s is not None else self.simple_stmt()

    @rule
    def compound_stmt(self) -> Any:
        for alternative in (self.function_def, self.class_def, self.interface_def, self.struct_def,
                            self.typedef_struct, self.if_stmt, self.while_stmt, self.for_stmt,
                            self.try_stmt):
            s = alternative()
            if s is not None:
                return s
        return None

    @rule
    def simple_stmt(self) -> Any:
        # var_decl이 assign_stmt, expr_stmt보다 먼저다. ('a * b;'는 C처럼 선언으로 읽는다.)
        for alternative in (self.import_stmt, self.return_stmt, self.throw_stmt, self.break_stmt,
                            self.continue_stmt, self.var_decl, self.assign_stmt, self.expr_stmt):
            s = alternative()
            if s is not None:
                return s
        return None

    @rule
    def block(self) -> Any:
        if not self.accept("{"):
            return None
        body = []
        while True:
            s = self.statement()
            if s is None:
                break
            body.append(s)
        return body if self.accept("}") else None

    @rule
    def import_stmt(self) -> Any:
        start = self.pos
        if self.accept("import"):
            path = self.dotted_name()
            if path is None:
                return None
            if self.accept("."):                            # [D12a] import a.b.*;
                if not (self.accept("*") and self.accept(";")):
                    return None
                return self.make(ast.Import, start, path, 1)
            return self.make(ast.Import, start, path, 0) if self.accept(";") else None
        if self.accept("from"):
            module = self.dotted_name()
            if module is None or not self.accept("import"):
                return None
            name = None
            if not self.accept("*"):                        # [D12a] from a.b import *;
                name = self.name()
                if name is None:
                    return None
            return self.make(ast.ImportFrom, start, module, name) if self.accept(";") else None
        return None

    @rule
    def return_stmt(self) -> Any:
        start = self.pos
        if not self.accept("return"):
            return None
        value = None
        if not self.at(";"):
            value = self.expression()
            if value is None:
                return None
        return self.make(ast.Return, start, value) if self.accept(";") else None

    @rule
    def throw_stmt(self) -> Any:
        start = self.pos
        if not self.accept("throw"):
            return None
        value = self.expression()
        if value is None:
            return None
        exc_type = None
        if self.accept("as"):                               # [D14] throw "msg" as ValueError;
            exc_type = self.name()
            if exc_type is None:
                return None
        return self.make(ast.Throw, start, value, exc_type) if self.accept(";") else None

    @rule
    def break_stmt(self) -> Any:
        start = self.pos
        return self.make(ast.Break, start) if self.accept("break") and self.accept(";") else None

    @rule
    def continue_stmt(self) -> Any:
        start = self.pos
        return self.make(ast.Continue, start) if self.accept("continue") and self.accept(";") else None

    @rule
    def expr_stmt(self) -> Any:
        start = self.pos
        value = self.expression()
        if value is None or not self.accept(";"):
            return None
        return self.make(ast.ExprStmt, start, value)

    # ============================================================ 선언과 대입

    @rule
    def var_decl(self) -> Any:
        start = self.pos
        core = self.var_decl_core()
        if core is None or not self.accept(";"):
            return None
        return self.make(ast.VarDecl, start, core.ty, core.name, core.init)   # ';'까지 포함한 위치

    @rule
    def var_decl_core(self) -> Any:
        start = self.pos
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None:
            return None
        after_name = self.pos
        if self.accept("="):
            init = self.initializer()
            if init is not None:
                return self.make(ast.VarDecl, start, ty, name, init)
            self.pos = after_name                           # 'type NAME' 만 읽는 두 번째 선택
        return self.make(ast.VarDecl, start, ty, name, ast.NoInit())

    @rule
    def initializer(self) -> Any:
        init = self.brace_init()
        if init is not None:
            return init
        value = self.expression()
        return None if value is None else ast.ExprInit(value)

    @rule
    def brace_init(self) -> Any:
        if not self.accept("{"):
            return None
        if self.accept("}"):                                # {}  [Q2.2]
            return ast.PositionalInit([])
        first = self.designated_init()
        if first is not None:                               # { .y = 20, .x = 10 }
            items = [first]
            while self.accept(","):
                item = self.designated_init()
                if item is None:
                    return None
                items.append(item)
            return ast.DesignatedInit(items) if self.accept("}") else None
        values = []                                         # { 10, 20 }
        while True:
            value = self.expression()
            if value is None:
                return None
            values.append(value)
            if not self.accept(","):
                break
        return ast.PositionalInit(values) if self.accept("}") else None

    @rule
    def designated_init(self) -> Any:
        if not self.accept("."):
            return None
        field = self.name()
        if field is None or not self.accept("="):
            return None
        value = self.expression()
        return None if value is None else ast.designation(field, value)

    @rule
    def assign_stmt(self) -> Any:
        start = self.pos
        core = self.assign_core()
        if core is None or not self.accept(";"):
            return None
        return self.make(ast.Assign, start, core.target, core.op, core.value)

    @rule
    def assign_core(self) -> Any:
        start = self.pos
        target = self.assign_target()
        if target is None:
            return None
        op = self.assign_op()
        if op is None:
            return None
        value = self.expression()
        return None if value is None else self.make(ast.Assign, start, target, op, value)

    @rule
    def assign_target(self) -> Any:
        start = self.pos
        if self.accept("*"):                                # [D2] *p = 5;
            inner = self.assign_target()
            return None if inner is None else self.make(ast.Deref, start, inner)
        if self.accept("**"):                               # **pp = 5;
            inner = self.assign_target()
            if inner is None:
                return None
            return self.make(ast.Deref, start, self.make(ast.Deref, start, inner))
        target = self.postfix_expr()
        if target is None:
            return None
        if not isinstance(target, (ast.Name, ast.Attribute, ast.PtrAttribute)):   # &{ is_assignable }
            self._expect("대입할 수 있는 좌변")
            return None
        return target

    @rule
    def assign_op(self) -> Any:
        for text, cls in _ASSIGN_OPS:
            if self.accept(text):
                return cls()
        return None

    # ================================================================== 함수

    @rule
    def named_type(self) -> Any:
        name = self.name()
        if name is None:
            return None
        args = self.type_args()
        return ast.Named(name, [] if args is None else args)

    @rule
    def type_params(self) -> Any:
        if not self.accept("<"):
            return None
        names = []
        while True:
            name = self.name()
            if name is None:
                return None
            names.append(name)
            if not self.accept(","):
                break
        return names if self.accept(">") else None

    @rule
    def function_def(self) -> Any:
        start = self.pos
        if not self.accept("fn"):
            return None
        name = self.name()
        if name is None:
            return None
        type_params = self.type_params() or []
        self.type_param_stack.append(set(type_params))     # T는 이 함수 안에서 타입 이름이다
        try:
            signature = self._function_signature()
            if signature is None:
                return None
            params, returns = signature
            body = self.block()
            if body is None:
                return None
        finally:
            self.type_param_stack.pop()
        return self.make(ast.FunctionDef, start, name, type_params, params, returns, body)

    def _function_signature(self) -> tuple[list[Any], Any] | None:
        """'(' [parameters] ')' '->' return_type"""
        if not self.accept("("):
            return None
        params = [] if self.at(")") else self.parameters()
        if params is None or not (self.accept(")") and self.accept("->")):
            return None
        returns = self.return_type()
        return None if returns is None else (params, returns)

    # ================================================================== 클래스

    @rule
    def class_def(self) -> Any:
        start = self.pos
        if not self.accept("class"):
            return None
        name = self.name()
        if name is None:
            return None
        type_params = self.type_params() or []
        base = None
        if self.accept("extends"):
            base = self.named_type()
            if base is None:
                return None
        interfaces = []
        if self.accept("implements"):
            while True:
                interface = self.named_type()
                if interface is None:
                    return None
                interfaces.append(interface)
                if not self.accept(","):
                    break
        if not self.accept("{"):
            return None
        self.type_param_stack.append(set(type_params))
        try:
            members = []
            while True:
                member = self.class_member()
                if member is None:
                    break
                members.append(member)
            if not self.accept("}"):
                return None
        finally:
            self.type_param_stack.pop()
        return self.make(ast.ClassDef, start, name, type_params, base, interfaces, members)

    @rule
    def class_member(self) -> Any:
        # 소프트 키워드 규칙을 먼저 시도한다.
        for alternative in (self.constructor_def, self.property_decl, self.getter_def,
                            self.setter_def, self.method_def, self.field_decl):
            member = alternative()
            if member is not None:
                return member
        return None

    @rule
    def access_modifier(self) -> Any:
        for text, cls in _ACCESS:
            if self.accept(text):
                return cls()
        return None

    @rule
    def field_decl(self) -> Any:
        start = self.pos
        access = self.access_modifier()
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None:
            return None
        value = None
        mark = self.pos
        if self.accept("="):
            value = self.expression()
            if value is None:
                self.pos = mark                             # ['=' expression] 이 통째로 없는 것으로
        if not self.accept(";"):
            return None
        return self.make(ast.Field, start, access, ty, name, value)

    @rule
    def method_def(self) -> Any:
        start = self.pos
        access = self.access_modifier()
        if not self.accept("fn"):
            return None
        name = self.name()
        if name is None:
            return None
        type_params = self.type_params() or []
        self.type_param_stack.append(set(type_params))
        try:
            signature = self._function_signature()
            if signature is None:
                return None
            params, returns = signature
            body = self.block()
            if body is None:
                return None
        finally:
            self.type_param_stack.pop()
        return self.make(ast.Method, start, access, name, type_params, params, returns, body)

    @rule
    def constructor_def(self) -> Any:
        start = self.pos
        if not (self.accept("constructor") and self.accept("(")):
            return None
        params = [] if self.at(")") else self.parameters()
        if params is None or not self.accept(")"):
            return None
        body = self.block()                                 # 생성자는 반환 타입을 쓰지 않는다
        return None if body is None else self.make(ast.Constructor, start, params, body)

    @rule
    def property_decl(self) -> Any:
        start = self.pos
        access = self.access_modifier()
        if not (self.accept("@") and self.accept("property")):
            return None
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None or not self.accept(";"):
            return None
        return self.make(ast.Property, start, access, ty, name)

    @rule
    def getter_def(self) -> Any:
        start = self.pos
        if not self.accept("getter"):
            return None
        name = self.name()
        if name is None:
            return None
        body = self.block()
        return None if body is None else self.make(ast.Getter, start, name, body)

    @rule
    def setter_def(self) -> Any:
        start = self.pos
        if not self.accept("setter"):
            return None
        name = self.name()
        if name is None or not self.accept("("):
            return None
        ty = self.type()
        if ty is None:
            return None
        param_index = self.pos
        param_name = self.name()
        if param_name is None or not self.accept(")"):
            return None
        if param_name != "value":                           # &{ name == 'value' }
            raise self.error(f"setter의 매개변수 이름은 value여야 합니다 ('{param_name}')", param_index)
        body = self.block()
        if body is None:
            return None
        return self.make(ast.Setter, start, name, ast.param(ty, param_name), body)

    # ============================================================== 인터페이스

    @rule
    def interface_def(self) -> Any:
        start = self.pos
        if not self.accept("interface"):
            return None
        name = self.name()
        if name is None or not self.accept("{"):
            return None
        methods = []
        while True:
            method = self.interface_method()
            if method is None:
                break
            methods.append(method)
        return self.make(ast.InterfaceDef, start, name, methods) if self.accept("}") else None

    @rule
    def interface_method(self) -> Any:
        access = self.access_modifier()
        if not self.accept("fn"):
            return None
        name = self.name()
        if name is None:
            return None
        signature = self._function_signature()
        if signature is None or not self.accept(";"):       # 본문 대신 ';'
            return None
        params, returns = signature
        return ast.signature(access, name, params, returns)

    # ================================================================== 구조체

    @rule
    def struct_def(self) -> Any:
        start = self.pos
        if not self.accept("struct"):
            return None
        tag = self.name()
        if tag is None:
            return None
        self._struct_names.append({tag})
        try:
            members = self.struct_body()
        finally:
            self._struct_names.pop()
        if members is None or not self.accept(";"):
            return None
        return self.make(ast.StructDef, start, tag, None, members)

    @rule
    def typedef_struct(self) -> Any:
        start = self.pos
        if not (self.accept("typedef") and self.accept("struct")):
            return None
        tag_index = self.pos
        tag = self.name()
        if tag is None:
            return None
        names = {tag}
        if tag_index in self._typedef_alias:                # 별칭은 prescan이 미리 알려 준다
            names.add(self._typedef_alias[tag_index])
        self._struct_names.append(names)
        try:
            members = self.struct_body()
        finally:
            self._struct_names.pop()
        if members is None:
            return None
        alias = self.name()
        if alias is None or not self.accept(";"):
            return None
        return self.make(ast.StructDef, start, tag, alias, members)

    @rule
    def struct_body(self) -> Any:
        if not self.accept("{"):
            return None
        members = []
        while True:
            member = self.struct_member()
            if member is None:
                break
            members.append(member)
        return members if self.accept("}") else None

    @rule
    def struct_member(self) -> Any:
        member = self.struct_constructor()
        return member if member is not None else self.struct_field()

    @rule
    def struct_field(self) -> Any:
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None or not self.accept(";"):
            return None
        return ast.StructField(ty, name)

    @rule
    def struct_constructor(self) -> Any:
        name_index = self.pos
        name = self.name()
        if name is None or not self.accept("("):
            return None
        # 구조체 안의 'NAME (' 는 필드가 될 수 없으므로 여기부터는 생성자다.   [D3]
        allowed = self._struct_names[-1] if self._struct_names else set()
        if name not in allowed:                             # &{ 술어 }
            raise self.error(
                f"구조체 생성자의 이름은 구조체 이름({', '.join(sorted(allowed))})과 같아야 합니다: '{name}'",
                name_index)
        params = [] if self.at(")") else self.parameters()
        if params is None or not self.accept(")"):
            return None
        inits = []
        if self.accept(":"):                                # [D15] 초기화 리스트는 선택, 쓰면 하나 이상
            while True:
                init = self.member_init()
                if init is None:
                    return None
                inits.append(init)
                if not self.accept(","):
                    break
        body = self.block()
        return None if body is None else ast.StructCtor(params, inits, body)

    @rule
    def member_init(self) -> Any:
        field = self.name()
        if field is None or not self.accept("("):
            return None
        value = self.expression()
        if value is None or not self.accept(")"):
            return None
        return ast.member_init(field, value)

    # ================================================================== 제어문

    @rule
    def if_stmt(self) -> Any:
        start = self.pos
        if not (self.accept("if") and self.accept("(")):
            return None
        test = self.expression()
        if test is None or not self.accept(")"):
            return None
        body = self.block()
        if body is None:
            return None
        orelse: list[Any] = []
        mark = self.pos
        if self.accept("else"):
            else_body = self.else_body()
            if else_body is None:
                self.pos = mark                             # ['else' else_body] 이 통째로 없는 것으로
            else:
                orelse = else_body
        return self.make(ast.If, start, test, body, orelse)

    @rule
    def else_body(self) -> Any:
        body = self.block()
        if body is not None:
            return body
        nested = self.if_stmt()                             # [N3] else if
        return None if nested is None else [nested]

    @rule
    def while_stmt(self) -> Any:
        start = self.pos
        if not (self.accept("while") and self.accept("(")):
            return None
        test = self.expression()
        if test is None or not self.accept(")"):
            return None
        body = self.block()
        return None if body is None else self.make(ast.While, start, test, body)

    @rule
    def for_stmt(self) -> Any:
        s = self.range_for_stmt()
        return s if s is not None else self.c_for_stmt()

    @rule
    def range_for_stmt(self) -> Any:
        # for (int i; i = 1..6..2):  ';' 앞은 변수 선언, 뒤는 범위 대입.      [Q6]
        start = self.pos
        if not (self.accept("for") and self.accept("(")):
            return None
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None or not self.accept(";"):
            return None
        target_index = self.pos
        target = self.name()
        if target is None or not self.accept("="):
            return None
        iterable = self.range_expr()
        if iterable is None or not self.accept(")"):
            return None
        # '..'까지 읽었으므로 범위 for가 확실하다. AST에는 이름이 하나만 남으므로 여기서 검사한다.
        if target != name:
            raise self.error(f"범위 for의 변수 이름이 같아야 합니다: '{name}'를 선언하고 '{target}'에 대입했습니다",
                             target_index)
        body = self.block()
        return None if body is None else self.make(ast.ForRange, start, ty, name, iterable, body)

    @rule
    def range_expr(self) -> Any:
        start = self.arith()
        if start is None or not self.accept(".."):
            return None
        stop = self.arith()
        if stop is None:
            return None
        step = None
        mark = self.pos
        if self.accept(".."):
            step = self.arith()
            if step is None:
                self.pos = mark                             # ['..' arith] 이 통째로 없는 것으로
        return ast.range_expr(start, stop, step)

    @rule
    def c_for_stmt(self) -> Any:
        start = self.pos
        if not (self.accept("for") and self.accept("(")):
            return None
        init = self.for_init()
        if init is None or not self.accept(";"):
            return None
        test = self.expression()
        if test is None or not self.accept(";"):
            return None
        step = self.for_step()
        if step is None or not self.accept(")"):
            return None
        body = self.block()
        return None if body is None else self.make(ast.For, start, init, test, step, body)

    @rule
    def for_init(self) -> Any:
        init = self.var_decl_core()
        return init if init is not None else self.assign_core()

    @rule
    def for_step(self) -> Any:
        return self.assign_core()

    @rule
    def try_stmt(self) -> Any:
        start = self.pos
        if not self.accept("try"):
            return None
        body = self.block()
        if body is None:
            return None
        handlers = []
        while True:
            handler = self.catch_clause()
            if handler is None:
                break
            handlers.append(handler)
        finalbody: list[Any] = []
        if handlers:                                        # 'try' block catch_clause+ ['finally' block]
            mark = self.pos
            if self.accept("finally"):
                final = self.block()
                if final is None:
                    self.pos = mark
                else:
                    finalbody = final
            return self.make(ast.Try, start, body, handlers, finalbody)
        if not self.accept("finally"):                      # 'try' block 'finally' block
            return None
        final = self.block()
        return None if final is None else self.make(ast.Try, start, body, [], final)

    @rule
    def catch_clause(self) -> Any:
        if not (self.accept("catch") and self.accept("(")):
            return None
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        if name is None or not self.accept(")"):
            return None
        body = self.block()
        return None if body is None else ast.handler(ty, name, body)
    
    # ===================================================================== 타입

    @rule
    def type(self) -> Any:
        base = self.type_base()
        if base is None:
            return None
        base = self._pointer_suffixes(base)
        if self.accept("?"):
            base = ast.Nullable(base)
        return base

    @rule
    def type_base(self) -> Any:
        base = self.primitive_type()
        if base is not None:
            return base
        if self.accept("struct"):
            tag = self.name()
            return None if tag is None else ast.StructRef(tag)
        name = self.name()
        if name is None:
            return None
        args = self.type_args()
        return ast.Named(name, [] if args is None else args)

    @rule
    def primitive_type(self) -> Any:
        t = self.tok
        kind = _PRIMITIVES.get(t.string) if t.type is TokenType.NAME else None
        if kind is None:
            self._expect("기본 타입")
            return None
        self.pos += 1
        return ast.Primitive(kind())

    @rule
    def pointer_suffix(self) -> Any:
        if self.accept("*"):
            return 1
        if self.accept("**"):
            return 2
        return None

    def _pointer_suffixes(self, base: Any) -> Any:
        """pointer_suffix*  ('**'는 포인터 두 단계)."""
        while True:
            count = self.pointer_suffix()
            if count is None:
                return base
            for _ in range(count):
                base = ast.Pointer(base)

    @rule
    def type_args(self) -> Any:
        if not self.accept("<"):
            return None
        args = []
        while True:
            t = self.type()
            if t is None:
                return None
            args.append(t)
            if not self.accept(","):
                break
        return args if self.accept(">") else None

    @rule
    def return_type(self) -> Any:
        if self.accept("void"):
            return ast.VoidRet()
        t = self.type()
        return None if t is None else ast.ValueRet(t)

    # ===================================================================== 함수

    @rule
    def parameters(self) -> Any:
        out = []
        while True:
            p = self.parameter()
            if p is None:
                return None
            out.append(p)
            if not self.accept(","):
                return out

    @rule
    def parameter(self) -> Any:
        ty = self.type()
        if ty is None:
            return None
        name = self.name()
        return None if name is None else ast.param(ty, name)

    # ======================================================================= 식

    @rule
    def expression(self) -> Any:
        e = self.lambda_expr()
        if e is not None:
            return e
        return self.pipe_expr()

    @rule
    def lambda_expr(self) -> Any:
        start = self.pos
        t = self.tok
        if (t.type is TokenType.NAME and t.string not in HARD_KEYWORDS
                and self.tokens[self.pos + 1].string == "=>"):
            self.pos += 2                                   # NAME '=>'
            params = [ast.lparam(None, t.string)]
        elif self.at("("):
            self.pos += 1
            plist = self.parameters()                       # [parameters]
            if not (self.accept(")") and self.accept("=>")):
                return None
            params = [ast.lparam(p.ty, p.name) for p in (plist or [])]
        else:
            return None
        # 여기부터는 '=>'를 읽었으므로 람다다. 실패는 되돌아가지 않고 바로 오류로 낸다.
        body = self.lambda_body()
        if body is None:
            raise self.error("람다 본문(식)이 필요합니다")
        if not self.accept("->"):
            raise self.error("람다에는 반환 타입이 필요합니다: 'x => 식 -> 타입' 형태로 쓰세요")
        returns = self.return_type()
        if returns is None:
            raise self.error("람다의 '->' 뒤에는 반환 타입이 필요합니다")
        if self.at("->"):                                   # !'->'   [D7]
            raise self.error("람다 본문의 포인터 접근(->)은 괄호로 감싸야 합니다: (p->x)")
        return self.make(ast.Lambda, start, params, returns, body)

    @rule
    def lambda_body(self) -> Any:
        saved, self.no_arrow = self.no_arrow, True          # [D7]
        try:
            return self.pipe_expr()
        finally:
            self.no_arrow = saved

    def _chain(self, operand: Callable[[], Any], ops: Iterable[str],
               build: Callable[[int, Any, str, Any], Any]) -> Any:
        """operand (op operand)*  왼쪽 결합."""
        start = self.pos
        left = operand()
        if left is None:
            return None
        while True:
            mark = self.pos
            op = next((s for s in ops if self.accept(s)), None)
            if op is None:
                return left
            right = operand()
            if right is None:
                self.pos = mark
                return left
            left = build(start, left, op, right)

    @rule
    def pipe_expr(self) -> Any:
        return self._chain(self.coalesce_expr, ("|>",),
                           lambda s, left, op, right: self.make(ast.Pipe, s, left, right))

    @rule
    def coalesce_expr(self) -> Any:
        start = self.pos
        left = self.or_expr()
        if left is None:
            return None
        mark = self.pos
        if self.accept("??"):                               # 오른쪽 결합
            right = self.coalesce_expr()
            if right is not None:
                return self.make(ast.Coalesce, start, left, right)
            self.pos = mark
        return left

    @rule
    def or_expr(self) -> Any:
        return self._chain(self.and_expr, ("or",),
                           lambda s, left, op, right: self.make(ast.BoolOp, s, ast.Or(), left, right))

    @rule
    def and_expr(self) -> Any:
        return self._chain(self.not_expr, ("and",),
                           lambda s, left, op, right: self.make(ast.BoolOp, s, ast.And(), left, right))

    @rule
    def not_expr(self) -> Any:
        start = self.pos
        if self.accept("not"):
            operand = self.not_expr()
            return None if operand is None else self.make(ast.UnaryOp, start, ast.Not(), operand)
        return self.comparison()

    @rule
    def comparison(self) -> Any:
        start = self.pos
        left = self.arith()
        if left is None:
            return None
        mark = self.pos
        if self.accept("is"):
            ty = self.type()
            if ty is not None:
                return self.make(ast.Is, start, left, ty)
        elif self.accept("in"):
            right = self.arith()
            if right is not None:
                return self.make(ast.In, start, left, right)
        else:
            op = self.comp_op()
            if op is not None:
                right = self.arith()
                if right is not None:
                    return self.make(ast.Compare, start, left, op, right)
        self.pos = mark                                     # 비교는 연쇄하지 않는다
        return left

    @rule
    def comp_op(self) -> Any:
        for text, cls in _CMP_OPS:
            if self.accept(text):
                return cls()
        return None

    @rule
    def arith(self) -> Any:
        return self._chain(self.term, tuple(_ADD_OPS),
                           lambda s, left, op, right: self.make(ast.BinOp, s, left, _ADD_OPS[op](), right))

    @rule
    def term(self) -> Any:
        return self._chain(self.unary, tuple(_MUL_OPS),
                           lambda s, left, op, right: self.make(ast.BinOp, s, left, _MUL_OPS[op](), right))

    @rule
    def unary(self) -> Any:
        start = self.pos
        for text, cls in (("+", ast.UAdd), ("-", ast.USub)):
            if self.accept(text):
                operand = self.unary()
                return None if operand is None else self.make(ast.UnaryOp, start, cls(), operand)
        if self.accept("&"):                                # [D2] 주소
            operand = self.unary()
            return None if operand is None else self.make(ast.AddressOf, start, operand)
        if self.accept("*"):                                # [D2] 역참조
            operand = self.unary()
            return None if operand is None else self.make(ast.Deref, start, operand)
        if self.accept("**"):                               # [D8] 역참조 두 번
            operand = self.unary()
            if operand is None:
                return None
            return self.make(ast.Deref, start, self.make(ast.Deref, start, operand))
        c = self.cast()
        return c if c is not None else self.power()

    @rule
    def cast(self) -> Any:
        start = self.pos
        if not self.accept("("):
            return None
        ty = self.cast_type()
        if ty is None or not self.accept(")"):
            return None
        operand = self.unary()
        return None if operand is None else self.make(ast.Cast, start, ty, operand)

    @rule
    def cast_type(self) -> Any:
        base = self.primitive_type()
        if base is None:
            if self.accept("struct"):
                tag = self.name()
                if tag is None:
                    return None
                base = ast.StructRef(tag)
            else:
                base = self._declared_named_type()
                if base is None:
                    return None
        base = self._pointer_suffixes(base)
        return ast.Nullable(base) if self.accept("?") else base

    @rule
    def power(self) -> Any:
        start = self.pos
        left = self.postfix_expr()
        if left is None:
            return None
        mark = self.pos
        if self.accept("**"):                               # 오른쪽 결합
            right = self.unary()
            if right is not None:
                return self.make(ast.BinOp, start, left, ast.Pow(), right)
            self.pos = mark
        return left

    @rule
    def postfix_expr(self) -> Any:
        start = self.pos
        value = self.primary()
        if value is None:
            return None
        while True:
            mark = self.pos
            if self.accept("."):
                attr = self.name()
                if attr is None:
                    self.pos = mark
                    return value
                value = self.make(ast.Attribute, start, value, attr)
            elif not self.no_arrow and self.accept("->"):   # [D7] 람다 본문 최상위에서는 읽지 않는다
                attr = self.name()
                if attr is None:
                    self.pos = mark
                    return value
                value = self.make(ast.PtrAttribute, start, value, attr)
            elif self.accept("("):
                args = self._call_arguments()
                if args is None:
                    self.pos = mark
                    return value
                value = self.make(ast.Call, start, value, args)
            else:
                return value

    def _call_arguments(self) -> list[Any] | None:
        """'('는 이미 읽었다. [arguments] ')'를 읽는다. no_arrow는 괄호 안에서 풀린다."""
        saved, self.no_arrow = self.no_arrow, False
        try:
            args = [] if self.at(")") else self.arguments()
            if args is None or not self.accept(")"):
                return None
            return args
        finally:
            self.no_arrow = saved

    @rule
    def primary(self) -> Any:
        start = self.pos
        t = self.tok
        if t.type is TokenType.NAME:
            s = t.string
            if s == "sizeof":
                return self.sizeof_expr()
            if s == "new":
                return self._new_expr()
            if s in ("self", "true", "false", "null") or s not in HARD_KEYWORDS:
                self.pos += 1
                if s == "self":
                    return self.make(ast.Self, start)
                if s in ("true", "false", "null"):
                    return self.make(ast.Constant, start, {"true": True, "false": False, "null": None}[s])
                return self.make(ast.Name, start, s)
        elif t.type is TokenType.INT:
            self.pos += 1
            return self.make(ast.Constant, start, int(t.string))
        elif t.type is TokenType.FLOAT:
            self.pos += 1
            return self.make(ast.Constant, start, float(t.string))
        elif t.type is TokenType.STRING:
            self.pos += 1
            return self.make(ast.Constant, start, decode_string(t.string))
        elif self.accept("("):
            saved, self.no_arrow = self.no_arrow, False     # [D7] 괄호 안에서는 '->'가 다시 포인터 접근
            try:
                e = self.expression()
                return e if e is not None and self.accept(")") else None
            finally:
                self.no_arrow = saved
        self._expect("식")
        return None

    def _new_expr(self) -> Any:
        """'new' NAME [type_args] '(' [arguments] ')'  [D11] 결과는 주소다."""
        start = self.pos
        self.pos += 1                                       # 'new'
        cls = self.name()
        if cls is None:
            return None
        type_args = self.type_args()
        if not self.accept("("):
            return None
        args = self._call_arguments()
        if args is None:
            return None
        return self.make(ast.New, start, cls, [] if type_args is None else type_args, args)

    @rule
    def sizeof_expr(self) -> Any:
        start = self.pos
        if not (self.accept("sizeof") and self.accept("(")):
            return None
        saved, self.no_arrow = self.no_arrow, False
        try:
            ty = self.sized_type()
            if ty is None or not self.accept(")"):
                return None
        finally:
            self.no_arrow = saved
        return self.make(ast.SizeOf, start, ty)

    @rule
    def sized_type(self) -> Any:
        base = self.primitive_type()
        if base is None:
            if self.accept("struct"):
                tag = self.name()
                if tag is None:
                    return None
                base = ast.StructRef(tag)
            else:
                base = self._declared_named_type()
                if base is None:
                    return None
        return self._pointer_suffixes(base)

    def _declared_named_type(self) -> Any:
        """NAME &{ is_declared_type_name } [type_args]"""
        t = self.tok
        if t.type is TokenType.NAME and t.string not in HARD_KEYWORDS \
                and self.is_declared_type_name(t.string):
            self.pos += 1
            args = self.type_args()
            return ast.Named(t.string, [] if args is None else args)
        self._expect("선언된 타입 이름")
        return None

    @rule
    def arguments(self) -> Any:
        out = []
        while True:
            e = self.expression()
            if e is None:
                return None
            out.append(e)
            if not self.accept(","):
                return out


# ============================================================ 파서 API

def parse(source: str, filename: str = "<string>", type_names: Iterable[str] = ()) -> Any:
    """소스 파일 전체를 읽어 Module 노드를 만든다 (CPython의 ast.parse와 같은 역할)."""
    p = Parser(source, filename, type_names)
    return p.run(p.file)


def parse_file(path: str) -> Any:
    """파일을 UTF-8로 읽어 parse()한다."""
    with open(path, encoding="utf-8") as f:
        return parse(f.read(), path)

def parse_expression(source: str, filename: str = "<string>",
                     type_names: Iterable[str] = ()) -> Any:
    """식 하나를 읽는다 (CPython의 mode='eval'과 같은 역할)."""
    p = Parser(source, filename, type_names)
    return p.run(p.eval_input)


def parse_type(source: str, filename: str = "<string>",
               type_names: Iterable[str] = ()) -> Any:
    """타입 하나를 읽는다."""
    p = Parser(source, filename, type_names)
    return p.run(p.type_input)