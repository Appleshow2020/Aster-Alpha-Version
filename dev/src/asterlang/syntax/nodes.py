"""AST 노드. Grammar/Aster.asdl과 1:1로 대응한다 (Tools/check_conformance.py가 검사).

CPython의 ast 모듈과 같은 모양이다.
  * 노드는 '_fields'(자식)와 '_attributes'(위치)를 가진다.
  * ASDL의 sum 타입(stmt, expr, ...)은 추상 기반 클래스이고,
    그 생성자(FunctionDef, Name, ...)는 하위 클래스다.
  * product 타입(param, handler, ...)은 AST를 직접 상속한다.
  * 필드가 없는 생성자(Break, IntT, Add, ...)는 필드 없는 노드다.

CPython과 다른 점: 생성자가 엄격하다. 모든 필드를 반드시 받고(리스트 필드는 [], 선택 필드는 None),
모르는 이름이나 초과 인자는 TypeError다. 위치(lineno 등)만 생략할 수 있다.
문법에서 벗어난 트리가 조용히 만들어지는 일을 막기 위해서다.
"""
from __future__ import annotations

from typing import Any

_POS = ("lineno", "col_offset", "end_lineno", "end_col_offset")


class AST:
    _fields: tuple[str, ...] = ()
    _attributes: tuple[str, ...] = ()

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        cls = type(self)
        if cls is AST or "_abstract" in cls.__dict__:
            raise TypeError(f"추상 노드 {cls.__name__}는 직접 만들 수 없습니다")
        names = cls._fields + cls._attributes
        if len(args) > len(names):
            raise TypeError(
                f"{cls.__name__}는 인자를 최대 {len(names)}개 받습니다 ({len(args)}개 전달)")
        for name, value in zip(names, args):
            setattr(self, name, value)
        for name, value in kwargs.items():
            if name not in names:
                raise TypeError(f"{cls.__name__}에 알 수 없는 필드 {name!r}")
            if hasattr(self, name):
                raise TypeError(f"{cls.__name__}의 {name!r}가 두 번 전달되었습니다")
            setattr(self, name, value)
        for name in cls._fields:
            if not hasattr(self, name):
                raise TypeError(f"{cls.__name__}에 필드 {name!r}가 없습니다")
        for name in cls._attributes:
            if not hasattr(self, name):
                setattr(self, name, None)

    def __repr__(self) -> str:
        return dump(self)


def dump(node: AST, indent: int | str | None = None, include_attributes: bool = False) -> str:
    """트리를 문자열로 만든다 (ast.dump와 같은 모양). 파서 테스트에서 구조를 비교할 때 쓴다."""
    if not isinstance(node, AST):
        raise TypeError(f"AST 노드가 필요합니다: {type(node).__name__}")
    pad_unit = " " * indent if isinstance(indent, int) else indent

    def wrap(name: str, open_: str, close: str, items: list[str], level: int) -> str:
        if not items:
            return f"{name}{open_}{close}"
        if pad_unit is None:
            return f"{name}{open_}{', '.join(items)}{close}"
        pad = "\n" + pad_unit * (level + 1)
        return f"{name}{open_}{pad}{(',' + pad).join(items)}{close}"

    def fmt(value: Any, level: int) -> str:
        if isinstance(value, AST):
            names = value._fields + (value._attributes if include_attributes else ())
            items = [f"{n}={fmt(getattr(value, n), level + 1)}" for n in names]
            return wrap(type(value).__name__, "(", ")", items, level)
        if isinstance(value, list):
            return wrap("", "[", "]", [fmt(v, level + 1) for v in value], level)
        return repr(value)

    return fmt(node, 0)


def _sum(name: str, attributes: tuple[str, ...] = ()) -> type[AST]:
    """ASDL의 sum 타입: 직접 만들 수 없는 기반 클래스."""
    return type(name, (AST,), {"_abstract": True, "_attributes": attributes,
                               "__module__": __name__})


def _node(name: str, base: type[AST], fields: str = "") -> type[AST]:
    """ASDL의 생성자 또는 product 타입. fields는 공백으로 구분한 필드 이름이다."""
    return type(name, (base,), {"_fields": tuple(fields.split()), "__module__": __name__})


# ---------------------------------------------------------------- 모듈과 문장

mod = _sum("mod")
Module = _node("Module", mod, "package body")

stmt = _sum("stmt", _POS)
FunctionDef = _node("FunctionDef", stmt, "name type_params params returns body")
ClassDef = _node("ClassDef", stmt, "name type_params base interfaces members")
InterfaceDef = _node("InterfaceDef", stmt, "name methods")
StructDef = _node("StructDef", stmt, "tag alias members")
VarDecl = _node("VarDecl", stmt, "ty name init")
Assign = _node("Assign", stmt, "target op value")
If = _node("If", stmt, "test body orelse")
While = _node("While", stmt, "test body")
For = _node("For", stmt, "init test step body")
ForRange = _node("ForRange", stmt, "ty name iter body")
Try = _node("Try", stmt, "body handlers finalbody")
Return = _node("Return", stmt, "value")
Throw = _node("Throw", stmt, "value exc_type")
Break = _node("Break", stmt)
Continue = _node("Continue", stmt)
Import = _node("Import", stmt, "path wildcard")
ImportFrom = _node("ImportFrom", stmt, "module name")
ExprStmt = _node("ExprStmt", stmt, "value")

