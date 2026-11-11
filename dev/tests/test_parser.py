import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Tools"))

import check_conformance  # pyright: ignore[reportMissingImports] # noqa: E402
from dev.src.asterlang.syntax import nodes as ast  # noqa: E402


class AsdlConformanceTest(unittest.TestCase):
    def test_nodes_match_asdl(self):
        self.assertEqual(check_conformance.check_asdl(), [])

    def test_asdl_parser(self):
        name, dfns = check_conformance.parse_asdl(
            "module M { a = A(int x, b* ys, c? z) | B attributes (int n) "
            "-- comment\n p = (int q) }")
        self.assertEqual(name, "M")
        self.assertEqual([c.name for c in dfns["a"].constructors], ["A", "B"])
        x, ys, z = dfns["a"].constructors[0].fields
        self.assertEqual((ys.seq, ys.opt, z.seq, z.opt, x.seq), (True, False, False, True, False))
        self.assertEqual([f.name for f in dfns["a"].attributes], ["n"])
        self.assertEqual(dfns["p"].fields[0].name, "q")

    def test_asdl_parser_rejects_garbage(self):
        with self.assertRaises(ValueError):
            check_conformance.parse_asdl("module M { a = A(int) }")
        with self.assertRaises(ValueError):
            check_conformance.parse_asdl("module M { a = A $ }")


class SpecInvariantTest(unittest.TestCase):
    """Aster 명세에서 정한 결정이 AST 모양에 남아 있는지 확인한다."""

    def names(self, base):
        return {c.__name__ for c in base.__subclasses__()}

    def test_void_is_not_a_type(self):
        self.assertEqual(self.names(ast.prim), {"IntT", "FloatT", "BoolT", "StrT", "NullT"})
        self.assertEqual(self.names(ast.ret), {"VoidRet", "ValueRet"})
        self.assertEqual(self.names(ast.typeref),
                         {"Primitive", "Named", "StructRef", "Pointer", "Nullable"})

    def test_struct_is_not_a_class(self):
        self.assertFalse(issubclass(ast.StructDef, ast.ClassDef))
        self.assertFalse(issubclass(ast.ClassDef, ast.StructDef))

    def test_no_removed_features(self):
        self.assertFalse(hasattr(ast, "FuncType"))   # D6b: 함수 타입 표기 없음
        self.assertFalse(hasattr(ast, "This"))       # D11c: this가 아니라 self

    def test_logical_operators_are_words_only(self):
        self.assertEqual(self.names(ast.boolop), {"And", "Or"})
        self.assertEqual(self.names(ast.unaryop), {"UAdd", "USub", "Not"})

    def test_compound_assignment_set(self):
        self.assertEqual(self.names(ast.assign_op),
                         {"AssignEq", "AssignAdd", "AssignSub", "AssignMult", "AssignDiv",
                          "AssignFloorDiv", "AssignMod", "AssignPow"})


class NodeBehaviorTest(unittest.TestCase):
    def test_positional_and_keyword(self):
        a = ast.Name("x", lineno=1, col_offset=0)
        b = ast.Name(id="x", lineno=1, col_offset=0)
        self.assertEqual((a.id, a.lineno, a.end_lineno), (b.id, 1, None))

    def test_missing_field(self):
        with self.assertRaises(TypeError):
            ast.Name()
        with self.assertRaises(TypeError):
            ast.Assign(ast.Name("x"), ast.AssignEq())

    def test_unknown_and_duplicate(self):
        with self.assertRaises(TypeError):
            ast.Name("x", bogus=1)
        with self.assertRaises(TypeError):
            ast.Name("x", id="y")
        with self.assertRaises(TypeError):
            ast.Break(1, 2, 3, 4, 5)

    def test_abstract_nodes(self):
        for cls in (ast.AST, ast.stmt, ast.expr, ast.typeref, ast.prim):
            with self.subTest(cls=cls.__name__), self.assertRaises(TypeError):
                cls()

    def test_hierarchy(self):
        self.assertIsInstance(ast.Break(), ast.stmt)
        self.assertIsInstance(ast.Name("x"), ast.expr)
        self.assertNotIsInstance(ast.Name("x"), ast.stmt)
        self.assertEqual(ast.Break._attributes, ast.If._attributes)
        self.assertEqual(ast.IntT._attributes, ())

    def test_dump(self):
        tree = ast.VarDecl(ast.Primitive(ast.IntT()), "count",
                           ast.ExprInit(ast.Constant(3)))
        self.assertEqual(
            ast.dump(tree),
            "VarDecl(ty=Primitive(kind=IntT()), name='count', init=ExprInit(value=Constant(value=3)))")
        self.assertEqual(ast.dump(ast.Break(lineno=2), include_attributes=True),
                         "Break(lineno=2, col_offset=None, end_lineno=None, end_col_offset=None)")
        self.assertEqual(repr(ast.Return(None)), "Return(value=None)")

    def test_dump_indent_and_lists(self):
        tree = ast.Call(ast.Name("f"), [ast.Constant(1), ast.Constant(None)])
        self.assertEqual(ast.dump(tree, indent=2),
                         "Call(\n"
                         "  func=Name(\n"
                         "    id='f'),\n"
                         "  args=[\n"
                         "    Constant(\n"
                         "      value=1),\n"
                         "    Constant(\n"
                         "      value=None)])")
        self.assertEqual(ast.dump(ast.Module([], [])), "Module(package=[], body=[])")

    def test_dump_requires_node(self):
        with self.assertRaises(TypeError):
            ast.dump("x")

    def test_generic_class_shape(self):
        # class Box<T> {} -> Named('Box', [...]) 로 쓰는 타입 인자와 type_params를 가진다.
        t = ast.Named("Box", [ast.Primitive(ast.IntT())])
        c = ast.ClassDef("Box", ["T"], None, [], [])
        self.assertEqual(ast.dump(t), "Named(name='Box', args=[Primitive(kind=IntT())])")
        self.assertEqual(c.type_params, ["T"])

