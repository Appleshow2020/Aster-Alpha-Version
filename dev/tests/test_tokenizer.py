import re
import unittest
from pathlib import Path

from dev.src.asterlang.parser.errors import TokenizerError
from dev.src.asterlang.parser.tokenizer import decode_string, tokenize
from dev.src.asterlang.parser.tokens import EXACT_TOKEN_TYPES, TokenType

TOKENS_FILE = Path(__file__).resolve().parents[1] / "Grammar" / "Tokens"


def toks(source):
    return [(t.type.name, t.string) for t in tokenize(source)][:-1]


def names(source):
    return [name for name, _ in toks(source)]


class GrammarConformanceTest(unittest.TestCase):
    def parse_tokens_file(self):
        order, exact = [], {}
        for line in TOKENS_FILE.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"([A-Z_]+)(?:\s+'([^']+)')?", line)
            self.assertIsNotNone(m, line)
            order.append(m.group(1))
            if m.group(2):
                exact[m.group(2)] = m.group(1)
        return order, exact

    def test_token_names_match(self):
        order, _ = self.parse_tokens_file()
        self.assertEqual(order, [t.name for t in TokenType])

    def test_exact_tokens_match(self):
        _, exact = self.parse_tokens_file()
        self.assertEqual(exact, {s: t.name for s, t in EXACT_TOKEN_TYPES.items()})


class BasicTest(unittest.TestCase):
    def test_empty_source(self):
        self.assertEqual([t.type for t in tokenize("")], [TokenType.ENDMARKER])

    def test_statement(self):
        self.assertEqual(toks("int count = 3;"), [
            ("NAME", "int"), ("NAME", "count"), ("EQUAL", "="),
            ("INT", "3"), ("SEMI", ";")])

    def test_keywords_are_names(self):
        self.assertEqual(names("if else fn class"), ["NAME"] * 4)

    def test_function_signature(self):
        self.assertEqual(names("fn add(int a) -> int {}"),
                         ["NAME", "NAME", "LPAR", "NAME", "NAME", "RPAR",
                          "ARROW", "NAME", "LBRACE", "RBRACE"])

    def test_unicode_identifier(self):
        self.assertEqual(toks("변수 = 1;")[0], ("NAME", "변수"))

    def test_positions(self):
        tokens = list(tokenize("a = 1;\n  bb"))
        self.assertEqual(tokens[0].start, (1, 0))
        self.assertEqual(tokens[3].end, (1, 6))
        self.assertEqual(tokens[4].string, "bb")
        self.assertEqual(tokens[4].start, (2, 2))
        self.assertEqual(tokens[4].end, (2, 4))
        self.assertEqual(tokens[-1].type, TokenType.ENDMARKER)

    def test_crlf_and_cr(self):
        for nl in ("\r\n", "\r", "\n"):
            tokens = list(tokenize("a" + nl + "b"))
            self.assertEqual(tokens[1].start, (2, 0))

    def test_bom_ignored(self):
        self.assertEqual(toks("\ufeffa"), [("NAME", "a")])


class OperatorTest(unittest.TestCase):
    def test_longest_match(self):
        self.assertEqual(names("**= ** *= * //= // / /= -> - -= => == = >= > <= <"),
                         ["DOUBLESTAREQUAL", "DOUBLESTAR", "STAREQUAL", "STAR",
                          "DOUBLESLASHEQUAL", "DOUBLESLASH", "SLASH", "SLASHEQUAL",
                          "ARROW", "MINUS", "MINEQUAL", "FATARROW", "EQEQUAL", "EQUAL",
                          "GREATEREQUAL", "GREATER", "LESSEQUAL", "LESS"])

    def test_no_whitespace_needed(self):
        self.assertEqual(names("a**b"), ["NAME", "DOUBLESTAR", "NAME"])
        self.assertEqual(names("a***b"), ["NAME", "DOUBLESTAR", "STAR", "NAME"])

    def test_misc_operators(self):
        self.assertEqual(names("10 |> f; a ?? b; int? x; &x; p->v; 1..6"),
                         ["INT", "PIPE", "NAME", "SEMI", "NAME", "DOUBLEQUESTION", "NAME", "SEMI",
                          "NAME", "QUESTION", "NAME", "SEMI", "AMPER", "NAME", "SEMI",
                          "NAME", "ARROW", "NAME", "SEMI", "INT", "DOTDOT", "INT"])

    def test_double_question_is_one_token(self):
        self.assertEqual(names("int??"), ["NAME", "DOUBLEQUESTION"])

    def test_no_shift_token(self):
        self.assertEqual(names("Box<Box<int>>"),
                         ["NAME", "LESS", "NAME", "LESS", "NAME", "GREATER", "GREATER"])
        self.assertEqual(names("Box<int>= x"), ["NAME", "LESS", "NAME", "GREATEREQUAL", "NAME"])

    def test_lambda(self):
        self.assertEqual(names("x => x*x -> int"),
                         ["NAME", "FATARROW", "NAME", "STAR", "NAME", "ARROW", "NAME"])

    def test_not_equal_and_pipe_are_allowed(self):
        self.assertEqual(names("a != b |> f"), ["NAME", "NOTEQUAL", "NAME", "PIPE", "NAME"])