# ---------------------------------------------------------------------- 식

expr = _sum("expr", _POS)
Lambda = _node("Lambda", expr, "params returns body")
Pipe = _node("Pipe", expr, "value func")
Coalesce = _node("Coalesce", expr, "left right")
BoolOp = _node("BoolOp", expr, "op left right")
UnaryOp = _node("UnaryOp", expr, "op operand")
BinOp = _node("BinOp", expr, "left op right")
Compare = _node("Compare", expr, "left op right")
Is = _node("Is", expr, "value ty")
In = _node("In", expr, "item container")
Cast = _node("Cast", expr, "ty value")
AddressOf = _node("AddressOf", expr, "value")
Deref = _node("Deref", expr, "value")
SizeOf = _node("SizeOf", expr, "ty")
Call = _node("Call", expr, "func args")
New = _node("New", expr, "cls type_args args")
Attribute = _node("Attribute", expr, "value attr")
PtrAttribute = _node("PtrAttribute", expr, "value attr")
Name = _node("Name", expr, "id")
Self = _node("Self", expr)
Constant = _node("Constant", expr, "value")

# ---------------------------------------------------------------------- 타입

typeref = _sum("typeref")
Primitive = _node("Primitive", typeref, "kind")
Named = _node("Named", typeref, "name args")
StructRef = _node("StructRef", typeref, "tag")
Pointer = _node("Pointer", typeref, "base")
Nullable = _node("Nullable", typeref, "base")

prim = _sum("prim")
IntT = _node("IntT", prim)
FloatT = _node("FloatT", prim)
BoolT = _node("BoolT", prim)
StrT = _node("StrT", prim)
NullT = _node("NullT", prim)

ret = _sum("ret")
VoidRet = _node("VoidRet", ret)
ValueRet = _node("ValueRet", ret, "ty")

# ------------------------------------------------------ product 타입과 초기화

param = _node("param", AST, "ty name")
lparam = _node("lparam", AST, "ty name")
signature = _node("signature", AST, "access name params returns")
handler = _node("handler", AST, "ty name body")
range_expr = _node("range_expr", AST, "start stop step")

initializer = _sum("initializer")
NoInit = _node("NoInit", initializer)
ExprInit = _node("ExprInit", initializer, "value")
PositionalInit = _node("PositionalInit", initializer, "values")
DesignatedInit = _node("DesignatedInit", initializer, "items")
designation = _node("designation", AST, "field value")

# ------------------------------------------------------- 클래스와 구조체 멤버

access = _sum("access")
Public = _node("Public", access)
Protected = _node("Protected", access)
Private = _node("Private", access)
Internal = _node("Internal", access)

member = _sum("member", _POS)
Field = _node("Field", member, "access ty name value")
Method = _node("Method", member, "access name type_params params returns body")
Constructor = _node("Constructor", member, "params body")
Property = _node("Property", member, "access ty name")
Getter = _node("Getter", member, "name body")
Setter = _node("Setter", member, "name param body")

struct_member = _sum("struct_member")
StructField = _node("StructField", struct_member, "ty name")
StructCtor = _node("StructCtor", struct_member, "params inits body")
member_init = _node("member_init", AST, "field value")

# -------------------------------------------------------------------- 연산자

boolop = _sum("boolop")
And = _node("And", boolop)
Or = _node("Or", boolop)

unaryop = _sum("unaryop")
UAdd = _node("UAdd", unaryop)
USub = _node("USub", unaryop)
Not = _node("Not", unaryop)

operator = _sum("operator")
Add = _node("Add", operator)
Sub = _node("Sub", operator)
Mult = _node("Mult", operator)
Div = _node("Div", operator)
FloorDiv = _node("FloorDiv", operator)
Mod = _node("Mod", operator)
Pow = _node("Pow", operator)

cmpop = _sum("cmpop")
Eq = _node("Eq", cmpop)
NotEq = _node("NotEq", cmpop)
Lt = _node("Lt", cmpop)
LtE = _node("LtE", cmpop)
Gt = _node("Gt", cmpop)
GtE = _node("GtE", cmpop)

assign_op = _sum("assign_op")
AssignEq = _node("AssignEq", assign_op)
AssignAdd = _node("AssignAdd", assign_op)
AssignSub = _node("AssignSub", assign_op)
AssignMult = _node("AssignMult", assign_op)
AssignDiv = _node("AssignDiv", assign_op)
AssignFloorDiv = _node("AssignFloorDiv", assign_op)
AssignMod = _node("AssignMod", assign_op)
AssignPow = _node("AssignPow", assign_op)