# ============================================================ 문법 규칙 대응 (aster.gram <-> parser.py)

from dev.src.asterlang.parser import parser as aster_parser  # noqa: E402
from dev.src.asterlang.parser.errors import AsterSyntaxError  # noqa: E402
from dev.src.asterlang.parser.parser import parse_expression, parse_type  # noqa: E402


class GrammarRulesTest(unittest.TestCase):
    def test_rules_and_keywords_match_grammar(self):
        self.assertEqual(check_conformance.check_rules(), [])

    def test_gram_keyword_reader(self):
        hard, soft = check_conformance.gram_keywords(
            "# 하드 키워드:\n#   a b\n#   c\n# 소프트 키워드: x y\n# 다른 줄\n")
        self.assertEqual((hard, soft), ({"a", "b", "c"}, {"x", "y"}))

    def test_every_rule_is_registered_once(self):
        self.assertIn("expression", aster_parser.RULE_NAMES)
        self.assertNotIn("make", aster_parser.RULE_NAMES)       # 도우미 메서드는 규칙이 아니다


# ============================================================ 식과 타입 (테스트용 축약 표기)

_OPS = {"Add": "+", "Sub": "-", "Mult": "*", "Div": "/", "FloorDiv": "//", "Mod": "%", "Pow": "**",
        "And": "and", "Or": "or", "UAdd": "+", "USub": "-", "Not": "not",
        "Eq": "==", "NotEq": "!=", "Lt": "<", "LtE": "<=", "Gt": ">", "GtE": ">="}


def ty(n):
    """타입 노드를 'Box<int>*?' 같은 글로 바꾼다."""
    if isinstance(n, ast.Primitive):
        return type(n.kind).__name__[:-1].lower()
    if isinstance(n, ast.Named):
        return n.name + ("<" + ",".join(ty(a) for a in n.args) + ">" if n.args else "")
    if isinstance(n, ast.StructRef):
        return "struct " + n.tag
    if isinstance(n, ast.Pointer):
        return ty(n.base) + "*"
    if isinstance(n, ast.Nullable):
        return ty(n.base) + "?"
    raise AssertionError(n)


def sx(n):
    """식 노드를 S-식 '(+ 1 (* 2 3))'으로 바꾼다. 우선순위와 결합 방향을 한눈에 비교하려는 용도다."""
    op = lambda o: _OPS[type(o).__name__]  # noqa: E731
    if isinstance(n, ast.Constant):
        return repr(n.value)
    if isinstance(n, ast.Name):
        return n.id
    if isinstance(n, ast.Self):
        return "self"
    if isinstance(n, (ast.BinOp, ast.Compare)):
        return f"({op(n.op)} {sx(n.left)} {sx(n.right)})"
    if isinstance(n, ast.BoolOp):
        return f"({op(n.op)} {sx(n.left)} {sx(n.right)})"
    if isinstance(n, ast.UnaryOp):
        return f"({op(n.op)} {sx(n.operand)})"
    if isinstance(n, ast.Pipe):
        return f"(|> {sx(n.value)} {sx(n.func)})"
    if isinstance(n, ast.Coalesce):
        return f"(?? {sx(n.left)} {sx(n.right)})"
    if isinstance(n, ast.Is):
        return f"(is {sx(n.value)} {ty(n.ty)})"
    if isinstance(n, ast.In):
        return f"(in {sx(n.item)} {sx(n.container)})"
    if isinstance(n, ast.Cast):
        return f"(cast {ty(n.ty)} {sx(n.value)})"
    if isinstance(n, ast.AddressOf):
        return f"(addr {sx(n.value)})"
    if isinstance(n, ast.Deref):
        return f"(deref {sx(n.value)})"
    if isinstance(n, ast.SizeOf):
        return f"(sizeof {ty(n.ty)})"
    if isinstance(n, ast.Call):
        return "(call " + " ".join([sx(n.func)] + [sx(a) for a in n.args]) + ")"
    if isinstance(n, ast.Attribute):
        return f"(. {sx(n.value)} {n.attr})"
    if isinstance(n, ast.PtrAttribute):
        return f"(-> {sx(n.value)} {n.attr})"
    if isinstance(n, ast.New):
        targs = "<" + ",".join(ty(a) for a in n.type_args) + ">" if n.type_args else ""
        return "(new " + " ".join([n.cls + targs] + [sx(a) for a in n.args]) + ")"
    if isinstance(n, ast.Lambda):
        params = " ".join(p.name if p.ty is None else f"{p.name}:{ty(p.ty)}" for p in n.params)
        ret = "void" if isinstance(n.returns, ast.VoidRet) else ty(n.returns.ty)
        return f"(lambda ({params}) {sx(n.body)} -> {ret})"
    raise AssertionError(n)


def parse_sx(source, type_names=()):
    return sx(parse_expression(source, type_names=type_names))


