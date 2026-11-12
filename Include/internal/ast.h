/* 자동 생성: Parser/asdl_c.py (원본: Parser/Aster.asdl). 직접 고치지 마세요. */
#ifndef ASTER_AST_H
#define ASTER_AST_H
#include <stdio.h>
#include "internal/arena.h"
#include "internal/asdl.h"
#include "internal/value.h"

typedef struct _mod *mod_ty;
typedef struct _stmt *stmt_ty;
typedef struct _expr *expr_ty;
typedef struct _typeref *typeref_ty;
typedef struct _ret *ret_ty;
typedef struct _param *param_ty;
typedef struct _lparam *lparam_ty;
typedef struct _signature *signature_ty;
typedef struct _handler *handler_ty;
typedef struct _range_expr *range_expr_ty;
typedef struct _enum_member *enum_member_ty;
typedef struct _initializer *initializer_ty;
typedef struct _designation *designation_ty;
typedef struct _member *member_ty;
typedef struct _struct_member *struct_member_ty;
typedef struct _member_init *member_init_ty;

typedef enum _prim { IntT = 1, FloatT = 2, BoolT = 3, StrT = 4, NullT = 5 } prim_ty;
typedef enum _access { Public = 1, Protected = 2, Private = 3, Internal = 4 } access_ty;
typedef enum _boolop { And = 1, Or = 2 } boolop_ty;
typedef enum _unaryop { UAdd = 1, USub = 2, Not = 3 } unaryop_ty;
typedef enum _operator { Add = 1, Sub = 2, Mult = 3, Div = 4, FloorDiv = 5, Mod = 6, Pow = 7 } operator_ty;
typedef enum _cmpop { Eq = 1, NotEq = 2, Lt = 3, LtE = 4, Gt = 5, GtE = 6 } cmpop_ty;
typedef enum _assign_op { AssignEq = 1, AssignAdd = 2, AssignSub = 3, AssignMult = 4, AssignDiv = 5, AssignFloorDiv = 6, AssignMod = 7, AssignPow = 8 } assign_op_ty;

enum _mod_kind { Module_kind = 1 };
struct _mod {
    enum _mod_kind kind;
    union {
        struct { asdl_seq *package; asdl_seq *body; } Module;
    } v;
};

enum _stmt_kind { FunctionDef_kind = 1, ClassDef_kind = 2, InterfaceDef_kind = 3, StructDef_kind = 4, VarDecl_kind = 5, EnumDef_kind = 6, Destructure_kind = 7, Assert_kind = 8, Assign_kind = 9, If_kind = 10, While_kind = 11, For_kind = 12, ForRange_kind = 13, ForIn_kind = 14, Try_kind = 15, Yield_kind = 16, Return_kind = 17, Throw_kind = 18, Break_kind = 19, Continue_kind = 20, Import_kind = 21, ImportFrom_kind = 22, Export_kind = 23, ExceptionDef_kind = 24, ExprStmt_kind = 25 };
struct _stmt {
    enum _stmt_kind kind;
    union {
        struct { identifier name; asdl_seq *type_params; asdl_seq *params; ret_ty returns; asdl_seq *body; int is_extern; } FunctionDef;
        struct { identifier name; asdl_seq *type_params; typeref_ty base; asdl_seq *interfaces; asdl_seq *members; } ClassDef;
        struct { identifier name; asdl_seq *methods; } InterfaceDef;
        struct { identifier tag; identifier alias; asdl_seq *members; } StructDef;
        struct { typeref_ty ty; identifier name; initializer_ty init; int is_const; } VarDecl;
        struct { identifier name; asdl_seq *members; } EnumDef;
        struct { asdl_seq *names; expr_ty value; } Destructure;
        struct { expr_ty test; expr_ty message; } Assert;
        struct { expr_ty target; assign_op_ty op; expr_ty value; } Assign;
        struct { expr_ty test; asdl_seq *body; asdl_seq *orelse; } If;
        struct { expr_ty test; asdl_seq *body; } While;
        struct { stmt_ty init; expr_ty test; stmt_ty step; asdl_seq *body; } For;
        struct { typeref_ty ty; identifier name; range_expr_ty iter; asdl_seq *body; } ForRange;
        struct { typeref_ty ty; identifier name; expr_ty iter; asdl_seq *body; } ForIn;
        struct { asdl_seq *body; asdl_seq *handlers; asdl_seq *finalbody; } Try;
        struct { expr_ty value; } Yield;
        struct { expr_ty value; } Return;
        struct { expr_ty value; identifier exc_type; } Throw;
        struct { asdl_seq *path; int wildcard; identifier alias; } Import;
        struct { asdl_seq *module; identifier name; identifier alias; } ImportFrom;
        struct { stmt_ty body; } Export;
        struct { identifier name; identifier base; } ExceptionDef;
        struct { expr_ty value; } ExprStmt;
    } v;
    int lineno;
    int col_offset;
    int end_lineno;
    int end_col_offset;
};

