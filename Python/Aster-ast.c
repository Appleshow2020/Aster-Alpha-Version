/* 자동 생성: Parser/asdl_c.py (원본: Parser/Aster.asdl). 직접 고치지 마세요. */
#include <stdio.h>
#include <stdlib.h>
#include "internal/ast.h"
#include "internal/object.h"

static void missing(const char *field, const char *node) {
    fprintf(stderr, "내부 오류: %s의 필수 필드 '%s'가 비어 있습니다\n", node, field);
    abort();
}

mod_ty _Aster_Module(asdl_seq *package, asdl_seq *body, Arena *arena) {
    mod_ty p;
    if (!package) missing("package", "Module");
    if (!body) missing("body", "Module");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Module_kind;
    p->v.Module.package = package;
    p->v.Module.body = body;
    return p;
}

stmt_ty _Aster_FunctionDef(identifier name, asdl_seq *type_params, asdl_seq *params, ret_ty returns, asdl_seq *body, int is_extern, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!name) missing("name", "FunctionDef");
    if (!type_params) missing("type_params", "FunctionDef");
    if (!params) missing("params", "FunctionDef");
    if (!returns) missing("returns", "FunctionDef");
    if (!body) missing("body", "FunctionDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = FunctionDef_kind;
    p->v.FunctionDef.name = name;
    p->v.FunctionDef.type_params = type_params;
    p->v.FunctionDef.params = params;
    p->v.FunctionDef.returns = returns;
    p->v.FunctionDef.body = body;
    p->v.FunctionDef.is_extern = is_extern;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ClassDef(identifier name, asdl_seq *type_params, typeref_ty base, asdl_seq *interfaces, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!name) missing("name", "ClassDef");
    if (!type_params) missing("type_params", "ClassDef");
    if (!interfaces) missing("interfaces", "ClassDef");
    if (!members) missing("members", "ClassDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ClassDef_kind;
    p->v.ClassDef.name = name;
    p->v.ClassDef.type_params = type_params;
    p->v.ClassDef.base = base;
    p->v.ClassDef.interfaces = interfaces;
    p->v.ClassDef.members = members;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_InterfaceDef(identifier name, asdl_seq *methods, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!name) missing("name", "InterfaceDef");
    if (!methods) missing("methods", "InterfaceDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = InterfaceDef_kind;
    p->v.InterfaceDef.name = name;
    p->v.InterfaceDef.methods = methods;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_StructDef(identifier tag, identifier alias, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!tag) missing("tag", "StructDef");
    if (!members) missing("members", "StructDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = StructDef_kind;
    p->v.StructDef.tag = tag;
    p->v.StructDef.alias = alias;
    p->v.StructDef.members = members;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_VarDecl(typeref_ty ty, identifier name, initializer_ty init, int is_const, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!ty) missing("ty", "VarDecl");
    if (!name) missing("name", "VarDecl");
    if (!init) missing("init", "VarDecl");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = VarDecl_kind;
    p->v.VarDecl.ty = ty;
    p->v.VarDecl.name = name;
    p->v.VarDecl.init = init;
    p->v.VarDecl.is_const = is_const;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_EnumDef(identifier name, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!name) missing("name", "EnumDef");
    if (!members) missing("members", "EnumDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = EnumDef_kind;
    p->v.EnumDef.name = name;
    p->v.EnumDef.members = members;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Destructure(asdl_seq *names, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!names) missing("names", "Destructure");
    if (!value) missing("value", "Destructure");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Destructure_kind;
    p->v.Destructure.names = names;
    p->v.Destructure.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Assert(expr_ty test, expr_ty message, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!test) missing("test", "Assert");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Assert_kind;
    p->v.Assert.test = test;
    p->v.Assert.message = message;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Assign(expr_ty target, assign_op_ty op, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!target) missing("target", "Assign");
    if (!op) missing("op", "Assign");
    if (!value) missing("value", "Assign");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Assign_kind;
    p->v.Assign.target = target;
    p->v.Assign.op = op;
    p->v.Assign.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_If(expr_ty test, asdl_seq *body, asdl_seq *orelse, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!test) missing("test", "If");
    if (!body) missing("body", "If");
    if (!orelse) missing("orelse", "If");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = If_kind;
    p->v.If.test = test;
    p->v.If.body = body;
    p->v.If.orelse = orelse;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_While(expr_ty test, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!test) missing("test", "While");
    if (!body) missing("body", "While");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = While_kind;
    p->v.While.test = test;
    p->v.While.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_For(stmt_ty init, expr_ty test, stmt_ty step, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!init) missing("init", "For");
    if (!test) missing("test", "For");
    if (!step) missing("step", "For");
    if (!body) missing("body", "For");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = For_kind;
    p->v.For.init = init;
    p->v.For.test = test;
    p->v.For.step = step;
    p->v.For.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ForRange(typeref_ty ty, identifier name, range_expr_ty iter, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!ty) missing("ty", "ForRange");
    if (!name) missing("name", "ForRange");
    if (!iter) missing("iter", "ForRange");
    if (!body) missing("body", "ForRange");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ForRange_kind;
    p->v.ForRange.ty = ty;
    p->v.ForRange.name = name;
    p->v.ForRange.iter = iter;
    p->v.ForRange.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ForIn(typeref_ty ty, identifier name, expr_ty iter, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!ty) missing("ty", "ForIn");
    if (!name) missing("name", "ForIn");
    if (!iter) missing("iter", "ForIn");
    if (!body) missing("body", "ForIn");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ForIn_kind;
    p->v.ForIn.ty = ty;
    p->v.ForIn.name = name;
    p->v.ForIn.iter = iter;
    p->v.ForIn.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Switch(expr_ty subject, asdl_seq *cases, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!subject) missing("subject", "Switch");
    if (!cases) missing("cases", "Switch");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Switch_kind;
    p->v.Switch.subject = subject;
    p->v.Switch.cases = cases;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Try(asdl_seq *body, asdl_seq *handlers, asdl_seq *finalbody, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!body) missing("body", "Try");
    if (!handlers) missing("handlers", "Try");
    if (!finalbody) missing("finalbody", "Try");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Try_kind;
    p->v.Try.body = body;
    p->v.Try.handlers = handlers;
    p->v.Try.finalbody = finalbody;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Yield(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Yield_kind;
    p->v.Yield.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Return(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Return_kind;
    p->v.Return.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Throw(expr_ty value, identifier exc_type, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!value) missing("value", "Throw");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Throw_kind;
    p->v.Throw.value = value;
    p->v.Throw.exc_type = exc_type;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Break(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Break_kind;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Continue(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Continue_kind;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Import(asdl_seq *path, int wildcard, identifier alias, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!path) missing("path", "Import");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Import_kind;
    p->v.Import.path = path;
    p->v.Import.wildcard = wildcard;
    p->v.Import.alias = alias;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ImportFrom(asdl_seq *module, identifier name, identifier alias, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!module) missing("module", "ImportFrom");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ImportFrom_kind;
    p->v.ImportFrom.module = module;
    p->v.ImportFrom.name = name;
    p->v.ImportFrom.alias = alias;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_Export(stmt_ty body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!body) missing("body", "Export");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Export_kind;
    p->v.Export.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ExceptionDef(identifier name, identifier base, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!name) missing("name", "ExceptionDef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ExceptionDef_kind;
    p->v.ExceptionDef.name = name;
    p->v.ExceptionDef.base = base;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

stmt_ty _Aster_ExprStmt(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    stmt_ty p;
    if (!value) missing("value", "ExprStmt");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ExprStmt_kind;
    p->v.ExprStmt.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Lambda(asdl_seq *params, ret_ty returns, expr_ty body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!params) missing("params", "Lambda");
    if (!returns) missing("returns", "Lambda");
    if (!body) missing("body", "Lambda");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Lambda_kind;
    p->v.Lambda.params = params;
    p->v.Lambda.returns = returns;
    p->v.Lambda.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Pipe(expr_ty value, expr_ty func, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Pipe");
    if (!func) missing("func", "Pipe");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Pipe_kind;
    p->v.Pipe.value = value;
    p->v.Pipe.func = func;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Coalesce(expr_ty left, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!left) missing("left", "Coalesce");
    if (!right) missing("right", "Coalesce");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Coalesce_kind;
    p->v.Coalesce.left = left;
    p->v.Coalesce.right = right;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_BoolOp(boolop_ty op, expr_ty left, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!op) missing("op", "BoolOp");
    if (!left) missing("left", "BoolOp");
    if (!right) missing("right", "BoolOp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = BoolOp_kind;
    p->v.BoolOp.op = op;
    p->v.BoolOp.left = left;
    p->v.BoolOp.right = right;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_UnaryOp(unaryop_ty op, expr_ty operand, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!op) missing("op", "UnaryOp");
    if (!operand) missing("operand", "UnaryOp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = UnaryOp_kind;
    p->v.UnaryOp.op = op;
    p->v.UnaryOp.operand = operand;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_BinOp(expr_ty left, operator_ty op, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!left) missing("left", "BinOp");
    if (!op) missing("op", "BinOp");
    if (!right) missing("right", "BinOp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = BinOp_kind;
    p->v.BinOp.left = left;
    p->v.BinOp.op = op;
    p->v.BinOp.right = right;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Compare(expr_ty left, cmpop_ty op, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!left) missing("left", "Compare");
    if (!op) missing("op", "Compare");
    if (!right) missing("right", "Compare");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Compare_kind;
    p->v.Compare.left = left;
    p->v.Compare.op = op;
    p->v.Compare.right = right;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Is(expr_ty value, typeref_ty ty, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Is");
    if (!ty) missing("ty", "Is");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Is_kind;
    p->v.Is.value = value;
    p->v.Is.ty = ty;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_In(expr_ty item, expr_ty container, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!item) missing("item", "In");
    if (!container) missing("container", "In");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = In_kind;
    p->v.In.item = item;
    p->v.In.container = container;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Cast(typeref_ty ty, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!ty) missing("ty", "Cast");
    if (!value) missing("value", "Cast");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Cast_kind;
    p->v.Cast.ty = ty;
    p->v.Cast.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_AddressOf(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "AddressOf");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = AddressOf_kind;
    p->v.AddressOf.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Deref(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Deref");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Deref_kind;
    p->v.Deref.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_SizeOf(typeref_ty ty, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!ty) missing("ty", "SizeOf");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = SizeOf_kind;
    p->v.SizeOf.ty = ty;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Call(expr_ty func, asdl_seq *args, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!func) missing("func", "Call");
    if (!args) missing("args", "Call");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Call_kind;
    p->v.Call.func = func;
    p->v.Call.args = args;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_New(identifier cls, asdl_seq *type_args, asdl_seq *args, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!cls) missing("cls", "New");
    if (!type_args) missing("type_args", "New");
    if (!args) missing("args", "New");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = New_kind;
    p->v.New.cls = cls;
    p->v.New.type_args = type_args;
    p->v.New.args = args;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Attribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Attribute");
    if (!attr) missing("attr", "Attribute");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Attribute_kind;
    p->v.Attribute.value = value;
    p->v.Attribute.attr = attr;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_PtrAttribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "PtrAttribute");
    if (!attr) missing("attr", "PtrAttribute");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = PtrAttribute_kind;
    p->v.PtrAttribute.value = value;
    p->v.PtrAttribute.attr = attr;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_OptionalAttribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "OptionalAttribute");
    if (!attr) missing("attr", "OptionalAttribute");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = OptionalAttribute_kind;
    p->v.OptionalAttribute.value = value;
    p->v.OptionalAttribute.attr = attr;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Name(identifier id, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!id) missing("id", "Name");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Name_kind;
    p->v.Name.id = id;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Self(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Self_kind;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Constant(Value value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Constant_kind;
    p->v.Constant.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Array(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elements) missing("elements", "Array");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Array_kind;
    p->v.Array.elements = elements;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Range(expr_ty start, expr_ty stop, expr_ty step, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!start) missing("start", "Range");
    if (!stop) missing("stop", "Range");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Range_kind;
    p->v.Range.start = start;
    p->v.Range.stop = stop;
    p->v.Range.step = step;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Subscript(expr_ty value, expr_ty index, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Subscript");
    if (!index) missing("index", "Subscript");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Subscript_kind;
    p->v.Subscript.value = value;
    p->v.Subscript.index = index;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Slice(expr_ty lower, expr_ty upper, expr_ty step, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Slice_kind;
    p->v.Slice.lower = lower;
    p->v.Slice.upper = upper;
    p->v.Slice.step = step;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_FString(asdl_seq *values, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!values) missing("values", "FString");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = FString_kind;
    p->v.FString.values = values;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_FormattedValue(expr_ty value, int conversion, expr_ty format_spec, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "FormattedValue");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = FormattedValue_kind;
    p->v.FormattedValue.value = value;
    p->v.FormattedValue.conversion = conversion;
    p->v.FormattedValue.format_spec = format_spec;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_List(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elements) missing("elements", "List");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = List_kind;
    p->v.List.elements = elements;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Tuple(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elements) missing("elements", "Tuple");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Tuple_kind;
    p->v.Tuple.elements = elements;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Dict(asdl_seq *keys, asdl_seq *values, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!keys) missing("keys", "Dict");
    if (!values) missing("values", "Dict");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Dict_kind;
    p->v.Dict.keys = keys;
    p->v.Dict.values = values;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Set(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elements) missing("elements", "Set");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Set_kind;
    p->v.Set.elements = elements;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Spread(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!value) missing("value", "Spread");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Spread_kind;
    p->v.Spread.value = value;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_ListComp(expr_ty elt, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elt) missing("elt", "ListComp");
    if (!name) missing("name", "ListComp");
    if (!iter) missing("iter", "ListComp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ListComp_kind;
    p->v.ListComp.elt = elt;
    p->v.ListComp.name = name;
    p->v.ListComp.iter = iter;
    p->v.ListComp.cond = cond;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_SetComp(expr_ty elt, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!elt) missing("elt", "SetComp");
    if (!name) missing("name", "SetComp");
    if (!iter) missing("iter", "SetComp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = SetComp_kind;
    p->v.SetComp.elt = elt;
    p->v.SetComp.name = name;
    p->v.SetComp.iter = iter;
    p->v.SetComp.cond = cond;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_DictComp(expr_ty key, expr_ty value, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!key) missing("key", "DictComp");
    if (!value) missing("value", "DictComp");
    if (!name) missing("name", "DictComp");
    if (!iter) missing("iter", "DictComp");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = DictComp_kind;
    p->v.DictComp.key = key;
    p->v.DictComp.value = value;
    p->v.DictComp.name = name;
    p->v.DictComp.iter = iter;
    p->v.DictComp.cond = cond;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

expr_ty _Aster_Incr(expr_ty target, int is_inc, int is_prefix, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    expr_ty p;
    if (!target) missing("target", "Incr");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Incr_kind;
    p->v.Incr.target = target;
    p->v.Incr.is_inc = is_inc;
    p->v.Incr.is_prefix = is_prefix;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

typeref_ty _Aster_Primitive(prim_ty kind, Arena *arena) {
    typeref_ty p;
    if (!kind) missing("kind", "Primitive");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Primitive_kind;
    p->v.Primitive.kind = kind;
    return p;
}

typeref_ty _Aster_Named(identifier name, asdl_seq *args, Arena *arena) {
    typeref_ty p;
    if (!name) missing("name", "Named");
    if (!args) missing("args", "Named");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Named_kind;
    p->v.Named.name = name;
    p->v.Named.args = args;
    return p;
}

typeref_ty _Aster_StructRef(identifier tag, Arena *arena) {
    typeref_ty p;
    if (!tag) missing("tag", "StructRef");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = StructRef_kind;
    p->v.StructRef.tag = tag;
    return p;
}

typeref_ty _Aster_Pointer(typeref_ty base, Arena *arena) {
    typeref_ty p;
    if (!base) missing("base", "Pointer");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Pointer_kind;
    p->v.Pointer.base = base;
    return p;
}

typeref_ty _Aster_Nullable(typeref_ty base, Arena *arena) {
    typeref_ty p;
    if (!base) missing("base", "Nullable");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Nullable_kind;
    p->v.Nullable.base = base;
    return p;
}

typeref_ty _Aster_UnionTy(asdl_seq *members, Arena *arena) {
    typeref_ty p;
    if (!members) missing("members", "UnionTy");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = UnionTy_kind;
    p->v.UnionTy.members = members;
    return p;
}

typeref_ty _Aster_FunctionTy(asdl_seq *params, ret_ty returns, Arena *arena) {
    typeref_ty p;
    if (!params) missing("params", "FunctionTy");
    if (!returns) missing("returns", "FunctionTy");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = FunctionTy_kind;
    p->v.FunctionTy.params = params;
    p->v.FunctionTy.returns = returns;
    return p;
}

ret_ty _Aster_VoidRet(Arena *arena) {
    ret_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = VoidRet_kind;
    return p;
}

ret_ty _Aster_ValueRet(typeref_ty ty, Arena *arena) {
    ret_ty p;
    if (!ty) missing("ty", "ValueRet");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ValueRet_kind;
    p->v.ValueRet.ty = ty;
    return p;
}

param_ty _Aster_param(typeref_ty ty, identifier name, Arena *arena) {
    param_ty p;
    if (!ty) missing("ty", "param");
    if (!name) missing("name", "param");
    p = Arena_Alloc(arena, sizeof(*p));
    p->ty = ty;
    p->name = name;
    return p;
}

lparam_ty _Aster_lparam(typeref_ty ty, identifier name, Arena *arena) {
    lparam_ty p;
    if (!name) missing("name", "lparam");
    p = Arena_Alloc(arena, sizeof(*p));
    p->ty = ty;
    p->name = name;
    return p;
}

signature_ty _Aster_signature(access_ty access, identifier name, asdl_seq *params, ret_ty returns, Arena *arena) {
    signature_ty p;
    if (!name) missing("name", "signature");
    if (!params) missing("params", "signature");
    if (!returns) missing("returns", "signature");
    p = Arena_Alloc(arena, sizeof(*p));
    p->access = access;
    p->name = name;
    p->params = params;
    p->returns = returns;
    return p;
}

handler_ty _Aster_handler(typeref_ty ty, identifier name, asdl_seq *body, Arena *arena) {
    handler_ty p;
    if (!ty) missing("ty", "handler");
    if (!name) missing("name", "handler");
    if (!body) missing("body", "handler");
    p = Arena_Alloc(arena, sizeof(*p));
    p->ty = ty;
    p->name = name;
    p->body = body;
    return p;
}

range_expr_ty _Aster_range_expr(expr_ty start, expr_ty stop, expr_ty step, Arena *arena) {
    range_expr_ty p;
    if (!start) missing("start", "range_expr");
    if (!stop) missing("stop", "range_expr");
    p = Arena_Alloc(arena, sizeof(*p));
    p->start = start;
    p->stop = stop;
    p->step = step;
    return p;
}

enum_member_ty _Aster_enum_member(identifier name, asdl_seq *params, expr_ty value, Arena *arena) {
    enum_member_ty p;
    if (!name) missing("name", "enum_member");
    if (!params) missing("params", "enum_member");
    p = Arena_Alloc(arena, sizeof(*p));
    p->name = name;
    p->params = params;
    p->value = value;
    return p;
}

match_case_ty _Aster_match_case(match_pattern_ty pattern, asdl_seq *body, Arena *arena) {
    match_case_ty p;
    if (!pattern) missing("pattern", "match_case");
    if (!body) missing("body", "match_case");
    p = Arena_Alloc(arena, sizeof(*p));
    p->pattern = pattern;
    p->body = body;
    return p;
}

match_pattern_ty _Aster_PLiteral(expr_ty value, Arena *arena) {
    match_pattern_ty p;
    if (!value) missing("value", "PLiteral");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = PLiteral_kind;
    p->v.PLiteral.value = value;
    return p;
}

match_pattern_ty _Aster_PVariant(expr_ty path, asdl_seq *items, Arena *arena) {
    match_pattern_ty p;
    if (!path) missing("path", "PVariant");
    if (!items) missing("items", "PVariant");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = PVariant_kind;
    p->v.PVariant.path = path;
    p->v.PVariant.items = items;
    return p;
}

match_pattern_ty _Aster_PBind(identifier name, Arena *arena) {
    match_pattern_ty p;
    if (!name) missing("name", "PBind");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = PBind_kind;
    p->v.PBind.name = name;
    return p;
}

initializer_ty _Aster_NoInit(Arena *arena) {
    initializer_ty p;
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = NoInit_kind;
    return p;
}

initializer_ty _Aster_ExprInit(expr_ty value, Arena *arena) {
    initializer_ty p;
    if (!value) missing("value", "ExprInit");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = ExprInit_kind;
    p->v.ExprInit.value = value;
    return p;
}

initializer_ty _Aster_PositionalInit(asdl_seq *values, Arena *arena) {
    initializer_ty p;
    if (!values) missing("values", "PositionalInit");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = PositionalInit_kind;
    p->v.PositionalInit.values = values;
    return p;
}

initializer_ty _Aster_DesignatedInit(asdl_seq *items, Arena *arena) {
    initializer_ty p;
    if (!items) missing("items", "DesignatedInit");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = DesignatedInit_kind;
    p->v.DesignatedInit.items = items;
    return p;
}

designation_ty _Aster_designation(identifier field, expr_ty value, Arena *arena) {
    designation_ty p;
    if (!field) missing("field", "designation");
    if (!value) missing("value", "designation");
    p = Arena_Alloc(arena, sizeof(*p));
    p->field = field;
    p->value = value;
    return p;
}

member_ty _Aster_Field(access_ty access, typeref_ty ty, identifier name, expr_ty value, int is_static, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!ty) missing("ty", "Field");
    if (!name) missing("name", "Field");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Field_kind;
    p->v.Field.access = access;
    p->v.Field.ty = ty;
    p->v.Field.name = name;
    p->v.Field.value = value;
    p->v.Field.is_static = is_static;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

member_ty _Aster_Method(access_ty access, identifier name, asdl_seq *type_params, asdl_seq *params, ret_ty returns, asdl_seq *body, int is_static, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!name) missing("name", "Method");
    if (!type_params) missing("type_params", "Method");
    if (!params) missing("params", "Method");
    if (!returns) missing("returns", "Method");
    if (!body) missing("body", "Method");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Method_kind;
    p->v.Method.access = access;
    p->v.Method.name = name;
    p->v.Method.type_params = type_params;
    p->v.Method.params = params;
    p->v.Method.returns = returns;
    p->v.Method.body = body;
    p->v.Method.is_static = is_static;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

member_ty _Aster_Constructor(asdl_seq *params, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!params) missing("params", "Constructor");
    if (!body) missing("body", "Constructor");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Constructor_kind;
    p->v.Constructor.params = params;
    p->v.Constructor.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

member_ty _Aster_Property(access_ty access, typeref_ty ty, identifier name, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!ty) missing("ty", "Property");
    if (!name) missing("name", "Property");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Property_kind;
    p->v.Property.access = access;
    p->v.Property.ty = ty;
    p->v.Property.name = name;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

member_ty _Aster_Getter(identifier name, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!name) missing("name", "Getter");
    if (!body) missing("body", "Getter");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Getter_kind;
    p->v.Getter.name = name;
    p->v.Getter.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

member_ty _Aster_Setter(identifier name, param_ty param, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena) {
    member_ty p;
    if (!name) missing("name", "Setter");
    if (!param) missing("param", "Setter");
    if (!body) missing("body", "Setter");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = Setter_kind;
    p->v.Setter.name = name;
    p->v.Setter.param = param;
    p->v.Setter.body = body;
    p->lineno = lineno;
    p->col_offset = col_offset;
    p->end_lineno = end_lineno;
    p->end_col_offset = end_col_offset;
    return p;
}

struct_member_ty _Aster_StructField(typeref_ty ty, identifier name, Arena *arena) {
    struct_member_ty p;
    if (!ty) missing("ty", "StructField");
    if (!name) missing("name", "StructField");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = StructField_kind;
    p->v.StructField.ty = ty;
    p->v.StructField.name = name;
    return p;
}

struct_member_ty _Aster_StructCtor(asdl_seq *params, asdl_seq *inits, asdl_seq *body, Arena *arena) {
    struct_member_ty p;
    if (!params) missing("params", "StructCtor");
    if (!inits) missing("inits", "StructCtor");
    if (!body) missing("body", "StructCtor");
    p = Arena_Alloc(arena, sizeof(*p));
    p->kind = StructCtor_kind;
    p->v.StructCtor.params = params;
    p->v.StructCtor.inits = inits;
    p->v.StructCtor.body = body;
    return p;
}

member_init_ty _Aster_member_init(identifier field, expr_ty value, Arena *arena) {
    member_init_ty p;
    if (!field) missing("field", "member_init");
    if (!value) missing("value", "member_init");
    p = Arena_Alloc(arena, sizeof(*p));
    p->field = field;
    p->value = value;
    return p;
}


/* ---- 덤프 ---- */
static void dump_identifier(FILE *out, identifier s) { if (!s) fputs("None", out); else fprintf(out, "'%s'", s); }
static void dump_int(FILE *out, int i) { fprintf(out, "%d", i); }
static void dump_constant(FILE *out, Value v) { Aster_Value_Repr(out, v); }
static void dumpv_identifier(FILE *out, void *p) { dump_identifier(out, (identifier)p); }
static void dump_seq(FILE *out, asdl_seq *s, void (*fn)(FILE *, void *)) {
    size_t i;
    fputc('[', out);
    for (i = 0; i < asdl_seq_LEN(s); i++) { if (i) fputs(", ", out); fn(out, asdl_seq_GET(s, i)); }
    fputc(']', out);
}

static void dump_mod(FILE *out, mod_ty n);
static inline void dumpv_mod(FILE *out, void *p) { dump_mod(out, (mod_ty)p); }
static void dump_stmt(FILE *out, stmt_ty n);
static inline void dumpv_stmt(FILE *out, void *p) { dump_stmt(out, (stmt_ty)p); }
static void dump_expr(FILE *out, expr_ty n);
static inline void dumpv_expr(FILE *out, void *p) { dump_expr(out, (expr_ty)p); }
static void dump_typeref(FILE *out, typeref_ty n);
static inline void dumpv_typeref(FILE *out, void *p) { dump_typeref(out, (typeref_ty)p); }
static void dump_prim(FILE *out, prim_ty n);
static inline void dumpv_prim(FILE *out, void *p) { dump_prim(out, (prim_ty)p); }
static void dump_ret(FILE *out, ret_ty n);
static inline void dumpv_ret(FILE *out, void *p) { dump_ret(out, (ret_ty)p); }
static void dump_param(FILE *out, param_ty n);
static inline void dumpv_param(FILE *out, void *p) { dump_param(out, (param_ty)p); }
static void dump_lparam(FILE *out, lparam_ty n);
static inline void dumpv_lparam(FILE *out, void *p) { dump_lparam(out, (lparam_ty)p); }
static void dump_signature(FILE *out, signature_ty n);
static inline void dumpv_signature(FILE *out, void *p) { dump_signature(out, (signature_ty)p); }
static void dump_handler(FILE *out, handler_ty n);
static inline void dumpv_handler(FILE *out, void *p) { dump_handler(out, (handler_ty)p); }
static void dump_range_expr(FILE *out, range_expr_ty n);
static inline void dumpv_range_expr(FILE *out, void *p) { dump_range_expr(out, (range_expr_ty)p); }
static void dump_enum_member(FILE *out, enum_member_ty n);
static inline void dumpv_enum_member(FILE *out, void *p) { dump_enum_member(out, (enum_member_ty)p); }
static void dump_match_case(FILE *out, match_case_ty n);
static inline void dumpv_match_case(FILE *out, void *p) { dump_match_case(out, (match_case_ty)p); }
static void dump_match_pattern(FILE *out, match_pattern_ty n);
static inline void dumpv_match_pattern(FILE *out, void *p) { dump_match_pattern(out, (match_pattern_ty)p); }
static void dump_initializer(FILE *out, initializer_ty n);
static inline void dumpv_initializer(FILE *out, void *p) { dump_initializer(out, (initializer_ty)p); }
static void dump_designation(FILE *out, designation_ty n);
static inline void dumpv_designation(FILE *out, void *p) { dump_designation(out, (designation_ty)p); }
static void dump_access(FILE *out, access_ty n);
static inline void dumpv_access(FILE *out, void *p) { dump_access(out, (access_ty)p); }
static void dump_member(FILE *out, member_ty n);
static inline void dumpv_member(FILE *out, void *p) { dump_member(out, (member_ty)p); }
static void dump_struct_member(FILE *out, struct_member_ty n);
static inline void dumpv_struct_member(FILE *out, void *p) { dump_struct_member(out, (struct_member_ty)p); }
static void dump_member_init(FILE *out, member_init_ty n);
static inline void dumpv_member_init(FILE *out, void *p) { dump_member_init(out, (member_init_ty)p); }
static void dump_boolop(FILE *out, boolop_ty n);
static inline void dumpv_boolop(FILE *out, void *p) { dump_boolop(out, (boolop_ty)p); }
static void dump_unaryop(FILE *out, unaryop_ty n);
static inline void dumpv_unaryop(FILE *out, void *p) { dump_unaryop(out, (unaryop_ty)p); }
static void dump_operator(FILE *out, operator_ty n);
static inline void dumpv_operator(FILE *out, void *p) { dump_operator(out, (operator_ty)p); }
static void dump_cmpop(FILE *out, cmpop_ty n);
static inline void dumpv_cmpop(FILE *out, void *p) { dump_cmpop(out, (cmpop_ty)p); }
static void dump_assign_op(FILE *out, assign_op_ty n);
static inline void dumpv_assign_op(FILE *out, void *p) { dump_assign_op(out, (assign_op_ty)p); }

static void dump_mod(FILE *out, mod_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case Module_kind:
        fputs("Module(", out);
        fputs("package=", out);
        dump_seq(out, n->v.Module.package, dumpv_identifier);
        fputs(", body=", out);
        dump_seq(out, n->v.Module.body, dumpv_stmt);
        fputc(')', out);
        break;
    }
}

static void dump_stmt(FILE *out, stmt_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case FunctionDef_kind:
        fputs("FunctionDef(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.FunctionDef.name);
        fputs(", type_params=", out);
        dump_seq(out, n->v.FunctionDef.type_params, dumpv_identifier);
        fputs(", params=", out);
        dump_seq(out, n->v.FunctionDef.params, dumpv_param);
        fputs(", returns=", out);
        dump_ret(out, n->v.FunctionDef.returns);
        fputs(", body=", out);
        dump_seq(out, n->v.FunctionDef.body, dumpv_stmt);
        fputs(", is_extern=", out);
        dump_int(out, n->v.FunctionDef.is_extern);
        fputc(')', out);
        break;
    case ClassDef_kind:
        fputs("ClassDef(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.ClassDef.name);
        fputs(", type_params=", out);
        dump_seq(out, n->v.ClassDef.type_params, dumpv_identifier);
        fputs(", base=", out);
        dump_typeref(out, n->v.ClassDef.base);
        fputs(", interfaces=", out);
        dump_seq(out, n->v.ClassDef.interfaces, dumpv_typeref);
        fputs(", members=", out);
        dump_seq(out, n->v.ClassDef.members, dumpv_member);
        fputc(')', out);
        break;
    case InterfaceDef_kind:
        fputs("InterfaceDef(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.InterfaceDef.name);
        fputs(", methods=", out);
        dump_seq(out, n->v.InterfaceDef.methods, dumpv_signature);
        fputc(')', out);
        break;
    case StructDef_kind:
        fputs("StructDef(", out);
        fputs("tag=", out);
        dump_identifier(out, n->v.StructDef.tag);
        fputs(", alias=", out);
        dump_identifier(out, n->v.StructDef.alias);
        fputs(", members=", out);
        dump_seq(out, n->v.StructDef.members, dumpv_struct_member);
        fputc(')', out);
        break;
    case VarDecl_kind:
        fputs("VarDecl(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.VarDecl.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.VarDecl.name);
        fputs(", init=", out);
        dump_initializer(out, n->v.VarDecl.init);
        fputs(", is_const=", out);
        dump_int(out, n->v.VarDecl.is_const);
        fputc(')', out);
        break;
    case EnumDef_kind:
        fputs("EnumDef(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.EnumDef.name);
        fputs(", members=", out);
        dump_seq(out, n->v.EnumDef.members, dumpv_enum_member);
        fputc(')', out);
        break;
    case Destructure_kind:
        fputs("Destructure(", out);
        fputs("names=", out);
        dump_seq(out, n->v.Destructure.names, dumpv_identifier);
        fputs(", value=", out);
        dump_expr(out, n->v.Destructure.value);
        fputc(')', out);
        break;
    case Assert_kind:
        fputs("Assert(", out);
        fputs("test=", out);
        dump_expr(out, n->v.Assert.test);
        fputs(", message=", out);
        dump_expr(out, n->v.Assert.message);
        fputc(')', out);
        break;
    case Assign_kind:
        fputs("Assign(", out);
        fputs("target=", out);
        dump_expr(out, n->v.Assign.target);
        fputs(", op=", out);
        dump_assign_op(out, n->v.Assign.op);
        fputs(", value=", out);
        dump_expr(out, n->v.Assign.value);
        fputc(')', out);
        break;
    case If_kind:
        fputs("If(", out);
        fputs("test=", out);
        dump_expr(out, n->v.If.test);
        fputs(", body=", out);
        dump_seq(out, n->v.If.body, dumpv_stmt);
        fputs(", orelse=", out);
        dump_seq(out, n->v.If.orelse, dumpv_stmt);
        fputc(')', out);
        break;
    case While_kind:
        fputs("While(", out);
        fputs("test=", out);
        dump_expr(out, n->v.While.test);
        fputs(", body=", out);
        dump_seq(out, n->v.While.body, dumpv_stmt);
        fputc(')', out);
        break;
    case For_kind:
        fputs("For(", out);
        fputs("init=", out);
        dump_stmt(out, n->v.For.init);
        fputs(", test=", out);
        dump_expr(out, n->v.For.test);
        fputs(", step=", out);
        dump_stmt(out, n->v.For.step);
        fputs(", body=", out);
        dump_seq(out, n->v.For.body, dumpv_stmt);
        fputc(')', out);
        break;
    case ForRange_kind:
        fputs("ForRange(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.ForRange.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.ForRange.name);
        fputs(", iter=", out);
        dump_range_expr(out, n->v.ForRange.iter);
        fputs(", body=", out);
        dump_seq(out, n->v.ForRange.body, dumpv_stmt);
        fputc(')', out);
        break;
    case ForIn_kind:
        fputs("ForIn(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.ForIn.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.ForIn.name);
        fputs(", iter=", out);
        dump_expr(out, n->v.ForIn.iter);
        fputs(", body=", out);
        dump_seq(out, n->v.ForIn.body, dumpv_stmt);
        fputc(')', out);
        break;
    case Switch_kind:
        fputs("Switch(", out);
        fputs("subject=", out);
        dump_expr(out, n->v.Switch.subject);
        fputs(", cases=", out);
        dump_seq(out, n->v.Switch.cases, dumpv_match_case);
        fputc(')', out);
        break;
    case Try_kind:
        fputs("Try(", out);
        fputs("body=", out);
        dump_seq(out, n->v.Try.body, dumpv_stmt);
        fputs(", handlers=", out);
        dump_seq(out, n->v.Try.handlers, dumpv_handler);
        fputs(", finalbody=", out);
        dump_seq(out, n->v.Try.finalbody, dumpv_stmt);
        fputc(')', out);
        break;
    case Yield_kind:
        fputs("Yield(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Yield.value);
        fputc(')', out);
        break;
    case Return_kind:
        fputs("Return(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Return.value);
        fputc(')', out);
        break;
    case Throw_kind:
        fputs("Throw(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Throw.value);
        fputs(", exc_type=", out);
        dump_identifier(out, n->v.Throw.exc_type);
        fputc(')', out);
        break;
    case Break_kind:
        fputs("Break()", out);
        break;
    case Continue_kind:
        fputs("Continue()", out);
        break;
    case Import_kind:
        fputs("Import(", out);
        fputs("path=", out);
        dump_seq(out, n->v.Import.path, dumpv_identifier);
        fputs(", wildcard=", out);
        dump_int(out, n->v.Import.wildcard);
        fputs(", alias=", out);
        dump_identifier(out, n->v.Import.alias);
        fputc(')', out);
        break;
    case ImportFrom_kind:
        fputs("ImportFrom(", out);
        fputs("module=", out);
        dump_seq(out, n->v.ImportFrom.module, dumpv_identifier);
        fputs(", name=", out);
        dump_identifier(out, n->v.ImportFrom.name);
        fputs(", alias=", out);
        dump_identifier(out, n->v.ImportFrom.alias);
        fputc(')', out);
        break;
    case Export_kind:
        fputs("Export(", out);
        fputs("body=", out);
        dump_stmt(out, n->v.Export.body);
        fputc(')', out);
        break;
    case ExceptionDef_kind:
        fputs("ExceptionDef(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.ExceptionDef.name);
        fputs(", base=", out);
        dump_identifier(out, n->v.ExceptionDef.base);
        fputc(')', out);
        break;
    case ExprStmt_kind:
        fputs("ExprStmt(", out);
        fputs("value=", out);
        dump_expr(out, n->v.ExprStmt.value);
        fputc(')', out);
        break;
    }
}

static void dump_expr(FILE *out, expr_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case Lambda_kind:
        fputs("Lambda(", out);
        fputs("params=", out);
        dump_seq(out, n->v.Lambda.params, dumpv_lparam);
        fputs(", returns=", out);
        dump_ret(out, n->v.Lambda.returns);
        fputs(", body=", out);
        dump_expr(out, n->v.Lambda.body);
        fputc(')', out);
        break;
    case Pipe_kind:
        fputs("Pipe(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Pipe.value);
        fputs(", func=", out);
        dump_expr(out, n->v.Pipe.func);
        fputc(')', out);
        break;
    case Coalesce_kind:
        fputs("Coalesce(", out);
        fputs("left=", out);
        dump_expr(out, n->v.Coalesce.left);
        fputs(", right=", out);
        dump_expr(out, n->v.Coalesce.right);
        fputc(')', out);
        break;
    case BoolOp_kind:
        fputs("BoolOp(", out);
        fputs("op=", out);
        dump_boolop(out, n->v.BoolOp.op);
        fputs(", left=", out);
        dump_expr(out, n->v.BoolOp.left);
        fputs(", right=", out);
        dump_expr(out, n->v.BoolOp.right);
        fputc(')', out);
        break;
    case UnaryOp_kind:
        fputs("UnaryOp(", out);
        fputs("op=", out);
        dump_unaryop(out, n->v.UnaryOp.op);
        fputs(", operand=", out);
        dump_expr(out, n->v.UnaryOp.operand);
        fputc(')', out);
        break;
    case BinOp_kind:
        fputs("BinOp(", out);
        fputs("left=", out);
        dump_expr(out, n->v.BinOp.left);
        fputs(", op=", out);
        dump_operator(out, n->v.BinOp.op);
        fputs(", right=", out);
        dump_expr(out, n->v.BinOp.right);
        fputc(')', out);
        break;
    case Compare_kind:
        fputs("Compare(", out);
        fputs("left=", out);
        dump_expr(out, n->v.Compare.left);
        fputs(", op=", out);
        dump_cmpop(out, n->v.Compare.op);
        fputs(", right=", out);
        dump_expr(out, n->v.Compare.right);
        fputc(')', out);
        break;
    case Is_kind:
        fputs("Is(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Is.value);
        fputs(", ty=", out);
        dump_typeref(out, n->v.Is.ty);
        fputc(')', out);
        break;
    case In_kind:
        fputs("In(", out);
        fputs("item=", out);
        dump_expr(out, n->v.In.item);
        fputs(", container=", out);
        dump_expr(out, n->v.In.container);
        fputc(')', out);
        break;
    case Cast_kind:
        fputs("Cast(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.Cast.ty);
        fputs(", value=", out);
        dump_expr(out, n->v.Cast.value);
        fputc(')', out);
        break;
    case AddressOf_kind:
        fputs("AddressOf(", out);
        fputs("value=", out);
        dump_expr(out, n->v.AddressOf.value);
        fputc(')', out);
        break;
    case Deref_kind:
        fputs("Deref(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Deref.value);
        fputc(')', out);
        break;
    case SizeOf_kind:
        fputs("SizeOf(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.SizeOf.ty);
        fputc(')', out);
        break;
    case Call_kind:
        fputs("Call(", out);
        fputs("func=", out);
        dump_expr(out, n->v.Call.func);
        fputs(", args=", out);
        dump_seq(out, n->v.Call.args, dumpv_expr);
        fputc(')', out);
        break;
    case New_kind:
        fputs("New(", out);
        fputs("cls=", out);
        dump_identifier(out, n->v.New.cls);
        fputs(", type_args=", out);
        dump_seq(out, n->v.New.type_args, dumpv_typeref);
        fputs(", args=", out);
        dump_seq(out, n->v.New.args, dumpv_expr);
        fputc(')', out);
        break;
    case Attribute_kind:
        fputs("Attribute(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Attribute.value);
        fputs(", attr=", out);
        dump_identifier(out, n->v.Attribute.attr);
        fputc(')', out);
        break;
    case PtrAttribute_kind:
        fputs("PtrAttribute(", out);
        fputs("value=", out);
        dump_expr(out, n->v.PtrAttribute.value);
        fputs(", attr=", out);
        dump_identifier(out, n->v.PtrAttribute.attr);
        fputc(')', out);
        break;
    case OptionalAttribute_kind:
        fputs("OptionalAttribute(", out);
        fputs("value=", out);
        dump_expr(out, n->v.OptionalAttribute.value);
        fputs(", attr=", out);
        dump_identifier(out, n->v.OptionalAttribute.attr);
        fputc(')', out);
        break;
    case Name_kind:
        fputs("Name(", out);
        fputs("id=", out);
        dump_identifier(out, n->v.Name.id);
        fputc(')', out);
        break;
    case Self_kind:
        fputs("Self()", out);
        break;
    case Constant_kind:
        fputs("Constant(", out);
        fputs("value=", out);
        dump_constant(out, n->v.Constant.value);
        fputc(')', out);
        break;
    case Array_kind:
        fputs("Array(", out);
        fputs("elements=", out);
        dump_seq(out, n->v.Array.elements, dumpv_expr);
        fputc(')', out);
        break;
    case Range_kind:
        fputs("Range(", out);
        fputs("start=", out);
        dump_expr(out, n->v.Range.start);
        fputs(", stop=", out);
        dump_expr(out, n->v.Range.stop);
        fputs(", step=", out);
        dump_expr(out, n->v.Range.step);
        fputc(')', out);
        break;
    case Subscript_kind:
        fputs("Subscript(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Subscript.value);
        fputs(", index=", out);
        dump_expr(out, n->v.Subscript.index);
        fputc(')', out);
        break;
    case Slice_kind:
        fputs("Slice(", out);
        fputs("lower=", out);
        dump_expr(out, n->v.Slice.lower);
        fputs(", upper=", out);
        dump_expr(out, n->v.Slice.upper);
        fputs(", step=", out);
        dump_expr(out, n->v.Slice.step);
        fputc(')', out);
        break;
    case FString_kind:
        fputs("FString(", out);
        fputs("values=", out);
        dump_seq(out, n->v.FString.values, dumpv_expr);
        fputc(')', out);
        break;
    case FormattedValue_kind:
        fputs("FormattedValue(", out);
        fputs("value=", out);
        dump_expr(out, n->v.FormattedValue.value);
        fputs(", conversion=", out);
        dump_int(out, n->v.FormattedValue.conversion);
        fputs(", format_spec=", out);
        dump_expr(out, n->v.FormattedValue.format_spec);
        fputc(')', out);
        break;
    case List_kind:
        fputs("List(", out);
        fputs("elements=", out);
        dump_seq(out, n->v.List.elements, dumpv_expr);
        fputc(')', out);
        break;
    case Tuple_kind:
        fputs("Tuple(", out);
        fputs("elements=", out);
        dump_seq(out, n->v.Tuple.elements, dumpv_expr);
        fputc(')', out);
        break;
    case Dict_kind:
        fputs("Dict(", out);
        fputs("keys=", out);
        dump_seq(out, n->v.Dict.keys, dumpv_expr);
        fputs(", values=", out);
        dump_seq(out, n->v.Dict.values, dumpv_expr);
        fputc(')', out);
        break;
    case Set_kind:
        fputs("Set(", out);
        fputs("elements=", out);
        dump_seq(out, n->v.Set.elements, dumpv_expr);
        fputc(')', out);
        break;
    case Spread_kind:
        fputs("Spread(", out);
        fputs("value=", out);
        dump_expr(out, n->v.Spread.value);
        fputc(')', out);
        break;
    case ListComp_kind:
        fputs("ListComp(", out);
        fputs("elt=", out);
        dump_expr(out, n->v.ListComp.elt);
        fputs(", name=", out);
        dump_identifier(out, n->v.ListComp.name);
        fputs(", iter=", out);
        dump_expr(out, n->v.ListComp.iter);
        fputs(", cond=", out);
        dump_expr(out, n->v.ListComp.cond);
        fputc(')', out);
        break;
    case SetComp_kind:
        fputs("SetComp(", out);
        fputs("elt=", out);
        dump_expr(out, n->v.SetComp.elt);
        fputs(", name=", out);
        dump_identifier(out, n->v.SetComp.name);
        fputs(", iter=", out);
        dump_expr(out, n->v.SetComp.iter);
        fputs(", cond=", out);
        dump_expr(out, n->v.SetComp.cond);
        fputc(')', out);
        break;
    case DictComp_kind:
        fputs("DictComp(", out);
        fputs("key=", out);
        dump_expr(out, n->v.DictComp.key);
        fputs(", value=", out);
        dump_expr(out, n->v.DictComp.value);
        fputs(", name=", out);
        dump_identifier(out, n->v.DictComp.name);
        fputs(", iter=", out);
        dump_expr(out, n->v.DictComp.iter);
        fputs(", cond=", out);
        dump_expr(out, n->v.DictComp.cond);
        fputc(')', out);
        break;
    case Incr_kind:
        fputs("Incr(", out);
        fputs("target=", out);
        dump_expr(out, n->v.Incr.target);
        fputs(", is_inc=", out);
        dump_int(out, n->v.Incr.is_inc);
        fputs(", is_prefix=", out);
        dump_int(out, n->v.Incr.is_prefix);
        fputc(')', out);
        break;
    }
}

static void dump_typeref(FILE *out, typeref_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case Primitive_kind:
        fputs("Primitive(", out);
        fputs("kind=", out);
        dump_prim(out, n->v.Primitive.kind);
        fputc(')', out);
        break;
    case Named_kind:
        fputs("Named(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.Named.name);
        fputs(", args=", out);
        dump_seq(out, n->v.Named.args, dumpv_typeref);
        fputc(')', out);
        break;
    case StructRef_kind:
        fputs("StructRef(", out);
        fputs("tag=", out);
        dump_identifier(out, n->v.StructRef.tag);
        fputc(')', out);
        break;
    case Pointer_kind:
        fputs("Pointer(", out);
        fputs("base=", out);
        dump_typeref(out, n->v.Pointer.base);
        fputc(')', out);
        break;
    case Nullable_kind:
        fputs("Nullable(", out);
        fputs("base=", out);
        dump_typeref(out, n->v.Nullable.base);
        fputc(')', out);
        break;
    case UnionTy_kind:
        fputs("UnionTy(", out);
        fputs("members=", out);
        dump_seq(out, n->v.UnionTy.members, dumpv_typeref);
        fputc(')', out);
        break;
    case FunctionTy_kind:
        fputs("FunctionTy(", out);
        fputs("params=", out);
        dump_seq(out, n->v.FunctionTy.params, dumpv_typeref);
        fputs(", returns=", out);
        dump_ret(out, n->v.FunctionTy.returns);
        fputc(')', out);
        break;
    }
}

static void dump_prim(FILE *out, prim_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case IntT: fputs("IntT()", out); break;
    case FloatT: fputs("FloatT()", out); break;
    case BoolT: fputs("BoolT()", out); break;
    case StrT: fputs("StrT()", out); break;
    case NullT: fputs("NullT()", out); break;
    }
}

static void dump_ret(FILE *out, ret_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case VoidRet_kind:
        fputs("VoidRet()", out);
        break;
    case ValueRet_kind:
        fputs("ValueRet(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.ValueRet.ty);
        fputc(')', out);
        break;
    }
}

static void dump_param(FILE *out, param_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("param(", out);
    fputs("ty=", out);
    dump_typeref(out, n->ty);
    fputs(", name=", out);
    dump_identifier(out, n->name);
    fputc(')', out);
}

static void dump_lparam(FILE *out, lparam_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("lparam(", out);
    fputs("ty=", out);
    dump_typeref(out, n->ty);
    fputs(", name=", out);
    dump_identifier(out, n->name);
    fputc(')', out);
}

static void dump_signature(FILE *out, signature_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("signature(", out);
    fputs("access=", out);
    dump_access(out, n->access);
    fputs(", name=", out);
    dump_identifier(out, n->name);
    fputs(", params=", out);
    dump_seq(out, n->params, dumpv_param);
    fputs(", returns=", out);
    dump_ret(out, n->returns);
    fputc(')', out);
}

static void dump_handler(FILE *out, handler_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("handler(", out);
    fputs("ty=", out);
    dump_typeref(out, n->ty);
    fputs(", name=", out);
    dump_identifier(out, n->name);
    fputs(", body=", out);
    dump_seq(out, n->body, dumpv_stmt);
    fputc(')', out);
}

static void dump_range_expr(FILE *out, range_expr_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("range_expr(", out);
    fputs("start=", out);
    dump_expr(out, n->start);
    fputs(", stop=", out);
    dump_expr(out, n->stop);
    fputs(", step=", out);
    dump_expr(out, n->step);
    fputc(')', out);
}

static void dump_enum_member(FILE *out, enum_member_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("enum_member(", out);
    fputs("name=", out);
    dump_identifier(out, n->name);
    fputs(", params=", out);
    dump_seq(out, n->params, dumpv_param);
    fputs(", value=", out);
    dump_expr(out, n->value);
    fputc(')', out);
}

static void dump_match_case(FILE *out, match_case_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("match_case(", out);
    fputs("pattern=", out);
    dump_match_pattern(out, n->pattern);
    fputs(", body=", out);
    dump_seq(out, n->body, dumpv_stmt);
    fputc(')', out);
}

static void dump_match_pattern(FILE *out, match_pattern_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case PLiteral_kind:
        fputs("PLiteral(", out);
        fputs("value=", out);
        dump_expr(out, n->v.PLiteral.value);
        fputc(')', out);
        break;
    case PVariant_kind:
        fputs("PVariant(", out);
        fputs("path=", out);
        dump_expr(out, n->v.PVariant.path);
        fputs(", items=", out);
        dump_seq(out, n->v.PVariant.items, dumpv_match_pattern);
        fputc(')', out);
        break;
    case PBind_kind:
        fputs("PBind(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.PBind.name);
        fputc(')', out);
        break;
    }
}

static void dump_initializer(FILE *out, initializer_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case NoInit_kind:
        fputs("NoInit()", out);
        break;
    case ExprInit_kind:
        fputs("ExprInit(", out);
        fputs("value=", out);
        dump_expr(out, n->v.ExprInit.value);
        fputc(')', out);
        break;
    case PositionalInit_kind:
        fputs("PositionalInit(", out);
        fputs("values=", out);
        dump_seq(out, n->v.PositionalInit.values, dumpv_expr);
        fputc(')', out);
        break;
    case DesignatedInit_kind:
        fputs("DesignatedInit(", out);
        fputs("items=", out);
        dump_seq(out, n->v.DesignatedInit.items, dumpv_designation);
        fputc(')', out);
        break;
    }
}

static void dump_designation(FILE *out, designation_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("designation(", out);
    fputs("field=", out);
    dump_identifier(out, n->field);
    fputs(", value=", out);
    dump_expr(out, n->value);
    fputc(')', out);
}

static void dump_access(FILE *out, access_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case Public: fputs("Public()", out); break;
    case Protected: fputs("Protected()", out); break;
    case Private: fputs("Private()", out); break;
    case Internal: fputs("Internal()", out); break;
    }
}

static void dump_member(FILE *out, member_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case Field_kind:
        fputs("Field(", out);
        fputs("access=", out);
        dump_access(out, n->v.Field.access);
        fputs(", ty=", out);
        dump_typeref(out, n->v.Field.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.Field.name);
        fputs(", value=", out);
        dump_expr(out, n->v.Field.value);
        fputs(", is_static=", out);
        dump_int(out, n->v.Field.is_static);
        fputc(')', out);
        break;
    case Method_kind:
        fputs("Method(", out);
        fputs("access=", out);
        dump_access(out, n->v.Method.access);
        fputs(", name=", out);
        dump_identifier(out, n->v.Method.name);
        fputs(", type_params=", out);
        dump_seq(out, n->v.Method.type_params, dumpv_identifier);
        fputs(", params=", out);
        dump_seq(out, n->v.Method.params, dumpv_param);
        fputs(", returns=", out);
        dump_ret(out, n->v.Method.returns);
        fputs(", body=", out);
        dump_seq(out, n->v.Method.body, dumpv_stmt);
        fputs(", is_static=", out);
        dump_int(out, n->v.Method.is_static);
        fputc(')', out);
        break;
    case Constructor_kind:
        fputs("Constructor(", out);
        fputs("params=", out);
        dump_seq(out, n->v.Constructor.params, dumpv_param);
        fputs(", body=", out);
        dump_seq(out, n->v.Constructor.body, dumpv_stmt);
        fputc(')', out);
        break;
    case Property_kind:
        fputs("Property(", out);
        fputs("access=", out);
        dump_access(out, n->v.Property.access);
        fputs(", ty=", out);
        dump_typeref(out, n->v.Property.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.Property.name);
        fputc(')', out);
        break;
    case Getter_kind:
        fputs("Getter(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.Getter.name);
        fputs(", body=", out);
        dump_seq(out, n->v.Getter.body, dumpv_stmt);
        fputc(')', out);
        break;
    case Setter_kind:
        fputs("Setter(", out);
        fputs("name=", out);
        dump_identifier(out, n->v.Setter.name);
        fputs(", param=", out);
        dump_param(out, n->v.Setter.param);
        fputs(", body=", out);
        dump_seq(out, n->v.Setter.body, dumpv_stmt);
        fputc(')', out);
        break;
    }
}

static void dump_struct_member(FILE *out, struct_member_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n->kind) {
    case StructField_kind:
        fputs("StructField(", out);
        fputs("ty=", out);
        dump_typeref(out, n->v.StructField.ty);
        fputs(", name=", out);
        dump_identifier(out, n->v.StructField.name);
        fputc(')', out);
        break;
    case StructCtor_kind:
        fputs("StructCtor(", out);
        fputs("params=", out);
        dump_seq(out, n->v.StructCtor.params, dumpv_param);
        fputs(", inits=", out);
        dump_seq(out, n->v.StructCtor.inits, dumpv_member_init);
        fputs(", body=", out);
        dump_seq(out, n->v.StructCtor.body, dumpv_stmt);
        fputc(')', out);
        break;
    }
}

static void dump_member_init(FILE *out, member_init_ty n) {
    if (!n) { fputs("None", out); return; }
    fputs("member_init(", out);
    fputs("field=", out);
    dump_identifier(out, n->field);
    fputs(", value=", out);
    dump_expr(out, n->value);
    fputc(')', out);
}

static void dump_boolop(FILE *out, boolop_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case And: fputs("And()", out); break;
    case Or: fputs("Or()", out); break;
    }
}

static void dump_unaryop(FILE *out, unaryop_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case UAdd: fputs("UAdd()", out); break;
    case USub: fputs("USub()", out); break;
    case Not: fputs("Not()", out); break;
    }
}

static void dump_operator(FILE *out, operator_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case Add: fputs("Add()", out); break;
    case Sub: fputs("Sub()", out); break;
    case Mult: fputs("Mult()", out); break;
    case Div: fputs("Div()", out); break;
    case FloorDiv: fputs("FloorDiv()", out); break;
    case Mod: fputs("Mod()", out); break;
    case Pow: fputs("Pow()", out); break;
    }
}

static void dump_cmpop(FILE *out, cmpop_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case Eq: fputs("Eq()", out); break;
    case NotEq: fputs("NotEq()", out); break;
    case Lt: fputs("Lt()", out); break;
    case LtE: fputs("LtE()", out); break;
    case Gt: fputs("Gt()", out); break;
    case GtE: fputs("GtE()", out); break;
    }
}

static void dump_assign_op(FILE *out, assign_op_ty n) {
    if (!n) { fputs("None", out); return; }
    switch (n) {
    case AssignEq: fputs("AssignEq()", out); break;
    case AssignAdd: fputs("AssignAdd()", out); break;
    case AssignSub: fputs("AssignSub()", out); break;
    case AssignMult: fputs("AssignMult()", out); break;
    case AssignDiv: fputs("AssignDiv()", out); break;
    case AssignFloorDiv: fputs("AssignFloorDiv()", out); break;
    case AssignMod: fputs("AssignMod()", out); break;
    case AssignPow: fputs("AssignPow()", out); break;
    }
}

void Aster_AST_Dump(FILE *f, mod_ty mod) {
    dump_mod(f, mod);
    fputc('\n', f);
}