class ExpressionTest(unittest.TestCase):
    def check(self, cases, type_names=()):
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(parse_sx(source, type_names), expected)

    def test_literals_and_names(self):
        self.check([
            ("3", "3"), ("3.8", "3.8"), (r'"a\n"', repr("a\n")), ("'b'", "'b'"),
            ("true", "True"), ("false", "False"), ("null", "None"), ("self", "self"),
            ("x", "x"), ("getter + 1", "(+ getter 1)"), ("constructor", "constructor"),
            ("12345678901234567890123", "12345678901234567890123"),
        ])

    def test_arithmetic_precedence(self):
        self.check([
            ("1 + 2 * 3", "(+ 1 (* 2 3))"),
            ("1 - 2 - 3", "(- (- 1 2) 3)"),
            ("(1 + 2) * 3", "(* (+ 1 2) 3)"),
            ("a // b % c * d", "(* (% (// a b) c) d)"),
            ("2 ** 3 ** 2", "(** 2 (** 3 2))"),
            ("-2 ** 2", "(- (** 2 2))"),
            ("2 ** -1", "(** 2 (- 1))"),
            ("+a", "(+ a)"),
        ])

    def test_pointer_operators(self):
        self.check([
            ("a**p", "(** a p)"),                    # [D8] 거듭제곱
            ("a * *p", "(* a (deref p))"),           # [D8] 곱셈과 역참조
            ("**pp", "(deref (deref pp))"),
            ("&x", "(addr x)"),
            ("*p + 1", "(+ (deref p) 1)"),
            ("&*p", "(addr (deref p))"),
        ])

    def test_logic_and_comparison(self):
        self.check([
            ("not a and b or c", "(or (and (not a) b) c)"),
            ("a or b and c", "(or a (and b c))"),
            ("not not a", "(not (not a))"),
            ("a < b", "(< a b)"), ("a <= b", "(<= a b)"), ("a > b", "(> a b)"),
            ("a >= b", "(>= a b)"), ("a == b", "(== a b)"), ("a != b", "(!= a b)"),
            ("a + 1 < b * 2", "(< (+ a 1) (* b 2))"),
            ("not x is int", "(not (is x int))"),
            ("x is Box<int>", "(is x Box<int>)"),
            ("x is Counter*", "(is x Counter*)"),
            ('"a" in text', "(in 'a' text)"),
            ("a < b and c > d", "(and (< a b) (> c d))"),
        ])

    def test_pipe_and_coalesce(self):
        self.check([
            ("10 |> double", "(|> 10 double)"),
            ("10 |> f |> g", "(|> (|> 10 f) g)"),
            ("a ?? b ?? c", "(?? a (?? b c))"),
            ("a ?? b |> f", "(|> (?? a b) f)"),
            ("a or b ?? c", "(?? (or a b) c)"),
        ])

    def test_cast(self):
        self.check([
            ("(int) 3.8", "(cast int 3.8)"),
            ("(str) count", "(cast str count)"),
            ("(int*) p", "(cast int* p)"),
            ("(float?) x", "(cast float? x)"),
            ("(struct Point*) p", "(cast struct Point* p)"),
            ("(x) + 1", "(+ x 1)"),                  # x는 선언된 타입이 아니므로 캐스트가 아니다
            ("(int) -1", "(cast int (- 1))"),
            ("(int) a + b", "(+ (cast int a) b)"),
        ])
        self.check([
            ("(Foo*) p", "(cast Foo* p)"),
            ("(Box<int>) b", "(cast Box<int> b)"),
        ], type_names={"Foo", "Box"})

    def test_sizeof(self):
        self.check([
            ("sizeof(int)", "(sizeof int)"),
            ("sizeof(struct Point)", "(sizeof struct Point)"),
            ("sizeof(int**)", "(sizeof int**)"),
        ])
        self.check([("sizeof(Point*) * 2", "(* (sizeof Point*) 2)")], type_names={"Point"})

    def test_postfix(self):
        self.check([
            ("a.b.c", "(. (. a b) c)"),
            ("f(1, 2)", "(call f 1 2)"),
            ("f()", "(call f)"),
            ("a.b(1).c", "(. (call (. a b) 1) c)"),
            ("f(1)(2)", "(call (call f 1) 2)"),
            ("p->x", "(-> p x)"),
            ("p->q->x", "(-> (-> p q) x)"),
            ("p->f(1)", "(call (-> p f) 1)"),
            ("self->value", "(-> self value)"),
            ('System.stdout.println("hi", n)', "(call (. (. System stdout) println) 'hi' n)"),
            ("a.b ** 2", "(** (. a b) 2)"),
        ])

    def test_new(self):
        self.check([
            ("new Counter()", "(new Counter)"),
            ("new Point(1, 2)", "(new Point 1 2)"),
            ("new Box<int>(3)", "(new Box<int> 3)"),
            ("new Pair<int, str>(1, 'a')", "(new Pair<int,str> 1 'a')"),
            ("new Box<Box<int>>(x)", "(new Box<Box<int>> x)"),
            ("new Counter()->value", "(-> (new Counter) value)"),
        ])