enum _expr_kind { Lambda_kind = 1, Pipe_kind = 2, Coalesce_kind = 3, BoolOp_kind = 4, UnaryOp_kind = 5, BinOp_kind = 6, Compare_kind = 7, Is_kind = 8, In_kind = 9, Cast_kind = 10, AddressOf_kind = 11, Deref_kind = 12, SizeOf_kind = 13, Call_kind = 14, New_kind = 15, Attribute_kind = 16, PtrAttribute_kind = 17, OptionalAttribute_kind = 18, Name_kind = 19, Self_kind = 20, Constant_kind = 21, Array_kind = 22, Range_kind = 23, Subscript_kind = 24, Slice_kind = 25, FString_kind = 26, FormattedValue_kind = 27, List_kind = 28, Tuple_kind = 29, Dict_kind = 30, Set_kind = 31, Spread_kind = 32, ListComp_kind = 33, SetComp_kind = 34, DictComp_kind = 35, Incr_kind = 36 };
struct _expr {
    enum _expr_kind kind;
    union {
        struct { asdl_seq *params; ret_ty returns; expr_ty body; } Lambda;
        struct { expr_ty value; expr_ty func; } Pipe;
        struct { expr_ty left; expr_ty right; } Coalesce;
        struct { boolop_ty op; expr_ty left; expr_ty right; } BoolOp;
        struct { unaryop_ty op; expr_ty operand; } UnaryOp;
        struct { expr_ty left; operator_ty op; expr_ty right; } BinOp;
        struct { expr_ty left; cmpop_ty op; expr_ty right; } Compare;
        struct { expr_ty value; typeref_ty ty; } Is;
        struct { expr_ty item; expr_ty container; } In;
        struct { typeref_ty ty; expr_ty value; } Cast;
        struct { expr_ty value; } AddressOf;
        struct { expr_ty value; } Deref;
        struct { typeref_ty ty; } SizeOf;
        struct { expr_ty func; asdl_seq *args; } Call;
        struct { identifier cls; asdl_seq *type_args; asdl_seq *args; } New;
        struct { expr_ty value; identifier attr; } Attribute;
        struct { expr_ty value; identifier attr; } PtrAttribute;
        struct { expr_ty value; identifier attr; } OptionalAttribute;
        struct { identifier id; } Name;
        struct { Value value; } Constant;
        struct { asdl_seq *elements; } Array;
        struct { expr_ty start; expr_ty stop; expr_ty step; } Range;
        struct { expr_ty value; expr_ty index; } Subscript;
        struct { expr_ty lower; expr_ty upper; expr_ty step; } Slice;
        struct { asdl_seq *values; } FString;
        struct { expr_ty value; int conversion; expr_ty format_spec; } FormattedValue;
        struct { asdl_seq *elements; } List;
        struct { asdl_seq *elements; } Tuple;
        struct { asdl_seq *keys; asdl_seq *values; } Dict;
        struct { asdl_seq *elements; } Set;
        struct { expr_ty value; } Spread;
        struct { expr_ty elt; identifier name; expr_ty iter; expr_ty cond; } ListComp;
        struct { expr_ty elt; identifier name; expr_ty iter; expr_ty cond; } SetComp;
        struct { expr_ty key; expr_ty value; identifier name; expr_ty iter; expr_ty cond; } DictComp;
        struct { expr_ty target; int is_inc; int is_prefix; } Incr;
    } v;
    int lineno;
    int col_offset;
    int end_lineno;
    int end_col_offset;
};

enum _typeref_kind { Primitive_kind = 1, Named_kind = 2, StructRef_kind = 3, Pointer_kind = 4, Nullable_kind = 5, FunctionTy_kind = 6 };
struct _typeref {
    enum _typeref_kind kind;
    union {
        struct { prim_ty kind; } Primitive;
        struct { identifier name; asdl_seq *args; } Named;
        struct { identifier tag; } StructRef;
        struct { typeref_ty base; } Pointer;
        struct { typeref_ty base; } Nullable;
        struct { asdl_seq *params; ret_ty returns; } FunctionTy;
    } v;
};