class ForbiddenTest(unittest.TestCase):
    def assertLexError(self, source, fragment=""):
        with self.assertRaises(TokenizerError) as ctx:
            list(tokenize(source))
        self.assertIn(fragment, ctx.exception.msg)
        return ctx.exception

    def test_logical_symbols(self):
        self.assertLexError("a && b", "and")
        self.assertLexError("a || b", "or")
        self.assertLexError("!a", "not")
        self.assertLexError("a | b", "|>")

    def test_brackets(self):
        self.assertLexError("a[0]", "[")
        self.assertLexError("]", "]")

    def test_unknown_character(self):
        self.assertLexError("a $ b", "$")
        self.assertLexError("a ` b", "`")

    def test_error_position(self):
        e = self.assertLexError("int x;\n  a && b")
        self.assertEqual((e.lineno, e.offset), (2, 4))
        self.assertIn("^^", e.format())


class CommentTest(unittest.TestCase):
    def test_line_comment(self):
        self.assertEqual(names("a # comment ; ! &&\nb"), ["NAME", "NAME"])

    def test_block_comment(self):
        self.assertEqual(names("a /* x\n y */ b"), ["NAME", "NAME"])
        self.assertEqual(list(tokenize("a /* x\n y */ b"))[1].start, (2, 6))

    def test_floor_division_is_not_a_comment(self):
        self.assertEqual(names("a // b"), ["NAME", "DOUBLESLASH", "NAME"])

    def test_unterminated_block_comment(self):
        with self.assertRaises(TokenizerError):
            list(tokenize("a /* never closed"))

    def test_block_comments_do_not_nest(self):
        self.assertEqual(names("/* a /* b */ c"), ["NAME"])

    def test_hash_in_string_is_not_a_comment(self):
        self.assertEqual(toks('"#x"'), [("STRING", '"#x"')])


class NumberTest(unittest.TestCase):
    def test_int_and_float(self):
        self.assertEqual(toks("3 3.8 0.5"), [("INT", "3"), ("FLOAT", "3.8"), ("FLOAT", "0.5")])

    def test_range_is_not_float(self):
        self.assertEqual(toks("1..6..2"), [("INT", "1"), ("DOTDOT", ".."), ("INT", "6"),
                                           ("DOTDOT", ".."), ("INT", "2")])
        self.assertEqual(names("1.5..3"), ["FLOAT", "DOTDOT", "INT"])

    def test_dot_without_digit(self):
        self.assertEqual(names("1.x"), ["INT", "DOT", "NAME"])

    def test_big_int_is_one_token(self):
        self.assertEqual(toks("1" * 40), [("INT", "1" * 40)])

    def test_identifier_char_after_number(self):
        for source in ("3abc", "1_000", "2.5e3", "7_"):
            with self.subTest(source=source), self.assertRaises(TokenizerError):
                list(tokenize(source))


class StringTest(unittest.TestCase):
    def test_both_quotes(self):
        self.assertEqual(toks("\"a\" 'b'"), [("STRING", '"a"'), ("STRING", "'b'")])

    def test_other_quote_inside(self):
        self.assertEqual(toks("\"it's\" 'say \"x\"'"),
                         [("STRING", "\"it's\""), ("STRING", "'say \"x\"'")])

    def test_escaped_quote_does_not_end_string(self):
        self.assertEqual(toks(r'"a\"b"'), [("STRING", r'"a\"b"')])
        self.assertEqual(toks(r"'a\'b'"), [("STRING", r"'a\'b'")])

    def test_decode(self):
        cases = {
            r'"\n\t\r\\"': "\n\t\r\\",
            r"'\'\"'": "'\"",
            r'"\0"': "\0",
            r'"\101\7"': "A\a",
            r'"\x41"': "A",
            r'"\u0041"': "A",
            r'"\U0001F600"': "\U0001F600",
            r'"\N{GREEK SMALL LETTER ALPHA}"': "\u03b1",
            '"한글"': "한글",
            '""': "",
        }
        for raw, value in cases.items():
            with self.subTest(raw=raw):
                self.assertEqual(decode_string(raw), value)
                list(tokenize(raw))      # 토큰화도 통과해야 한다

    def test_unknown_escape(self):
        with self.assertRaises(TokenizerError) as ctx:
            list(tokenize(r'"\q"'))
        self.assertIn("\\q", ctx.exception.msg)

    def test_bad_escapes(self):
        for source in (r'"\x4"', r'"\u12"', r'"\U00110000"', r'"\N{NO SUCH NAME}"',
                       r'"\N{ABC"', r'"\N"', '"\\\n"'):
            with self.subTest(source=source), self.assertRaises(TokenizerError):
                list(tokenize(source))

    def test_unterminated(self):
        for source in ('"abc', "'abc", '"abc\ndef"', '"a\\'):
            with self.subTest(source=source), self.assertRaises(TokenizerError):
                list(tokenize(source))

    def test_decode_rejects_non_literal(self):
        with self.assertRaises(ValueError):
            decode_string("abc")


if __name__ == "__main__":
    unittest.main()