class LambdaTest(unittest.TestCase):
    def check(self, cases):
        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(parse_sx(source), expected)

    def test_forms(self):
        self.check([
            ("x => x*x -> int", "(lambda (x) (* x x) -> int)"),
            ("(int a, int b) => a+b -> int", "(lambda (a:int b:int) (+ a b) -> int)"),
            ("() => 1 -> int", "(lambda () 1 -> int)"),
            ('() => System.stdout.println("hi") -> void',
             "(lambda () (call (. (. System stdout) println) 'hi') -> void)"),
            ("(Counter* c) => c -> Counter*", "(lambda (c:Counter*) c -> Counter*)"),
            ("x => x -> int?", "(lambda (x) x -> int?)"),
            ("x => x |> f -> int", "(lambda (x) (|> x f) -> int)"),
            ("x => a ?? b -> int", "(lambda (x) (?? a b) -> int)"),
        ])

    def test_arrow_inside_body(self):                            # [D7]
        self.check([
            ("x => (p->v) -> int", "(lambda (x) (-> p v) -> int)"),
            ("x => f(p->v) -> int", "(lambda (x) (call f (-> p v)) -> int)"),
            ("x => (a + p->v) -> int", "(lambda (x) (+ a (-> p v)) -> int)"),
            ("x => (y => y -> int)(x) -> int",
             "(lambda (x) (call (lambda (y) y -> int) x) -> int)"),
        ])

    def test_lambda_as_argument_and_call(self):
        self.check([
            ("apply(x => x*x -> int, 3)", "(call apply (lambda (x) (* x x) -> int) 3)"),
            ("(x => x -> int)(3)", "(call (lambda (x) x -> int) 3)"),
            ("f(() => 1 -> int, 2)", "(call f (lambda () 1 -> int) 2)"),
        ])

    def test_pointer_access_after_lambda_returns_to_normal(self):
        self.check([("(x => x -> int) + p->v", "(+ (lambda (x) x -> int) (-> p v))")])

    def assertError(self, source, fragment, **kw):
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse_expression(source, **kw)
        self.assertIn(fragment, ctx.exception.msg)
        return ctx.exception

    def test_missing_return_type(self):                          # 원칙 4: 람다에도 반환 타입
        self.assertError("x => x*x", "반환 타입")
        self.assertError("(int a) => a", "반환 타입")
        self.assertError("() => 1", "반환 타입")
        self.assertError("x => 1 ->", "반환 타입")

    def test_unparenthesized_pointer_access(self):               # [D7]
        self.assertError("x => p->v -> int", "괄호")
        self.assertError("(Counter* c) => c->value -> int", "괄호")

    def test_missing_body(self):
        self.assertError("x =>", "본문")
        self.assertError("() => -> int", "본문")

    def test_untyped_parameters_only_for_single_name(self):
        self.assertError("(int a, b) => 1 -> int", "문법 오류")
        self.assertError("(x) => x -> int", "문법 오류")
        self.assertError("x, y => x -> int", "문법 오류")


class TypeTest(unittest.TestCase):
    def test_valid(self):
        for source in ["int", "float", "bool", "str", "null", "int*", "int**", "int***", "int?",
                       "int*?", "int**?", "Counter*", "Counter", "Counter?", "Box<int>",
                       "Box<Box<int>>*", "Pair<int, str>", "Pair<int*, Box<str?>>",
                       "struct Point", "struct Point*", "ValueError", "str?"]:
            with self.subTest(source=source):
                compact = source.replace(" ", "")
                self.assertEqual(ty(parse_type(source)).replace(" ", ""), compact)

    def test_pointer_depth(self):
        t = parse_type("int**")
        self.assertIsInstance(t, ast.Pointer)
        self.assertIsInstance(t.base, ast.Pointer)
        self.assertIsInstance(t.base.base, ast.Primitive)

    def test_invalid(self):
        for source in ["void", "void*", "int?*", "int??", "Box<int", "Box<>", "Box<int,>",
                       "int x", "struct", "struct *", "if", "new", "int[", "*int"]:
            with self.subTest(source=source), self.assertRaises(AsterSyntaxError):
                parse_type(source)

    def test_nullable_class_and_primitive_both_parse(self):
        # 'Counter?'가 Counter와 같은 타입으로 취급되는 것(D18b')은 typecheck의 일이다.
        self.assertIsInstance(parse_type("Counter?"), ast.Nullable)


class ParserErrorTest(unittest.TestCase):
    def assertError(self, source, fragment="", **kw):
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse_expression(source, **kw)
        self.assertIn(fragment, ctx.exception.msg)
        return ctx.exception

    def test_incomplete(self):
        e = self.assertError("1 +", "파일 끝")
        self.assertEqual((e.lineno, e.offset), (1, 3))
        self.assertError("(1 + 2", "')'")
        self.assertError("f(1,)", "문법 오류")
        self.assertError("f(1", "')'")

    def test_unexpected_token_is_reported_with_expectations(self):
        e = self.assertError("1 2", "'2'")
        self.assertEqual(e.offset, 2)
        self.assertIn("필요합니다", e.msg)

    def test_closing_delimiter_is_listed_first(self):
        e = self.assertError("(1 + 2", "예상하지 못한 파일 끝 (')'")
        self.assertEqual((e.lineno, e.offset), (1, 6))
        e = self.assertError("f(1, 2", "')'")                      # ','와 ')'가 모두 가능하다
        self.assertLess(e.msg.index("')'"), e.msg.index("'.'"))     # 연산자보다 앞에 나온다

    def test_star_after_operator_is_a_dereference(self):
        self.assertEqual(parse_sx("a +\n  * b"), "(+ a (deref b))")

    def test_no_chained_comparison(self):
        self.assertError("a < b < c", "'<'")
        self.assertError("a == b == c", "'=='")

    def test_keywords_are_not_names(self):
        for source in ["if", "class", "fn", "int", "void", "as", "this_is_ok + new"]:
            with self.subTest(source=source), self.assertRaises(AsterSyntaxError):
                parse_expression(source)
        self.assertEqual(parse_sx("this + getter"), "(+ this getter)")   # this는 키워드가 아니다

    def test_forbidden_forms(self):
        self.assertError("sizeof(x)", "선언된 타입 이름")
        self.assertError("x is", "문법 오류")
        self.assertError("not", "문법 오류")
        self.assertError("f<int>(3)", "문법 오류")                      # [D16] 명시 호출 없음
        self.assertError("(1)(", "문법 오류")

    def test_lexical_errors_surface_as_syntax_errors(self):
        self.assertError("a && b", "and")
        self.assertError("a || b", "or")
        self.assertError("!a", "not")
        self.assertError("a[0]", "[")
        self.assertError("1_000", "number")

    def test_error_text_and_caret(self):
        e = self.assertError("a +\n  / b")                           # '*'는 역참조라 오류가 아니다
        self.assertEqual(e.lineno, 2)
        self.assertIn("^", e.format())

    def test_deep_nesting(self):
        self.assertEqual(parse_sx("(" * 120 + "1" + ")" * 120), "1")
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse_expression("(" * 30000 + "1" + ")" * 30000)
        self.assertIn("중첩", ctx.exception.msg)
        self.assertEqual(sys.getrecursionlimit(), 1000)                # 한도를 되돌려 놓는다