enum _ret_kind { VoidRet_kind = 1, ValueRet_kind = 2 };
struct _ret {
    enum _ret_kind kind;
    union {
        struct { typeref_ty ty; } ValueRet;
    } v;
};

struct _param {
    typeref_ty ty;
    identifier name;
};

struct _lparam {
    typeref_ty ty;
    identifier name;
};

struct _signature {
    access_ty access;
    identifier name;
    asdl_seq *params;
    ret_ty returns;
};

struct _handler {
    typeref_ty ty;
    identifier name;
    asdl_seq *body;
};

struct _range_expr {
    expr_ty start;
    expr_ty stop;
    expr_ty step;
};

struct _enum_member {
    identifier name;
    expr_ty value;
};

enum _initializer_kind { NoInit_kind = 1, ExprInit_kind = 2, PositionalInit_kind = 3, DesignatedInit_kind = 4 };
struct _initializer {
    enum _initializer_kind kind;
    union {
        struct { expr_ty value; } ExprInit;
        struct { asdl_seq *values; } PositionalInit;
        struct { asdl_seq *items; } DesignatedInit;
    } v;
};

struct _designation {
    identifier field;
    expr_ty value;
};

enum _member_kind { Field_kind = 1, Method_kind = 2, Constructor_kind = 3, Property_kind = 4, Getter_kind = 5, Setter_kind = 6 };
struct _member {
    enum _member_kind kind;
    union {
        struct { access_ty access; typeref_ty ty; identifier name; expr_ty value; int is_static; } Field;
        struct { access_ty access; identifier name; asdl_seq *type_params; asdl_seq *params; ret_ty returns; asdl_seq *body; int is_static; } Method;
        struct { asdl_seq *params; asdl_seq *body; } Constructor;
        struct { access_ty access; typeref_ty ty; identifier name; } Property;
        struct { identifier name; asdl_seq *body; } Getter;
        struct { identifier name; param_ty param; asdl_seq *body; } Setter;
    } v;
    int lineno;
    int col_offset;
    int end_lineno;
    int end_col_offset;
};

enum _struct_member_kind { StructField_kind = 1, StructCtor_kind = 2 };
struct _struct_member {
    enum _struct_member_kind kind;
    union {
        struct { typeref_ty ty; identifier name; } StructField;
        struct { asdl_seq *params; asdl_seq *inits; asdl_seq *body; } StructCtor;
    } v;
};

struct _member_init {
    identifier field;
    expr_ty value;
};