class PositionTest(unittest.TestCase):
    def test_binop_span(self):
        e = parse_expression("a + bcd")
        self.assertEqual((e.lineno, e.col_offset, e.end_lineno, e.end_col_offset), (1, 0, 1, 7))
        self.assertEqual((e.right.col_offset, e.right.end_col_offset), (4, 7))

    def test_multiline_span(self):
        e = parse_expression("a +\n  b")
        self.assertEqual((e.lineno, e.col_offset, e.end_lineno, e.end_col_offset), (1, 0, 2, 3))

    def test_call_and_lambda_span(self):
        e = parse_expression("f(x => x -> int)")
        self.assertEqual((e.col_offset, e.end_col_offset), (0, 16))
        self.assertEqual((e.args[0].col_offset, e.args[0].end_col_offset), (2, 15))

    def test_types_have_no_position(self):
        self.assertFalse(hasattr(parse_type("int"), "lineno"))


class MemoizationTest(unittest.TestCase):
    def test_no_arrow_is_part_of_the_memo_key(self):
        # 같은 위치를 람다 본문(no_arrow)과 일반 문맥에서 모두 읽어도 결과가 섞이지 않는다.
        self.assertEqual(parse_sx("(x => (p->v) -> int)(p->v)"),
                         "(call (lambda (x) (-> p v) -> int) (-> p v))")

    def test_parser_state_is_restored_after_error(self):
        p = aster_parser.Parser("x => p->v -> int")
        with self.assertRaises(AsterSyntaxError):
            p.run(p.eval_input)
        self.assertFalse(p.no_arrow)

    def test_prescan_finds_types_declared_later(self):
        p = aster_parser.Parser(
            "fn f() -> void { y = (Late*) q; }\n"
            "class Late {}\n"
            "typedef struct Tag { int a; } Alias;\n"
            "interface Shape { }\n"
            "from aster.time import LocalTime;\n"
            "import aster.lang.String.*;\n")
        self.assertTrue({"Late", "Tag", "Alias", "Shape", "LocalTime"} <= p.type_names)
        self.assertNotIn("String", p.type_names)

# ============================================================ 문장, 선언, 클래스, 구조체, 제어문

import tempfile  # noqa: E402

from dev.src.asterlang.parser.parser import parse, parse_file  # noqa: E402


def stmts(source, **kw):
    return parse(source, **kw).body


class ExamplesTest(unittest.TestCase):
    """tests/grammar/valid는 통과하고 invalid는 거부된다 (문법 이탈 검사)."""

    def test_examples(self):
        self.assertEqual(check_conformance.check_examples(), [])

    def test_there_are_enough_examples(self):
        def count(kind):
            return sum(len(check_conformance.split_cases(p.read_text(encoding="utf-8")))
                       for p in (check_conformance.EXAMPLES / kind).glob("*.ast"))
        self.assertGreaterEqual(count("valid"), 40)
        self.assertGreaterEqual(count("invalid"), 80)

    def test_split_cases(self):
        self.assertEqual(check_conformance.split_cases("a;\n#---\n\n#---\nb;\n"), ["a;", "b;"])

    def test_checker_detects_a_wrong_example(self):
        # 일부러 잘못된 예제를 임시 폴더에 넣어, 검사 도구가 실제로 잡아내는지 확인한다.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "valid").mkdir()
            (root / "invalid").mkdir()
            (root / "valid" / "v.ast").write_text("int x = ;\n", encoding="utf-8")
            (root / "invalid" / "i.ast").write_text("int x = 1;\n#---\n# 주석만\n", encoding="utf-8")
            old = check_conformance.EXAMPLES
            check_conformance.EXAMPLES = root
            try:
                problems = check_conformance.check_examples()
            finally:
                check_conformance.EXAMPLES = old
        self.assertEqual(len(problems), 3)
        self.assertIn("통과해야 하는데", problems[0])
        self.assertIn("거부되어야 하는데", problems[1])
        self.assertIn("코드가 없는", problems[2])


class DeclarationTest(unittest.TestCase):
    def test_var_decl_forms(self):
        a, b, c, p, s = stmts("int a = 3; int b; int? c = null; Point* p = new Point(1, 2); struct Point s;")
        self.assertIsInstance(a.init, ast.ExprInit)
        self.assertIsInstance(b.init, ast.NoInit)
        self.assertEqual([ty(x.ty) for x in (a, b, c, p, s)],
                         ["int", "int", "int?", "Point*", "struct Point"])
        self.assertEqual(sx(p.init.value), "(new Point 1 2)")

    def test_brace_initializers(self):
        a, b, c, d = stmts("P a = {10, 20}; P b = {.y = 20, .x = 10}; P c = {0}; P d = {};")
        self.assertIsInstance(a.init, ast.PositionalInit)
        self.assertEqual([sx(v) for v in a.init.values], ["10", "20"])
        self.assertIsInstance(b.init, ast.DesignatedInit)
        self.assertEqual([(i.field, sx(i.value)) for i in b.init.items], [("y", "20"), ("x", "10")])
        self.assertEqual([sx(v) for v in c.init.values], ["0"])
        self.assertEqual((type(d.init), d.init.values), (ast.PositionalInit, []))

    def test_declaration_versus_expression(self):
        kinds = [type(s).__name__ for s in stmts(
            "a * b; a = b * c; f(x); p->f(); a.b = 1; *p = 2; int x; Box<int> y; x < y;")]
        self.assertEqual(kinds, ["VarDecl", "Assign", "ExprStmt", "ExprStmt", "Assign", "Assign",
                                 "VarDecl", "VarDecl", "ExprStmt"])
        self.assertEqual(ty(stmts("a * b;")[0].ty), "a*")           # C처럼 포인터 선언으로 읽는다

    def test_assignment_operators_and_targets(self):
        ops = stmts("a = 1; a += 1; a -= 1; a *= 1; a /= 1; a //= 1; a %= 1; a **= 1;")
        self.assertEqual([type(s.op).__name__ for s in ops],
                         ["AssignEq", "AssignAdd", "AssignSub", "AssignMult", "AssignDiv",
                          "AssignFloorDiv", "AssignMod", "AssignPow"])
        deref, deref2, attr, ptr = stmts("*p = 1; **pp = 2; a.b = 3; p->x = 4;")
        self.assertIsInstance(deref.target, ast.Deref)
        self.assertIsInstance(deref2.target.value, ast.Deref)
        self.assertIsInstance(attr.target, ast.Attribute)
        self.assertIsInstance(ptr.target, ast.PtrAttribute)

    def test_lambda_declaration_is_a_plain_assignment(self):             # [D6b]
        (s,) = stmts("add = (int a, int b) => a + b -> int;")
        self.assertIsInstance(s, ast.Assign)
        self.assertIsInstance(s.value, ast.Lambda)
        self.assertEqual(sx(s.value), "(lambda (a:int b:int) (+ a b) -> int)")

    def test_imports(self):
        a, b, c, d = stmts("import a.b.C; import a.b.*; from a.b import C; from a.b import *;")
        self.assertEqual((a.path, a.wildcard), (["a", "b", "C"], 0))
        self.assertEqual((b.path, b.wildcard), (["a", "b"], 1))
        self.assertEqual((c.module, c.name), (["a", "b"], "C"))
        self.assertEqual((d.module, d.name), (["a", "b"], None))
        self.assertEqual(parse("package aster.time; int x;").package, ["aster", "time"])
        self.assertEqual(parse("int x;").package, [])

    def test_throw_and_return(self):
        a, b, c, d, e = stmts("throw 'x'; throw 'x' as ValueError; throw ValueError; return; return 1;")
        self.assertEqual((a.exc_type, b.exc_type, c.exc_type), (None, "ValueError", None))
        self.assertEqual(sx(c.value), "ValueError")
        self.assertIsNone(d.value)
        self.assertEqual(sx(e.value), "1")