/* 생성자 */
mod_ty _Aster_Module(asdl_seq *package, asdl_seq *body, Arena *arena);
stmt_ty _Aster_FunctionDef(identifier name, asdl_seq *type_params, asdl_seq *params, ret_ty returns, asdl_seq *body, int is_extern, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ClassDef(identifier name, asdl_seq *type_params, typeref_ty base, asdl_seq *interfaces, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_InterfaceDef(identifier name, asdl_seq *methods, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_StructDef(identifier tag, identifier alias, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_VarDecl(typeref_ty ty, identifier name, initializer_ty init, int is_const, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_EnumDef(identifier name, asdl_seq *members, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Destructure(asdl_seq *names, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Assert(expr_ty test, expr_ty message, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Assign(expr_ty target, assign_op_ty op, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_If(expr_ty test, asdl_seq *body, asdl_seq *orelse, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_While(expr_ty test, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_For(stmt_ty init, expr_ty test, stmt_ty step, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ForRange(typeref_ty ty, identifier name, range_expr_ty iter, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ForIn(typeref_ty ty, identifier name, expr_ty iter, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Try(asdl_seq *body, asdl_seq *handlers, asdl_seq *finalbody, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Yield(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Return(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Throw(expr_ty value, identifier exc_type, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Break(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Continue(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Import(asdl_seq *path, int wildcard, identifier alias, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ImportFrom(asdl_seq *module, identifier name, identifier alias, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_Export(stmt_ty body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ExceptionDef(identifier name, identifier base, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
stmt_ty _Aster_ExprStmt(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Lambda(asdl_seq *params, ret_ty returns, expr_ty body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Pipe(expr_ty value, expr_ty func, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Coalesce(expr_ty left, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_BoolOp(boolop_ty op, expr_ty left, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_UnaryOp(unaryop_ty op, expr_ty operand, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_BinOp(expr_ty left, operator_ty op, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Compare(expr_ty left, cmpop_ty op, expr_ty right, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Is(expr_ty value, typeref_ty ty, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_In(expr_ty item, expr_ty container, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Cast(typeref_ty ty, expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_AddressOf(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Deref(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_SizeOf(typeref_ty ty, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Call(expr_ty func, asdl_seq *args, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_New(identifier cls, asdl_seq *type_args, asdl_seq *args, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Attribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_PtrAttribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_OptionalAttribute(expr_ty value, identifier attr, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Name(identifier id, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Self(int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Constant(Value value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Array(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Range(expr_ty start, expr_ty stop, expr_ty step, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Subscript(expr_ty value, expr_ty index, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Slice(expr_ty lower, expr_ty upper, expr_ty step, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_FString(asdl_seq *values, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_FormattedValue(expr_ty value, int conversion, expr_ty format_spec, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_List(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Tuple(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Dict(asdl_seq *keys, asdl_seq *values, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Set(asdl_seq *elements, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Spread(expr_ty value, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_ListComp(expr_ty elt, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_SetComp(expr_ty elt, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_DictComp(expr_ty key, expr_ty value, identifier name, expr_ty iter, expr_ty cond, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
expr_ty _Aster_Incr(expr_ty target, int is_inc, int is_prefix, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
typeref_ty _Aster_Primitive(prim_ty kind, Arena *arena);
typeref_ty _Aster_Named(identifier name, asdl_seq *args, Arena *arena);
typeref_ty _Aster_StructRef(identifier tag, Arena *arena);
typeref_ty _Aster_Pointer(typeref_ty base, Arena *arena);
typeref_ty _Aster_Nullable(typeref_ty base, Arena *arena);
typeref_ty _Aster_FunctionTy(asdl_seq *params, ret_ty returns, Arena *arena);
ret_ty _Aster_VoidRet(Arena *arena);
ret_ty _Aster_ValueRet(typeref_ty ty, Arena *arena);
param_ty _Aster_param(typeref_ty ty, identifier name, Arena *arena);
lparam_ty _Aster_lparam(typeref_ty ty, identifier name, Arena *arena);
signature_ty _Aster_signature(access_ty access, identifier name, asdl_seq *params, ret_ty returns, Arena *arena);
handler_ty _Aster_handler(typeref_ty ty, identifier name, asdl_seq *body, Arena *arena);
range_expr_ty _Aster_range_expr(expr_ty start, expr_ty stop, expr_ty step, Arena *arena);
enum_member_ty _Aster_enum_member(identifier name, expr_ty value, Arena *arena);
initializer_ty _Aster_NoInit(Arena *arena);
initializer_ty _Aster_ExprInit(expr_ty value, Arena *arena);
initializer_ty _Aster_PositionalInit(asdl_seq *values, Arena *arena);
initializer_ty _Aster_DesignatedInit(asdl_seq *items, Arena *arena);
designation_ty _Aster_designation(identifier field, expr_ty value, Arena *arena);
member_ty _Aster_Field(access_ty access, typeref_ty ty, identifier name, expr_ty value, int is_static, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
member_ty _Aster_Method(access_ty access, identifier name, asdl_seq *type_params, asdl_seq *params, ret_ty returns, asdl_seq *body, int is_static, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
member_ty _Aster_Constructor(asdl_seq *params, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
member_ty _Aster_Property(access_ty access, typeref_ty ty, identifier name, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
member_ty _Aster_Getter(identifier name, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
member_ty _Aster_Setter(identifier name, param_ty param, asdl_seq *body, int lineno, int col_offset, int end_lineno, int end_col_offset, Arena *arena);
struct_member_ty _Aster_StructField(typeref_ty ty, identifier name, Arena *arena);
struct_member_ty _Aster_StructCtor(asdl_seq *params, asdl_seq *inits, asdl_seq *body, Arena *arena);
member_init_ty _Aster_member_init(identifier field, expr_ty value, Arena *arena);

/* AST를 파이썬 ast.dump와 같은 모양으로 출력한다 (테스트용) */
void Aster_AST_Dump(FILE *f, mod_ty mod);

#endif