class DefinitionTest(unittest.TestCase):
    def test_function(self):
        (f,) = stmts("fn add<T>(T a, int b) -> int { return b; }")
        self.assertEqual((f.name, f.type_params), ("add", ["T"]))
        self.assertEqual([(ty(p.ty), p.name) for p in f.params], [("T", "a"), ("int", "b")])
        self.assertEqual(ty(f.returns.ty), "int")
        self.assertIsInstance(f.body[0], ast.Return)
        (g,) = stmts("fn g() -> void { }")
        self.assertIsInstance(g.returns, ast.VoidRet)
        self.assertEqual((g.params, g.type_params, g.body), ([], [], []))

    def test_type_params_make_casts_possible_only_inside(self):
        (f,) = stmts("fn conv<T>(int x) -> T { return (T) x; }")
        self.assertIsInstance(f.body[0].value, ast.Cast)
        with self.assertRaises(AsterSyntaxError):                       # T는 함수 밖에서는 타입 이름이 아니다
            parse("fn conv<T>(int x) -> T { return x; }\nfn g() -> int { return (T) x; }")
        (c,) = stmts("class Box<T> { fn f() -> T { return (T) 1; } }")
        self.assertIsInstance(c.members[0].body[0].value, ast.Cast)

    def test_class_members(self):
        (c,) = stmts("""
            class Dog<T> extends Animal<T> implements Pet, Walker<int> {
                public int a;
                private str b = "x";
                int c;
                constructor(int n) { self->a = n; }
                public fn f<U>(U u) -> void { }
                protected fn g() -> int { return 1; }
            }""")
        self.assertEqual((c.name, c.type_params), ("Dog", ["T"]))
        self.assertEqual(ty(c.base), "Animal<T>")
        self.assertEqual([ty(i) for i in c.interfaces], ["Pet", "Walker<int>"])
        a, b, cc, ctor, f, g = c.members
        self.assertIsInstance(a.access, ast.Public)
        self.assertIsInstance(b.access, ast.Private)
        self.assertEqual(sx(b.value), repr("x"))
        self.assertIsNone(cc.access)                                    # 생략하면 None (private은 의미 분석이 정한다)
        self.assertIsNone(cc.value)
        self.assertIsInstance(ctor, ast.Constructor)
        self.assertEqual([p.name for p in ctor.params], ["n"])
        self.assertEqual((f.name, f.type_params), ("f", ["U"]))
        self.assertIsInstance(g.access, ast.Protected)

    def test_property_getter_setter(self):
        (c,) = stmts("""
            class Person {
                private str _name;
                public @property
                str name;
                getter name { return self->_name; }
                setter name(str value) { self->_name = value; }
            }""")
        _, prop, getter, setter = c.members
        self.assertIsInstance(prop, ast.Property)
        self.assertEqual((ty(prop.ty), prop.name), ("str", "name"))
        self.assertIsInstance(prop.access, ast.Public)
        self.assertEqual((type(getter), getter.name), (ast.Getter, "name"))
        self.assertEqual((type(setter), setter.name, setter.param.name, ty(setter.param.ty)),
                         (ast.Setter, "name", "value", "str"))

    def test_interface(self):
        (i,) = stmts("interface Shape { fn area() -> float; public fn name(int n) -> str; }")
        a, n = i.methods
        self.assertEqual((a.name, a.params, type(a.returns), a.access), ("area", [], ast.ValueRet, None))
        self.assertIsInstance(n.access, ast.Public)
        self.assertEqual([p.name for p in n.params], ["n"])
        (e,) = stmts("interface MyInterface {}")
        self.assertEqual(e.methods, [])

    def test_struct_and_typedef(self):
        plain, td = stmts("struct P { int x; int y; };\ntypedef struct Q { int a; } QAlias;")
        self.assertEqual((plain.tag, plain.alias), ("P", None))
        self.assertEqual([(ty(m.ty), m.name) for m in plain.members], [("int", "x"), ("int", "y")])
        self.assertEqual((td.tag, td.alias), ("Q", "QAlias"))

    def test_struct_constructor(self):
        (s,) = stmts("typedef struct P { int x; P(int a): x(a) { } P() { } } P;")
        field, ctor, ctor2 = s.members
        self.assertIsInstance(field, ast.StructField)
        self.assertEqual([p.name for p in ctor.params], ["a"])
        self.assertEqual([(i.field, sx(i.value)) for i in ctor.inits], [("x", "a")])
        self.assertEqual(ctor2.inits, [])                               # [D15] 초기화 리스트는 선택

    def test_struct_constructor_may_use_alias_or_tag(self):
        stmts("typedef struct _N { int v; N(int x) : v(x) {} _N() {} } N;")
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse("typedef struct _N { int v; M(int x) : v(x) {} } N;")
        self.assertIn("생성자의 이름", ctx.exception.msg)
        self.assertIn("(N, _N)", ctx.exception.msg)


class ControlFlowTest(unittest.TestCase):
    def test_if_else_chain(self):
        (s,) = stmts("if (a) { x; } else if (b) { y; } else { z; }")
        self.assertEqual(len(s.body), 1)
        (nested,) = s.orelse
        self.assertIsInstance(nested, ast.If)
        self.assertEqual(sx(nested.test), "b")
        self.assertEqual(len(nested.orelse), 1)
        (plain,) = stmts("if (a) { }")
        self.assertEqual(plain.orelse, [])

    def test_loops(self):
        w, f, r, r2 = stmts("while (a < 5) { break; } for (int i = 0; i < 5; i += 1) { continue; } "
                            "for (int i; i = 1..6..2) { } for (int j; j = 0..10) { }")
        self.assertIsInstance(w.body[0], ast.Break)
        self.assertIsInstance(f, ast.For)
        self.assertIsInstance(f.init, ast.VarDecl)
        self.assertIsInstance(f.step, ast.Assign)
        self.assertIsInstance(f.body[0], ast.Continue)
        self.assertIsInstance(r, ast.ForRange)
        self.assertEqual((ty(r.ty), r.name), ("int", "i"))
        self.assertEqual([sx(x) for x in (r.iter.start, r.iter.stop, r.iter.step)], ["1", "6", "2"])
        self.assertIsNone(r2.iter.step)

    def test_c_for_forms(self):
        a, b = stmts("for (i = 0; i < 3; i += 1) { } for (int k; k < 3; k += 1) { }")
        self.assertIsInstance(a.init, ast.Assign)
        self.assertIsInstance(b.init, ast.VarDecl)
        self.assertIsInstance(b.init.init, ast.NoInit)

    def test_range_for_requires_same_name(self):
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse("for (int i; j = 1..6) { }")
        self.assertIn("'i'", ctx.exception.msg)
        self.assertIn("'j'", ctx.exception.msg)
        self.assertEqual((ctx.exception.lineno, ctx.exception.offset), (1, 12))     # 0부터 센 열: 대입한 j의 자리

    def test_try(self):
        a, b, c = stmts("try { x; } catch (ValueError e) { y; } catch (KeyError k) { } finally { z; } "
                        "try { } finally { w; } try { } catch (Exception e) { }")
        self.assertEqual([(ty(h.ty), h.name) for h in a.handlers], [("ValueError", "e"), ("KeyError", "k")])
        self.assertEqual(len(a.finalbody), 1)
        self.assertEqual((b.handlers, len(b.finalbody)), ([], 1))
        self.assertEqual((len(c.handlers), c.finalbody), (1, []))


class StatementPositionTest(unittest.TestCase):
    def test_statement_span_includes_semicolon(self):
        (d,) = stmts("int a = 3;")
        self.assertEqual((d.lineno, d.col_offset, d.end_lineno, d.end_col_offset), (1, 0, 1, 10))
        (a,) = stmts("  x += 1 ;")
        self.assertEqual((a.col_offset, a.end_col_offset), (2, 10))

    def test_block_statements_span_to_closing_brace(self):
        (f,) = stmts("fn f() -> void {\n  return;\n}")
        self.assertEqual((f.lineno, f.end_lineno, f.end_col_offset), (1, 3, 1))
        self.assertEqual((f.body[0].lineno, f.body[0].end_col_offset), (2, 9))

    def test_member_spans(self):
        (c,) = stmts("class A {\n  public int x;\n}")
        self.assertEqual((c.members[0].lineno, c.members[0].col_offset, c.members[0].end_col_offset), (2, 2, 15))


class StatementErrorTest(unittest.TestCase):
    def assertError(self, source, fragment, **kw):
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse(source, **kw)
        self.assertIn(fragment, ctx.exception.msg)
        return ctx.exception

    def test_missing_semicolon_points_at_the_end_of_the_previous_line(self):
        e = self.assertError("int x = 3\nint y;\n", "줄 끝에 ';'")
        self.assertEqual((e.lineno, e.offset), (1, 9))
        self.assertIn("^", e.format())
        e = self.assertError("fn f() -> void {\n  x = 1\n  y = 2;\n}\n", "줄 끝에 ';'")
        self.assertEqual((e.lineno, e.offset), (2, 7))

    def test_error_inside_a_nested_body_is_reported_where_it_happens(self):
        e = self.assertError("class A {\n  fn f() -> void {\n    x = ;\n  }\n}\n", "예상하지 못한 ';'")
        self.assertEqual(e.lineno, 3)

    def test_committed_errors(self):
        e = self.assertError("class A { setter name(str v) { } }", "value여야")
        self.assertEqual(e.offset, 26)
        self.assertError("typedef struct P { Q() { } } P;", "구조체 이름(P)")

    def test_semicolon_after_a_definition_is_an_error(self):
        self.assertError("fn f() -> void { };", "예상하지 못한 ';'")
        self.assertError("class A { };", "예상하지 못한 ';'")

    def test_top_level_only_allows_statements(self):
        self.assertError("{ }", "예상하지 못한 '{'")
        self.assertError("public int x;", "'public'")

    def test_lexical_errors_in_files(self):
        self.assertError("a = b && c;", "and")


class FileApiTest(unittest.TestCase):
    def test_parse_returns_module(self):
        m = parse("")
        self.assertIsInstance(m, ast.Module)
        self.assertEqual((m.package, m.body), ([], []))

    def test_parse_file_with_bom_and_crlf(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "a.ast"
            path.write_bytes("\ufeffint a = 1;\r\nint b = 2;\r\n".encode("utf-8"))
            m = parse_file(str(path))
            self.assertEqual([s.name for s in m.body], ["a", "b"])
            self.assertEqual(m.body[1].lineno, 2)

    def test_error_carries_filename(self):
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse("int x = ;", "demo.ast")
        self.assertTrue(str(ctx.exception).startswith("demo.ast:1:"))

    def test_deeply_nested_blocks(self):
        self.assertEqual(len(parse("if (x) {" * 100 + "}" * 100).body), 1)
        with self.assertRaises(AsterSyntaxError) as ctx:
            parse("if (x) {" * 5000 + "}" * 5000)
        self.assertIn("중첩", ctx.exception.msg)
        self.assertEqual(sys.getrecursionlimit(), 1000)


class WholeProgramTest(unittest.TestCase):
    PROGRAM = """
package aster.demo;
import aster.lang.String.*;

typedef struct Point {
    int x;
    int y;
    Point(int a, int b): x(a), y(b) {}
} Point;

interface Shape { fn area() -> float; }

class Counter {
    private int value = 0;
    public fn increment() -> void { self->value += 1; }
    public fn getValue() -> int { return self->value; }
}

fn clamp<T>(T v, T lo) -> T {
    if (v < lo) { return lo; } else if (v == lo) { return v; }
    return v;
}

Counter* counter = new Counter();
counter->increment();
Point* p = new Point(1, 2);
square = x => x * x -> int;
int total = 0;
for (int i; i = 1..6..2) { total += square(i); }
try {
    if (total > 100) { throw "too big" as ValueError; }
} catch (ValueError e) {
    System.stdout.println("caught", e);
} finally {
    System.stdout.println("count", counter->getValue(), total ?? 0);
}
"""

    def test_program(self):
        m = parse(self.PROGRAM, "program.ast")
        self.assertEqual(m.package, ["aster", "demo"])
        self.assertEqual([type(s).__name__ for s in m.body],
                         ["Import", "StructDef", "InterfaceDef", "ClassDef", "FunctionDef", "VarDecl",
                          "ExprStmt", "VarDecl", "Assign", "VarDecl", "ForRange", "Try"])

if __name__ == "__main__":
    unittest.main()