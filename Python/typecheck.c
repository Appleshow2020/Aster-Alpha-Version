#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/typecheck.h"
#include "internal/parser.h"
#include "internal/runtime.h"

/* ------------------------------------------------------------------ 타입 */

typedef enum {
    TY_ANY,
    TY_VOID,
    TY_NULL,
    TY_INT,
    TY_FLOAT,
    TY_BOOL,
    TY_STR,
    TY_LIST,
    TY_TUPLE,
    TY_DICT,
    TY_SET,
    TY_FROZENSET,
    TY_PTR,
    TY_CLASS,
    TY_STRUCT,
    TY_MODULE,
    TY_FUNC,
    TY_OVERLOAD
} TyKind;

typedef struct Ty Ty;
typedef struct Sym Sym;
typedef struct Scope Scope;
typedef struct Member Member;
typedef struct ClassInfo ClassInfo;
typedef struct StructInfo StructInfo;
typedef struct FieldInfo FieldInfo;
typedef struct Checker Checker;
typedef struct ModuleInfo ModuleInfo;

struct Ty {
    TyKind kind;
    const char *name;        /* TY_CLASS, TY_STRUCT, TY_MODULE */
    Ty *a;                   /* 원소, 포인터 대상, dict 키 */
    Ty *b;                   /* dict 값 */
    int nullable;
    /* TY_FUNC */
    Ty **params;
    size_t nparams;
    Ty *ret;
    int varargs;
    /* TY_OVERLOAD */
    Ty **overloads;
    size_t noverloads;
};

struct Sym {
    const char *name;
    Ty *ty;
    Sym *next;
};

struct Scope {
    Scope *parent;
    Sym *syms;
};

struct Member {
    const char *name;
    Ty *ty;                  /* 필드/프로퍼티 타입 */
    Ty *fn;                  /* 메서드면 함수 타입 */
    int is_method;
    access_ty access;
    Member *next;
};

struct ClassInfo {
    const char *name;
    const char *base_name;
    ClassInfo *base;
    Member *members;
    Ty *ctor;
    int is_exception;
    int is_interface;
    int line, col;
    const char **interfaces;
    size_t ninterfaces;
    ClassInfo *next;
};

struct FieldInfo {
    const char *name;
    Ty *ty;
};

struct StructInfo {
    const char *name;
    const char *alias;
    FieldInfo *fields;
    size_t nfields;
    StructInfo *next;
};

struct Checker {
    Arena *arena;
    const char *filename;
    int errors;
    Scope *scope;
    ClassInfo *classes;
    StructInfo *structs;
    ClassInfo *current_class;
    Ty *current_ret;
    int current_ret_known;
    int in_function;
    int allow_unknown;
    const char **tparams;
    size_t ntparams;
    const TypeCheckEnv *env;
    ModuleInfo *modules;
};

/* import된 모듈의 export 심볼 타입. */
struct ModuleInfo {
    const char *name;
    Sym *exports;
    size_t nexports;
    int has_explicit_exports;
    int loading;             /* 순환 import 중이면 1 */
    int resolved;            /* 파일을 찾아 검사까지 마쳤으면 1 */
    int ok;                  /* 모듈을 실제로 읽었다면 1 */
    int opaque;              /* 내장 모듈(System/Files)이나 검사 환경이 없을 때 1 */
    ModuleInfo *next;
};

/* ------------------------------------------------------------------ 공통 */

static Ty *ty_new(Checker *c, TyKind kind)
{
    Ty *t = Arena_Alloc(c->arena, sizeof(Ty));
    memset(t, 0, sizeof(*t));
    t->kind = kind;
    return t;
}

static Ty *ty_any(Checker *c) { return ty_new(c, TY_ANY); }
static Ty *ty_void(Checker *c) { return ty_new(c, TY_VOID); }
static Ty *ty_null(Checker *c) { return ty_new(c, TY_NULL); }
static Ty *ty_int(Checker *c) { return ty_new(c, TY_INT); }
static Ty *ty_float(Checker *c) { return ty_new(c, TY_FLOAT); }
static Ty *ty_bool(Checker *c) { return ty_new(c, TY_BOOL); }
static Ty *ty_str(Checker *c) { return ty_new(c, TY_STR); }

static Ty *ty_class(Checker *c, const char *name)
{
    Ty *t = ty_new(c, TY_CLASS);
    t->name = name;
    return t;
}

static Ty *ty_struct(Checker *c, const char *name)
{
    Ty *t = ty_new(c, TY_STRUCT);
    t->name = name;
    return t;
}

static Ty *ty_module(Checker *c, const char *name)
{
    Ty *t = ty_new(c, TY_MODULE);
    t->name = name;
    return t;
}

static Ty *ty_ptr(Checker *c, Ty *base)
{
    Ty *t = ty_new(c, TY_PTR);
    t->a = base;
    return t;
}

static Ty *ty_seq(Checker *c, TyKind kind, Ty *elem)
{
    Ty *t = ty_new(c, kind);
    t->a = elem ? elem : ty_any(c);
    return t;
}

static Ty *ty_dict(Checker *c, Ty *key, Ty *value)
{
    Ty *t = ty_new(c, TY_DICT);
    t->a = key ? key : ty_any(c);
    t->b = value ? value : ty_any(c);
    return t;
}

static Ty *ty_func(Checker *c, Ty **params, size_t nparams, Ty *ret, int varargs)
{
    Ty *t = ty_new(c, TY_FUNC);
    t->params = params;
    t->nparams = nparams;
    t->ret = ret ? ret : ty_void(c);
    t->varargs = varargs;
    return t;
}

static Ty *ty_overload_append(Checker *c, Ty *existing, Ty *sig)
{
    Ty *t;
    if (!existing)
        return sig;
    if (existing->kind == TY_OVERLOAD) {
        size_t n = existing->noverloads;
        Ty **grown = Arena_Alloc(c->arena, (n + 1) * sizeof(Ty *));
        memcpy(grown, existing->overloads, n * sizeof(Ty *));
        grown[n] = sig;
        existing->overloads = grown;
        existing->noverloads = n + 1;
        return existing;
    }
    if (existing->kind != TY_FUNC)
        return existing;
    t = ty_new(c, TY_OVERLOAD);
    t->overloads = Arena_Alloc(c->arena, 2 * sizeof(Ty *));
    t->overloads[0] = existing;
    t->overloads[1] = sig;
    t->noverloads = 2;
    return t;
}

static Ty *ty_nullable(Checker *c, Ty *base)
{
    Ty *t;
    if (!base || base->kind == TY_ANY || base->nullable)
        return base ? base : ty_any(c);
    t = Arena_Alloc(c->arena, sizeof(Ty));
    *t = *base;
    t->nullable = 1;
    return t;
}

static Ty *ty_non_null(Checker *c, Ty *base)
{
    Ty *t;
    if (!base || !base->nullable)
        return base;
    t = Arena_Alloc(c->arena, sizeof(Ty));
    *t = *base;
    t->nullable = 0;
    return t;
}

static void ty_write(char *buf, size_t cap, size_t *o, Ty *t)
{
    size_t i;

    if (*o >= cap)
        return;
    if (!t) {
        snprintf(buf + *o, cap - *o, "any");
        *o += strlen(buf + *o);
        return;
    }
    switch (t->kind) {
    case TY_ANY: snprintf(buf + *o, cap - *o, "any"); break;
    case TY_VOID: snprintf(buf + *o, cap - *o, "void"); break;
    case TY_NULL: snprintf(buf + *o, cap - *o, "null"); break;
    case TY_INT: snprintf(buf + *o, cap - *o, "int"); break;
    case TY_FLOAT: snprintf(buf + *o, cap - *o, "float"); break;
    case TY_BOOL: snprintf(buf + *o, cap - *o, "bool"); break;
    case TY_STR: snprintf(buf + *o, cap - *o, "str"); break;
    case TY_CLASS:
        snprintf(buf + *o, cap - *o, "%s", t->name ? t->name : "class");
        break;
    case TY_STRUCT:
        snprintf(buf + *o, cap - *o, "%s", t->name ? t->name : "struct");
        break;
    case TY_MODULE:
        snprintf(buf + *o, cap - *o, "%s", t->name ? t->name : "module");
        break;
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET:
        snprintf(buf + *o, cap - *o, "%s<", t->kind == TY_LIST ? "list" :
                 t->kind == TY_TUPLE ? "tuple" : t->kind == TY_SET ? "set" : "frozenset");
        *o += strlen(buf + *o);
        ty_write(buf, cap, o, t->a);
        snprintf(buf + *o, cap - *o, ">");
        break;
    case TY_DICT:
        snprintf(buf + *o, cap - *o, "dict<");
        *o += strlen(buf + *o);
        ty_write(buf, cap, o, t->a);
        snprintf(buf + *o, cap - *o, ", ");
        *o += strlen(buf + *o);
        ty_write(buf, cap, o, t->b);
        snprintf(buf + *o, cap - *o, ">");
        break;
    case TY_PTR:
        snprintf(buf + *o, cap - *o, "pointer<");
        *o += strlen(buf + *o);
        ty_write(buf, cap, o, t->a);
        snprintf(buf + *o, cap - *o, ">");
        break;
    case TY_FUNC:
        snprintf(buf + *o, cap - *o, "fn(");
        *o += strlen(buf + *o);
        for (i = 0; i < t->nparams; i++) {
            if (i) {
                snprintf(buf + *o, cap - *o, ", ");
                *o += strlen(buf + *o);
            }
            ty_write(buf, cap, o, t->params[i]);
        }
        snprintf(buf + *o, cap - *o, ") -> ");
        *o += strlen(buf + *o);
        ty_write(buf, cap, o, t->ret);
        break;
    case TY_OVERLOAD:
        snprintf(buf + *o, cap - *o, "function");
        break;
    }
    while (*o < cap && buf[*o])
        (*o)++;
}

static const char *ty_name(Ty *t)
{
    static char bufs[4][256];
    static int turn;
    size_t o = 0;
    char *buf = bufs[turn++ & 3];
    buf[0] = 0;
    ty_write(buf, sizeof(bufs[0]), &o, t);
    return buf;
}

static int is_num(Ty *t)
{
    return t && (t->kind == TY_INT || t->kind == TY_FLOAT);
}

static int same_ty(Checker *c, Ty *a, Ty *b)
{
    size_t i;
    (void)c;
    if (!a || !b)
        return 1;
    if (a->kind == TY_ANY || b->kind == TY_ANY)
        return 1;
    if (a->kind != b->kind)
        return is_num(a) && is_num(b);
    switch (a->kind) {
    case TY_CLASS:
    case TY_STRUCT:
        return !strcmp(a->name ? a->name : "", b->name ? b->name : "");
    case TY_PTR:
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET:
        return same_ty(c, a->a, b->a);
    case TY_DICT:
        return same_ty(c, a->a, b->a) && same_ty(c, a->b, b->b);
    case TY_FUNC:
        if (a->nparams != b->nparams)
            return 0;
        for (i = 0; i < a->nparams; i++) {
            if (!same_ty(c, a->params[i], b->params[i]))
                return 0;
        }
        return same_ty(c, a->ret, b->ret);
    default:
        return 1;
    }
}

static int is_container(Ty *t)
{
    return t && (t->kind == TY_LIST || t->kind == TY_TUPLE ||
                 t->kind == TY_DICT || t->kind == TY_SET ||
                 t->kind == TY_FROZENSET);
}

static ClassInfo *find_class(Checker *c, const char *name)
{
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        if (!strcmp(ci->name, name))
            return ci;
    }
    return NULL;
}

static StructInfo *find_struct(Checker *c, const char *name)
{
    StructInfo *si;
    for (si = c->structs; si; si = si->next) {
        if (!strcmp(si->name, name) ||
            (si->alias && !strcmp(si->alias, name)))
            return si;
    }
    return NULL;
}

static Member *class_member(ClassInfo *ci, const char *name)
{
    for (; ci; ci = ci->base) {
        Member *m;
        for (m = ci->members; m; m = m->next) {
            if (!strcmp(m->name, name))
                return m;
        }
    }
    return NULL;
}

/* 멤버를 실제로 선언한 클래스(상속 체인에서). */
static ClassInfo *member_owner(ClassInfo *ci, const char *name)
{
    for (; ci; ci = ci->base) {
        Member *m;
        for (m = ci->members; m; m = m->next) {
            if (!strcmp(m->name, name))
                return ci;
        }
    }
    return NULL;
}

static int class_derives(Checker *c, const char *from, const char *to)
{
    ClassInfo *ci = find_class(c, from);
    if (!ci)
        return 1;                    /* 바깥 클래스는 상속 관계를 모르므로 허용 */
    for (; ci; ci = ci->base) {
        if (!strcmp(ci->name, to))
            return 1;
    }
    return 0;
}

static void check_error(Checker *c, int line, int col, const char *fmt, ...);

static int is_type_param(Checker *c, const char *name)
{
    size_t i;
    for (i = 0; i < c->ntparams; i++) {
        if (!strcmp(c->tparams[i], name))
            return 1;
    }
    return 0;
}

static Ty *typeref_to_ty(Checker *c, typeref_ty ty);

static Ty *named_to_ty(Checker *c, const char *name, asdl_seq *args)
{
    Ty *elem = NULL;
    size_t nargs = args ? asdl_seq_LEN(args) : 0;

    if (!strcmp(name, "var") || !strcmp(name, "any") ||
        !strcmp(name, "void") || is_type_param(c, name))
        return ty_any(c);
    if (!strcmp(name, "list") || !strcmp(name, "List") || !strcmp(name, "array")) {
        if (nargs > 0)
            elem = typeref_to_ty(c, asdl_seq_GET(args, 0));
        return ty_seq(c, TY_LIST, elem);
    }
    if (!strcmp(name, "dict") || !strcmp(name, "map") || !strcmp(name, "Map")) {
        Ty *key = nargs > 0 ? typeref_to_ty(c, asdl_seq_GET(args, 0)) : NULL;
        Ty *value = nargs > 1 ? typeref_to_ty(c, asdl_seq_GET(args, 1)) : NULL;
        return ty_dict(c, key, value);
    }
    if (!strcmp(name, "set")) {
        if (nargs > 0)
            elem = typeref_to_ty(c, asdl_seq_GET(args, 0));
        return ty_seq(c, TY_SET, elem);
    }
    if (!strcmp(name, "tuple")) {
        return ty_seq(c, TY_TUPLE, elem);
    }
    if (!strcmp(name, "frozenset")) {
        return ty_seq(c, TY_FROZENSET, elem);
    }
    if (find_struct(c, name))
        return ty_struct(c, name);
    return ty_class(c, name);
}

static Ty *typeref_to_ty(Checker *c, typeref_ty ty)
{
    if (!ty)
        return ty_any(c);
    switch (ty->kind) {
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT: return ty_int(c);
        case FloatT: return ty_float(c);
        case BoolT: return ty_bool(c);
        case StrT: return ty_str(c);
        case NullT: return ty_null(c);
        }
        return ty_any(c);
    case Named_kind:
        return named_to_ty(c, ty->v.Named.name, ty->v.Named.args);
    case StructRef_kind:
        if (find_struct(c, ty->v.StructRef.tag))
            return ty_struct(c, ty->v.StructRef.tag);
        return ty_class(c, ty->v.StructRef.tag);
    case Pointer_kind:
        return ty_ptr(c, typeref_to_ty(c, ty->v.Pointer.base));
    case Nullable_kind:
        return ty_nullable(c, typeref_to_ty(c, ty->v.Nullable.base));
    case FunctionTy_kind: {
        asdl_seq *params = ty->v.FunctionTy.params;
        size_t n = params ? asdl_seq_LEN(params) : 0;
        Ty **tys = n ? Arena_Alloc(c->arena, n * sizeof(Ty *)) : NULL;
        size_t i;
        Ty *ret;
        for (i = 0; i < n; i++)
            tys[i] = typeref_to_ty(c, asdl_seq_GET(params, i));
        ret = ty->v.FunctionTy.returns->kind == ValueRet_kind
            ? typeref_to_ty(c, ty->v.FunctionTy.returns->v.ValueRet.ty)
            : ty_void(c);
        return ty_func(c, tys, n, ret, 0);
    }
    }
    return ty_any(c);
}

static Ty *unify(Checker *c, Ty *a, Ty *b)
{
    if (!a)
        return b;
    if (!b)
        return a;
    if (a->kind == TY_ANY)
        return b;
    if (b->kind == TY_ANY)
        return a;
    if (a->kind == TY_NULL)
        return b;
    if (b->kind == TY_NULL)
        return a;
    if (a->nullable)
        a = ty_non_null(c, a);
    if (b->nullable)
        b = ty_non_null(c, b);
    if (a->kind == b->kind) {
        if (is_num(a) && is_num(b))
            return (a->kind == TY_FLOAT || b->kind == TY_FLOAT) ? ty_float(c) : ty_int(c);
        if (a->kind == TY_LIST || a->kind == TY_SET || a->kind == TY_FROZENSET ||
            a->kind == TY_TUPLE)
            return ty_seq(c, a->kind, unify(c, a->a, b->a));
        if (a->kind == TY_DICT)
            return ty_dict(c, unify(c, a->a, b->a), unify(c, a->b, b->b));
        if (a->kind == TY_CLASS && strcmp(a->name, b->name))
            return ty_class(c, a->name);
        return a;
    }
    if (is_num(a) && is_num(b))
        return ty_float(c);
    return ty_any(c);
}

static int assignable(Checker *c, Ty *dst, Ty *src)
{
    if (!dst || !src)
        return 1;
    if (dst->kind == TY_ANY || src->kind == TY_ANY)
        return 1;
    if (src->kind == TY_NULL)
        return dst->nullable || dst->kind == TY_PTR || dst->kind == TY_CLASS ||
               dst->kind == TY_STRUCT || dst->kind == TY_MODULE ||
               is_container(dst);
    if (dst->nullable) {
        if (src->kind == TY_NULL)
            return 1;
        return assignable(c, ty_non_null(c, dst), ty_non_null(c, src));
    }
    if (src->nullable)
        return 0;
    switch (dst->kind) {
    case TY_INT:
        return src->kind == TY_INT;
    case TY_FLOAT:
        return src->kind == TY_FLOAT || src->kind == TY_INT;
    case TY_BOOL:
        return src->kind == TY_BOOL;
    case TY_STR:
        return src->kind == TY_STR;
    case TY_NULL:
        return src->kind == TY_NULL;
    case TY_PTR:
        if (src->kind == TY_PTR) {
            if (!src->a)
                return 1;
            return assignable(c, dst->a, src->a);
        }
        return assignable(c, dst->a, src);
    case TY_CLASS:
        if (src->kind == TY_CLASS)
            return !strcmp(dst->name, src->name) || class_derives(c, src->name, dst->name);
        if (src->kind == TY_STRUCT)
            return !strcmp(dst->name, src->name);
        return 0;
    case TY_STRUCT:
        if (src->kind == TY_STRUCT)
            return !strcmp(dst->name, src->name) || !find_struct(c, dst->name) ||
                   !find_struct(c, src->name);
        return src->kind == TY_CLASS && !strcmp(dst->name, src->name);
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET:
        if (src->kind != dst->kind)
            return 0;
        return assignable(c, dst->a, src->a);
    case TY_DICT:
        if (src->kind != TY_DICT)
            return 0;
        return assignable(c, dst->a, src->a) && assignable(c, dst->b, src->b);
    case TY_MODULE:
        return src->kind == TY_MODULE;
    case TY_FUNC:
        return src->kind == TY_FUNC && same_ty(c, dst, src);
    case TY_VOID:
        return src->kind == TY_VOID;
    default:
        return 1;
    }
}

/* ------------------------------------------------------------------ 환경 */

static void push_scope(Checker *c)
{
    Scope *s = Arena_Alloc(c->arena, sizeof(Scope));
    s->parent = c->scope;
    s->syms = NULL;
    c->scope = s;
}

static void pop_scope(Checker *c)
{
    if (c->scope)
        c->scope = c->scope->parent;
}

static void define(Checker *c, const char *name, Ty *ty)
{
    Sym *s = Arena_Alloc(c->arena, sizeof(Sym));
    s->name = name;
    s->ty = ty ? ty : ty_any(c);
    s->next = c->scope->syms;
    c->scope->syms = s;
}

static Ty *lookup(Checker *c, const char *name)
{
    Scope *sc;
    for (sc = c->scope; sc; sc = sc->parent) {
        Sym *s;
        for (s = sc->syms; s; s = s->next) {
            if (!strcmp(s->name, name))
                return s->ty;
        }
    }
    return NULL;
}

/* 이름이 이미 있으면 타입만 갱신한다. */
static void define_or_update(Checker *c, const char *name, Ty *ty)
{
    Scope *sc;
    for (sc = c->scope; sc; sc = sc->parent) {
        Sym *s;
        for (s = sc->syms; s; s = s->next) {
            if (!strcmp(s->name, name)) {
                if (s->ty->kind == TY_ANY && ty && ty->kind != TY_ANY)
                    s->ty = ty;
                return;
            }
        }
    }
    define(c, name, ty);
}

/* 같은 이름의 함수 선언은 오버로드로 합친다. */
static void define_or_overload(Checker *c, const char *name, Ty *fn)
{
    Scope *sc;
    for (sc = c->scope; sc; sc = sc->parent) {
        Sym *s;
        for (s = sc->syms; s; s = s->next) {
            if (!strcmp(s->name, name)) {
                s->ty = ty_overload_append(c, s->ty, fn);
                return;
            }
        }
    }
    define(c, name, fn);
}

/* ------------------------------------------------------------------ 진단 */

static void check_error(Checker *c, int line, int col, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d:%d: error: ", c->filename ? c->filename : "<input>",
            line, col + 1);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    c->errors++;
}

static void require_assignable(Checker *c, Ty *dst, Ty *src, expr_ty at, const char *what)
{
    if (!assignable(c, dst, src)) {
        check_error(c, at->lineno, at->col_offset,
                    "%s: expected %s but got %s",
                    what, ty_name(dst), ty_name(src));
    }
}

/* ------------------------------------------------------------------ 함수 시그니처 */

static Ty *params_to_func(Checker *c, asdl_seq *params, ret_ty ret, int lambda_params)
{
    size_t n = params ? asdl_seq_LEN(params) : 0;
    Ty **tys = n ? Arena_Alloc(c->arena, n * sizeof(Ty *)) : NULL;
    Ty *rt;
    size_t i;

    for (i = 0; i < n; i++) {
        typeref_ty ty;
        if (lambda_params) {
            lparam_ty p = asdl_seq_GET(params, i);
            ty = p->ty;
        }
        else {
            param_ty p = asdl_seq_GET(params, i);
            ty = p->ty;
        }
        tys[i] = ty ? typeref_to_ty(c, ty) : ty_any(c);
    }
    if (ret) {
        rt = ret->kind == ValueRet_kind ? typeref_to_ty(c, ret->v.ValueRet.ty)
                                        : ty_void(c);
    }
    else {
        rt = ty_void(c);
    }
    return ty_func(c, tys, n, rt, 0);
}

/* ------------------------------------------------------------------ 타입 파라미터 문맥 */

typedef struct {
    const char **saved;
    size_t saved_n;
} TParamSave;

static void push_tparams(Checker *c, asdl_seq *type_params, TParamSave *save)
{
    size_t n = type_params ? asdl_seq_LEN(type_params) : 0;
    size_t i;
    save->saved = c->tparams;
    save->saved_n = c->ntparams;
    if (n > 0) {
        c->tparams = Arena_Alloc(c->arena, n * sizeof(const char *));
        for (i = 0; i < n; i++)
            c->tparams[i] = asdl_seq_GET(type_params, i);
        c->ntparams = n;
    }
    else {
        c->tparams = NULL;
        c->ntparams = 0;
    }
}

static void pop_tparams(Checker *c, TParamSave *save)
{
    c->tparams = save->saved;
    c->ntparams = save->saved_n;
}

/* ------------------------------------------------------------------ 표현식 */

static Ty *infer(Checker *c, expr_ty e);

static Ty *constant_type(Checker *c, Value v)
{
    switch (v.tag) {
    case VT_NULL: return ty_null(c);
    case VT_BOOL: return ty_bool(c);
    case VT_INT: return ty_int(c);
    case VT_FLOAT: return ty_float(c);
    case VT_OBJ: return ty_str(c);
    }
    return ty_any(c);
}

static Ty *iter_element(Checker *c, Ty *t)
{
    if (!t)
        return ty_any(c);
    switch (t->kind) {
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET:
        return t->a;
    case TY_DICT:
        return t->a;
    case TY_STR:
        return ty_str(c);
    case TY_PTR:
        return iter_element(c, t->a);
    default:
        return ty_any(c);
    }
}

static Ty *str_method_type(Checker *c, const char *name)
{
    static const struct {
        const char *name;
        char ret;                /* i=int, f=float, s=str, b=bool, l=list<str>, v=void */
    } table[] = {
        {"upper", 's'}, {"lower", 's'}, {"strip", 's'}, {"trim", 's'},
        {"toUpperCase", 's'}, {"toLowerCase", 's'}, {"replace", 's'},
        {"join", 's'}, {"substring", 's'}, {"charAt", 's'}, {"toString", 's'},
        {"split", 'l'}, {"chars", 'l'},
        {"length", 'i'}, {"count", 'i'}, {"find", 'i'}, {"indexOf", 'i'},
        {"to_int", 'i'}, {"to_int", 'i'}, {"toInt", 'i'},
        {"to_float", 'f'}, {"toFloat", 'f'},
        {"startswith", 'b'}, {"endswith", 'b'}, {"isdigit", 'b'},
        {"isalpha", 'b'}, {"isspace", 'b'}, {"contains", 'b'}
    };
    size_t i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (!strcmp(table[i].name, name)) {
            switch (table[i].ret) {
            case 'i': return ty_int(c);
            case 'f': return ty_float(c);
            case 'b': return ty_bool(c);
            case 'l': return ty_seq(c, TY_LIST, ty_str(c));
            case 'v': return ty_void(c);
            default: return ty_str(c);
            }
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ 모듈 */

static void collect_body(Checker *c, asdl_seq *body);
static void check_body(Checker *c, asdl_seq *body);
static void resolve_bases(Checker *c);
static void init_builtins(Checker *c);
static void check_interfaces(Checker *c);

static const char *module_dotted_name(Checker *c, asdl_seq *path)
{
    size_t i, n = path ? asdl_seq_LEN(path) : 0, total = 0, o = 0;
    char *name;

    for (i = 0; i < n; i++)
        total += strlen((const char *)asdl_seq_GET(path, i)) + 1;
    name = Arena_Alloc(c->arena, total ? total : 1);
    for (i = 0; i < n; i++) {
        const char *part = asdl_seq_GET(path, i);
        size_t len = strlen(part);
        if (i > 0)
            name[o++] = '.';
        memcpy(name + o, part, len);
        o += len;
    }
    name[o] = 0;
    return name;
}

static ModuleInfo *find_module(Checker *c, const char *name)
{
    ModuleInfo *mi;
    for (mi = c->modules; mi; mi = mi->next) {
        if (!strcmp(mi->name, name))
            return mi;
    }
    return NULL;
}

static Ty *module_export_type(ModuleInfo *mi, const char *name)
{
    Sym *s;
    if (!mi)
        return NULL;
    for (s = mi->exports; s; s = s->next) {
        if (!strcmp(s->name, name))
            return s->ty;
    }
    return NULL;
}

/* C 런타임이 직접 제공해서 .ast 파일이 없는 모듈. */
static int is_opaque_module(const char *name)
{
    return !strcmp(name, "System") || !strncmp(name, "System.", 7) ||
           !strcmp(name, "Files");
}

static char *read_source_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t cap = 4096, len = 0;

    if (!f)
        return NULL;
    buf = malloc(cap + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        size_t n;
        if (len == cap) {
            char *grown;
            cap *= 2;
            grown = realloc(buf, cap + 1);
            if (!grown) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = grown;
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0)
            break;
    }
    fclose(f);
    buf[len] = 0;
    *size = len;
    return buf;
}

static const char *exported_name(stmt_ty s)
{
    switch (s->kind) {
    case FunctionDef_kind:
        return s->v.FunctionDef.name;
    case ClassDef_kind:
        return s->v.ClassDef.name;
    case StructDef_kind:
        return s->v.StructDef.tag;
    case InterfaceDef_kind:
        return s->v.InterfaceDef.name;
    case ExceptionDef_kind:
        return s->v.ExceptionDef.name;
    case VarDecl_kind:
        return s->v.VarDecl.name;
    case Assign_kind:
        if (s->v.Assign.target->kind == Name_kind)
            return s->v.Assign.target->v.Name.id;
        return NULL;
    default:
        return NULL;
    }
}

static void module_add_export(Checker *c, ModuleInfo *mi, const char *name)
{
    Sym *s;
    Ty *ty;

    if (module_export_type(mi, name))
        return;
    ty = lookup(c, name);
    s = Arena_Alloc(c->arena, sizeof(Sym));
    s->name = name;
    s->ty = ty ? ty : ty_any(c);
    s->next = mi->exports;
    mi->exports = s;
    mi->nexports++;
}

static int has_export_wrappers(asdl_seq *body)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (((stmt_ty)asdl_seq_GET(body, i))->kind == Export_kind)
            return 1;
    }
    return 0;
}

static void snapshot_exports(Checker *c, ModuleInfo *mi, asdl_seq *body)
{
    size_t i;

    if (has_export_wrappers(body)) {
        mi->has_explicit_exports = 1;
        for (i = 0; i < asdl_seq_LEN(body); i++) {
            stmt_ty s = asdl_seq_GET(body, i);
            if (s->kind == Export_kind) {
                const char *name = exported_name(s->v.Export.body);
                if (name)
                    module_add_export(c, mi, name);
            }
        }
        return;
    }
    {
        Sym *s;
        for (s = c->scope->syms; s; s = s->next) {
            if (s->name[0] != '_')
                module_add_export(c, mi, s->name);
        }
    }
}

static ModuleInfo *load_module(Checker *c, const char *name, int line, int col)
{
    ModuleInfo *mi = find_module(c, name);
    Checker mc;
    char file[1024];
    char *source;
    size_t size = 0;
    AsterError err;
    mod_ty mod;

    if (mi)
        return mi;

    mi = Arena_Alloc(c->arena, sizeof(ModuleInfo));
    memset(mi, 0, sizeof(*mi));
    mi->name = name;
    mi->next = c->modules;
    c->modules = mi;

    if (!c->env || is_opaque_module(name)) {
        mi->opaque = 1;
        mi->resolved = 1;
        mi->ok = 1;
        return mi;
    }
    mi->loading = 1;
    if (!Aster_ResolveModuleFile(c->env->base_dir, c->env->stdlib_dir,
                                 c->env->module_dirs, c->env->nmodule_dirs,
                                 name, file, sizeof file)) {
        check_error(c, line, col, "cannot find module '%s'", name);
        mi->loading = 0;
        mi->resolved = 1;
        return mi;
    }
    source = read_source_file(file, &size);
    if (!source) {
        check_error(c, line, col, "cannot read module file '%s': %s", name, file);
        mi->loading = 0;
        mi->resolved = 1;
        return mi;
    }
    mod = Aster_ParseFile(source, size, c->arena, &err);
    free(source);
    if (!mod) {
        check_error(c, line, col, "syntax error in module '%s': %s:%d:%d: %s",
                    name, file, err.line, err.col + 1, err.message);
        mi->loading = 0;
        mi->resolved = 1;
        return mi;
    }
    {
        asdl_seq *body = mod->v.Module.body;

        memset(&mc, 0, sizeof(mc));
        mc.arena = c->arena;
        mc.filename = Arena_Strndup(c->arena, file, strlen(file));
        mc.env = c->env;
        mc.modules = c->modules;

        init_builtins(&mc);
        push_scope(&mc);
        collect_body(&mc, body);
        resolve_bases(&mc);
        snapshot_exports(&mc, mi, body);
        check_interfaces(&mc);
        check_body(&mc, body);

        c->errors += mc.errors;
        c->modules = mc.modules;
    }
    mi->loading = 0;
    mi->resolved = 1;
    mi->ok = 1;
    return mi;
}

/* 모듈의 공개 이름을 현재 스코프에 정의한다. */
static void import_module_exports(Checker *c, ModuleInfo *mi)
{
    Sym *s;
    if (!mi || !mi->ok || mi->opaque)
        return;
    for (s = mi->exports; s; s = s->next)
        define_or_update(c, s->name, s->ty);
}

static void handle_import(Checker *c, stmt_ty s)
{
    size_t n = asdl_seq_LEN(s->v.Import.path);
    const char *dotted = module_dotted_name(c, s->v.Import.path);
    const char *bind = s->v.Import.alias
        ? s->v.Import.alias : asdl_seq_GET(s->v.Import.path, n - 1);
    ModuleInfo *mi = load_module(c, dotted, s->lineno, s->col_offset);

    define_or_update(c, bind, ty_module(c, dotted));
    if (s->v.Import.wildcard) {
        if (!strcmp(dotted, "System.stdout") || !strcmp(dotted, "System.stderr")) {
            define_or_update(c, "print", ty_func(c, NULL, 0, ty_void(c), 1));
            define_or_update(c, "println", ty_func(c, NULL, 0, ty_void(c), 1));
        }
        else {
            import_module_exports(c, mi);
            c->allow_unknown = 1;
        }
    }
}

static void handle_import_from(Checker *c, stmt_ty s)
{
    const char *dotted = module_dotted_name(c, s->v.ImportFrom.module);
    ModuleInfo *mi = load_module(c, dotted, s->lineno, s->col_offset);

    if (s->v.ImportFrom.name) {
        const char *name = s->v.ImportFrom.name;
        Ty *ty = module_export_type(mi, name);

        if (!ty && mi && mi->ok && !mi->opaque && !mi->loading)
            check_error(c, s->lineno, s->col_offset,
                        "module '%s' has no member '%s'", dotted, name);
        define_or_update(c, s->v.ImportFrom.alias ? s->v.ImportFrom.alias : name,
                         ty ? ty : ty_any(c));
    }
    else {
        if (!strcmp(dotted, "System.stdout") || !strcmp(dotted, "System.stderr")) {
            define_or_update(c, "print", ty_func(c, NULL, 0, ty_void(c), 1));
            define_or_update(c, "println", ty_func(c, NULL, 0, ty_void(c), 1));
        }
        import_module_exports(c, mi);
        c->allow_unknown = 1;
    }
}

static Ty *member_type(Checker *c, Ty *base, const char *name, int line, int col)
{
    if (!base || base->kind == TY_ANY)
        return ty_any(c);
    if (base->kind == TY_MODULE) {
        ModuleInfo *mi = load_module(c, base->name, line, col);
        Ty *ty;
        if (!mi || !mi->ok || mi->opaque)
            return ty_any(c);
        ty = module_export_type(mi, name);
        if (ty)
            return ty;
        if (!mi->loading)
            check_error(c, line, col, "module '%s' has no member '%s'",
                        base->name, name);
        return ty_any(c);
    }
    if (base->kind == TY_PTR || base->kind == TY_CLASS) {
        Ty *obj = base->kind == TY_PTR ? (base->a ? base->a : ty_any(c)) : base;
        return member_type(c, obj, name, line, col);
    }
    if (base->kind == TY_STR) {
        Ty *t = str_method_type(c, name);
        if (t)
            return t;
        check_error(c, line, col, "str has no attribute '%s'", name);
        return ty_any(c);
    }
    if (is_container(base) || base->kind == TY_STRUCT)
        return ty_any(c);
    return ty_any(c);
}

static Ty *attr_type(Checker *c, expr_ty e, Ty *base)
{
    const char *name;
    if (e->kind == Attribute_kind)
        name = e->v.Attribute.attr;
    else if (e->kind == PtrAttribute_kind)
        name = e->v.PtrAttribute.attr;
    else
        name = e->v.OptionalAttribute.attr;

    if (base && base->kind == TY_PTR)
        base = base->a ? base->a : ty_any(c);

    if (base && base->kind == TY_CLASS) {
        ClassInfo *ci = find_class(c, base->name);
        if (ci) {
            Member *m = class_member(ci, name);
            if (m) {
                ClassInfo *owner = member_owner(ci, name);
                const char *owner_name = owner ? owner->name : base->name;
                if (m->access == Private) {
                    if (!c->current_class ||
                        strcmp(c->current_class->name, owner_name))
                        check_error(c, e->lineno, e->col_offset,
                                    "'%s' member '%s' is private and cannot be used outside '%s'",
                                    owner_name, name, owner_name);
                }
                else if (m->access == Protected) {
                    if (!c->current_class ||
                        !class_derives(c, c->current_class->name, owner_name))
                        check_error(c, e->lineno, e->col_offset,
                                    "'%s' member '%s' is protected and cannot be used outside '%s' and its subclasses",
                                    owner_name, name, owner_name);
                }
                return m->is_method ? m->fn : m->ty;
            }
            if (ci->is_exception && (!strcmp(name, "name") || !strcmp(name, "message")))
                return ty_str(c);
            check_error(c, e->lineno, e->col_offset,
                        "class '%s' has no member '%s'", base->name, name);
            return ty_any(c);
        }
        return ty_any(c);
    }
    return member_type(c, base, name, e->lineno, e->col_offset);
}

static Ty *call_result(Checker *c, expr_ty call, Ty *callee)
{
    asdl_seq *args = call->v.Call.args;
    size_t nargs = args ? asdl_seq_LEN(args) : 0;
    size_t i;

    for (i = 0; i < nargs; i++)
        infer(c, asdl_seq_GET(args, i));

    if (!callee || callee->kind == TY_ANY)
        return ty_any(c);
    if (callee->kind == TY_OVERLOAD) {
        Ty *fallback = NULL;
        size_t k;
        for (k = 0; k < callee->noverloads; k++) {
            Ty *sig = callee->overloads[k];
            int ok = 1;
            if (!sig || sig->kind != TY_FUNC)
                continue;
            if (!sig->varargs && nargs != sig->nparams)
                continue;
            for (i = 0; i < nargs && i < sig->nparams; i++) {
                Ty *arg_ty = infer(c, asdl_seq_GET(args, i));
                if (!assignable(c, sig->params[i], arg_ty)) {
                    ok = 0;
                    break;
                }
            }
            if (ok)
                return sig->ret;
            if (!fallback)
                fallback = sig;
        }
        if (fallback) {
            for (i = 0; i < nargs && i < fallback->nparams; i++) {
                expr_ty arg = asdl_seq_GET(args, i);
                require_assignable(c, fallback->params[i], infer(c, arg), arg, "argument");
            }
            return fallback->ret;
        }
        check_error(c, call->lineno, call->col_offset,
                    "no function definition accepts %zu argument(s)", nargs);
        return ty_any(c);
    }
    if (callee->kind == TY_FUNC) {
        if (!callee->varargs && nargs != callee->nparams) {
            check_error(c, call->lineno, call->col_offset,
                        "function takes %zu argument(s) but %zu were given",
                        callee->nparams, nargs);
            return callee->ret;
        }
        for (i = 0; i < nargs && i < callee->nparams; i++) {
            expr_ty arg = asdl_seq_GET(args, i);
            require_assignable(c, callee->params[i], infer(c, arg), arg, "argument");
        }
        return callee->ret;
    }
    if (callee->kind == TY_CLASS) {
        ClassInfo *ci = find_class(c, callee->name);
        if (ci && ci->is_exception)
            return callee;
        if (ci && ci->ctor) {
            for (i = 0; i < nargs && i < ci->ctor->nparams; i++) {
                expr_ty arg = asdl_seq_GET(args, i);
                require_assignable(c, ci->ctor->params[i], infer(c, arg), arg, "constructor argument");
            }
            if (!ci->ctor->varargs && nargs != ci->ctor->nparams)
                check_error(c, call->lineno, call->col_offset,
                            "constructor of '%s' takes %zu argument(s)",
                            callee->name, ci->ctor->nparams);
            return callee;
        }
        if (nargs != 0 && ci)
            check_error(c, call->lineno, call->col_offset,
                        "'%s' has no constructor", callee->name);
        return callee;
    }
    return ty_any(c);
}

static const char *binop_hook(operator_ty op)
{
    switch (op) {
    case Add: return "__add__";
    case Sub: return "__sub__";
    case Mult: return "__mul__";
    case Div: return "__div__";
    case FloorDiv: return "__floordiv__";
    case Mod: return "__mod__";
    case Pow: return "__pow__";
    }
    return NULL;
}

/* 클래스가 연산자 훅을 정의했으면 그 반환 타입을 돌려준다. */
static Ty *operator_hook_ret(Checker *c, Ty *base, const char *name)
{
    ClassInfo *ci;
    Member *m;
    if (!base || base->kind != TY_CLASS || !name)
        return NULL;
    ci = find_class(c, base->name);
    if (!ci)
        return NULL;
    m = class_member(ci, name);
    if (!m || !m->is_method || !m->fn || m->fn->kind != TY_FUNC)
        return NULL;
    return m->fn->ret;
}

static Ty *binop_type(Checker *c, operator_ty op, Ty *l, Ty *r)
{
    Ty *hooked = operator_hook_ret(c, l, binop_hook(op));
    if (hooked)
        return hooked;
    if (op == Add) {
        if (l->kind == TY_STR || r->kind == TY_STR)
            return ty_str(c);
        if (l->kind == TY_LIST && r->kind == TY_LIST)
            return ty_seq(c, TY_LIST, unify(c, l->a, r->a));
        if (l->kind == TY_TUPLE && r->kind == TY_TUPLE)
            return ty_seq(c, TY_TUPLE, unify(c, l->a, r->a));
    }
    if (op == Mult) {
        if (l->kind == TY_STR && (r->kind == TY_INT || r->kind == TY_ANY))
            return ty_str(c);
        if (r->kind == TY_STR && (l->kind == TY_INT || l->kind == TY_ANY))
            return ty_str(c);
        if ((l->kind == TY_LIST || l->kind == TY_TUPLE) &&
            (r->kind == TY_INT || r->kind == TY_ANY))
            return l;
    }
    if (l->kind == TY_ANY || r->kind == TY_ANY)
        return ty_any(c);
    if (is_num(l) && is_num(r))
        return (l->kind == TY_FLOAT || r->kind == TY_FLOAT) ? ty_float(c) : ty_int(c);
    return ty_any(c);
}

static Ty *infer_lambda(Checker *c, expr_ty e)
{
    Ty *fn;
    size_t i, n = asdl_seq_LEN(e->v.Lambda.params);
    Ty *body_ty;

    push_scope(c);
    for (i = 0; i < n; i++) {
        lparam_ty p = asdl_seq_GET(e->v.Lambda.params, i);
        define(c, p->name, p->ty ? typeref_to_ty(c, p->ty) : ty_any(c));
    }
    body_ty = infer(c, e->v.Lambda.body);
    pop_scope(c);

    fn = params_to_func(c, e->v.Lambda.params, e->v.Lambda.returns, 1);
    if (!e->v.Lambda.returns)
        fn->ret = body_ty;
    return fn;
}

static Ty *infer(Checker *c, expr_ty e)
{
    if (!e)
        return ty_any(c);

    switch (e->kind) {
    case Lambda_kind:
        return infer_lambda(c, e);
    case Pipe_kind: {
        Ty *value = infer(c, e->v.Pipe.value);
        Ty *func = infer(c, e->v.Pipe.func);
        if (func && func->kind == TY_FUNC)
            return func->ret;
        if (func && func->kind == TY_CLASS)
            return call_result(c, e, func);
        (void)value;
        return ty_any(c);
    }
    case Coalesce_kind: {
        Ty *left = infer(c, e->v.Coalesce.left);
        Ty *right = infer(c, e->v.Coalesce.right);
        if (left->kind == TY_NULL)
            return right;
        if (left->nullable)
            return ty_non_null(c, left);
        if (left->kind == TY_ANY)
            return unify(c, left, right);
        return left;
    }
    case BoolOp_kind:
        infer(c, e->v.BoolOp.left);
        infer(c, e->v.BoolOp.right);
        return ty_bool(c);
    case UnaryOp_kind: {
        Ty *operand = infer(c, e->v.UnaryOp.operand);
        Ty *hooked;
        if (e->v.UnaryOp.op == Not)
            return ty_bool(c);
        if (e->v.UnaryOp.op == USub) {
            hooked = operator_hook_ret(c, operand, "__neg__");
            if (hooked)
                return hooked;
        }
        return operand;
    }
    case BinOp_kind: {
        Ty *l = infer(c, e->v.BinOp.left);
        Ty *r = infer(c, e->v.BinOp.right);
        return binop_type(c, e->v.BinOp.op, l, r);
    }
    case Compare_kind:
        infer(c, e->v.Compare.left);
        infer(c, e->v.Compare.right);
        return ty_bool(c);
    case Is_kind:
        infer(c, e->v.Is.value);
        return ty_bool(c);
    case In_kind:
        infer(c, e->v.In.item);
        infer(c, e->v.In.container);
        return ty_bool(c);
    case Cast_kind:
        infer(c, e->v.Cast.value);
        return typeref_to_ty(c, e->v.Cast.ty);
    case AddressOf_kind:
        return ty_ptr(c, infer(c, e->v.AddressOf.value));
    case Deref_kind: {
        Ty *t = infer(c, e->v.Deref.value);
        if (t->kind == TY_PTR)
            return t->a ? t->a : ty_any(c);
        if (t->kind == TY_CLASS)
            return t;
        return ty_any(c);
    }
    case SizeOf_kind:
        return ty_int(c);
    case Call_kind: {
        Ty *callee = infer(c, e->v.Call.func);
        return call_result(c, e, callee);
    }
    case New_kind: {
        StructInfo *si = find_struct(c, e->v.New.cls);
        ClassInfo *ci = find_class(c, e->v.New.cls);
        size_t nargs = asdl_seq_LEN(e->v.New.args);
        size_t i;
        if (si)
            return ty_struct(c, si->name);
        if (ci && ci->ctor) {
            for (i = 0; i < nargs && i < ci->ctor->nparams; i++) {
                expr_ty arg = asdl_seq_GET(e->v.New.args, i);
                require_assignable(c, ci->ctor->params[i], infer(c, arg), arg,
                                   "constructor argument");
            }
            if (!ci->ctor->varargs && nargs != ci->ctor->nparams)
                check_error(c, e->lineno, e->col_offset,
                            "constructor of '%s' takes %zu argument(s)",
                            e->v.New.cls, ci->ctor->nparams);
        }
        else if (ci && nargs != 0) {
            check_error(c, e->lineno, e->col_offset,
                        "'%s' has no constructor", e->v.New.cls);
        }
        for (i = 0; i < nargs; i++)
            infer(c, asdl_seq_GET(e->v.New.args, i));
        return ty_class(c, e->v.New.cls);
    }
    case Attribute_kind:
        return attr_type(c, e, infer(c, e->v.Attribute.value));
    case PtrAttribute_kind:
        return attr_type(c, e, infer(c, e->v.PtrAttribute.value));
    case OptionalAttribute_kind: {
        Ty *base = infer(c, e->v.OptionalAttribute.value);
        Ty *result = attr_type(c, e, base);
        if (base->kind == TY_NULL || base->nullable || base->kind == TY_PTR)
            result = ty_nullable(c, result);
        return result;
    }
    case Name_kind: {
        const char *id = e->v.Name.id;
        Ty *t;
        if (!strcmp(id, "self"))
            return c->current_class ? ty_class(c, c->current_class->name) : ty_any(c);
        if (!strcmp(id, "super"))
            return ty_any(c);
        t = lookup(c, id);
        if (!t) {
            if (!c->allow_unknown)
                check_error(c, e->lineno, e->col_offset,
                            "undefined name '%s'", id);
            return ty_any(c);
        }
        return t;
    }
    case Self_kind:
        return c->current_class ? ty_class(c, c->current_class->name) : ty_any(c);
    case Constant_kind:
        return constant_type(c, e->v.Constant.value);
    case Array_kind:
        return ty_seq(c, TY_LIST, ty_any(c));
    case Range_kind: {
        infer(c, e->v.Range.start);
        infer(c, e->v.Range.stop);
        if (e->v.Range.step)
            infer(c, e->v.Range.step);
        return ty_seq(c, TY_LIST, ty_int(c));
    }
    case Subscript_kind: {
        Ty *container = infer(c, e->v.Subscript.value);
        if (e->v.Subscript.index->kind == Slice_kind) {
            expr_ty sl = e->v.Subscript.index;
            if (sl->v.Slice.lower)
                infer(c, sl->v.Slice.lower);
            if (sl->v.Slice.upper)
                infer(c, sl->v.Slice.upper);
            if (sl->v.Slice.step)
                infer(c, sl->v.Slice.step);
            return is_container(container) || container->kind == TY_STR
                ? container : ty_any(c);
        }
        infer(c, e->v.Subscript.index);
        if (container->kind == TY_DICT)
            return container->b;
        if (container->kind == TY_LIST || container->kind == TY_TUPLE ||
            container->kind == TY_SET || container->kind == TY_FROZENSET)
            return container->a;
        if (container->kind == TY_STR)
            return ty_str(c);
        if (container->kind == TY_PTR)
            return container->a ? container->a : ty_any(c);
        {
            Ty *hooked = operator_hook_ret(c, container, "__getitem__");
            if (hooked)
                return hooked;
        }
        return ty_any(c);
    }
    case Slice_kind:
        return ty_any(c);
    case FString_kind: {
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.FString.values); i++)
            infer(c, asdl_seq_GET(e->v.FString.values, i));
        return ty_str(c);
    }
    case FormattedValue_kind:
        infer(c, e->v.FormattedValue.value);
        return ty_str(c);
    case List_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.List.elements); i++)
            elem = unify(c, elem, infer(c, asdl_seq_GET(e->v.List.elements, i)));
        return ty_seq(c, TY_LIST, elem);
    }
    case Tuple_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Tuple.elements); i++)
            elem = unify(c, elem, infer(c, asdl_seq_GET(e->v.Tuple.elements, i)));
        return ty_seq(c, TY_TUPLE, elem);
    }
    case Dict_kind: {
        Ty *key = NULL, *value = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Dict.keys); i++) {
            expr_ty k = asdl_seq_GET(e->v.Dict.keys, i);
            expr_ty v = asdl_seq_GET(e->v.Dict.values, i);
            if (k)
                key = unify(c, key, infer(c, k));
            value = unify(c, value, infer(c, v));
        }
        return ty_dict(c, key, value);
    }
    case Set_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Set.elements); i++)
            elem = unify(c, elem, infer(c, asdl_seq_GET(e->v.Set.elements, i)));
        return ty_seq(c, TY_SET, elem);
    }
    case Spread_kind:
        return infer(c, e->v.Spread.value);
    case ListComp_kind:
    case SetComp_kind: {
        int is_set = e->kind == SetComp_kind;
        Ty *elem;
        push_scope(c);
        define(c, is_set ? e->v.SetComp.name : e->v.ListComp.name, ty_any(c));
        infer(c, is_set ? e->v.SetComp.iter : e->v.ListComp.iter);
        if (is_set ? e->v.SetComp.cond : e->v.ListComp.cond)
            infer(c, is_set ? e->v.SetComp.cond : e->v.ListComp.cond);
        elem = infer(c, is_set ? e->v.SetComp.elt : e->v.ListComp.elt);
        pop_scope(c);
        return ty_seq(c, is_set ? TY_SET : TY_LIST, elem);
    }
    case DictComp_kind: {
        Ty *key, *value;
        push_scope(c);
        define(c, e->v.DictComp.name, ty_any(c));
        infer(c, e->v.DictComp.iter);
        if (e->v.DictComp.cond)
            infer(c, e->v.DictComp.cond);
        key = infer(c, e->v.DictComp.key);
        value = infer(c, e->v.DictComp.value);
        pop_scope(c);
        return ty_dict(c, key, value);
    }
    case Incr_kind:
        return infer(c, e->v.Incr.target);
    }
    return ty_any(c);
}

/* ------------------------------------------------------------------ 선언 수집 */

static void collect_stmt(Checker *c, stmt_ty s);

static void collect_body(Checker *c, asdl_seq *body)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        collect_stmt(c, asdl_seq_GET(body, i));
}

static void class_add_member(Checker *c, ClassInfo *ci, const char *name, Ty *ty,
                             Ty *fn, int is_method, access_ty access)
{
    Member *m = Arena_Alloc(c->arena, sizeof(Member));
    if (is_method) {
        Member *old = class_member(ci, name);
        if (old && old->is_method) {
            old->fn = ty_overload_append(c, old->fn, fn);
            return;
        }
    }
    m->name = name;
    m->ty = ty;
    m->fn = fn;
    m->is_method = is_method;
    m->access = access;
    m->next = ci->members;
    ci->members = m;
}

static void collect_class(Checker *c, stmt_ty s)
{
    ClassInfo *ci = find_class(c, s->v.ClassDef.name);
    size_t i, n = asdl_seq_LEN(s->v.ClassDef.members);
    TParamSave save;

    if (!ci) {
        ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
        memset(ci, 0, sizeof(*ci));
        ci->name = s->v.ClassDef.name;
        ci->line = s->lineno;
        ci->col = s->col_offset;
        ci->next = c->classes;
        c->classes = ci;
    }
    if (s->v.ClassDef.base && s->v.ClassDef.base->kind == Named_kind)
        ci->base_name = s->v.ClassDef.base->v.Named.name;
    ci->ninterfaces = asdl_seq_LEN(s->v.ClassDef.interfaces);
    if (ci->ninterfaces) {
        ci->interfaces = Arena_Alloc(c->arena, ci->ninterfaces * sizeof(const char *));
        for (i = 0; i < ci->ninterfaces; i++) {
            typeref_ty iface = asdl_seq_GET(s->v.ClassDef.interfaces, i);
            ci->interfaces[i] = iface && iface->kind == Named_kind
                ? iface->v.Named.name : "";
        }
    }

    push_tparams(c, s->v.ClassDef.type_params, &save);
    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(s->v.ClassDef.members, i);
        switch (m->kind) {
        case Field_kind:
            class_add_member(c, ci, m->v.Field.name, typeref_to_ty(c, m->v.Field.ty),
                             NULL, 0, m->v.Field.access);
            break;
        case Property_kind:
            class_add_member(c, ci, m->v.Property.name, typeref_to_ty(c, m->v.Property.ty),
                             NULL, 0, m->v.Property.access);
            break;
        case Method_kind:
            class_add_member(c, ci, m->v.Method.name, NULL,
                             params_to_func(c, m->v.Method.params, m->v.Method.returns, 0),
                             1, m->v.Method.access);
            break;
        case Constructor_kind:
            ci->ctor = params_to_func(c, m->v.Constructor.params, NULL, 0);
            break;
        case Getter_kind:
            if (!class_member(ci, m->v.Getter.name))
                class_add_member(c, ci, m->v.Getter.name, ty_any(c), NULL, 0, Public);
            break;
        case Setter_kind:
            if (!class_member(ci, m->v.Setter.name))
                class_add_member(c, ci, m->v.Setter.name, ty_any(c), NULL, 0, Public);
            break;
        }
    }
    pop_tparams(c, &save);
}

static void collect_interface(Checker *c, stmt_ty s)
{
    ClassInfo *ci = find_class(c, s->v.InterfaceDef.name);
    size_t i, n = asdl_seq_LEN(s->v.InterfaceDef.methods);

    if (!ci) {
        ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
        memset(ci, 0, sizeof(*ci));
        ci->name = s->v.InterfaceDef.name;
        ci->is_interface = 1;
        ci->line = s->lineno;
        ci->col = s->col_offset;
        ci->next = c->classes;
        c->classes = ci;
    }
    for (i = 0; i < n; i++) {
        signature_ty sig = asdl_seq_GET(s->v.InterfaceDef.methods, i);
        class_add_member(c, ci, sig->name, NULL,
                         params_to_func(c, sig->params, sig->returns, 0), 1, Public);
    }
}

static void collect_struct(Checker *c, stmt_ty s)
{
    StructInfo *si = find_struct(c, s->v.StructDef.tag);
    size_t i, n = asdl_seq_LEN(s->v.StructDef.members);
    size_t nfields = 0;

    for (i = 0; i < n; i++) {
        struct_member_ty m = asdl_seq_GET(s->v.StructDef.members, i);
        if (m->kind == StructField_kind)
            nfields++;
    }
    if (!si) {
        si = Arena_Alloc(c->arena, sizeof(StructInfo));
        memset(si, 0, sizeof(*si));
        si->name = s->v.StructDef.tag;
        si->alias = s->v.StructDef.alias;
        si->next = c->structs;
        c->structs = si;
    }
    si->nfields = nfields;
    si->fields = nfields ? Arena_Alloc(c->arena, nfields * sizeof(FieldInfo)) : NULL;
    nfields = 0;
    for (i = 0; i < n; i++) {
        struct_member_ty m = asdl_seq_GET(s->v.StructDef.members, i);
        if (m->kind == StructField_kind) {
            si->fields[nfields].name = m->v.StructField.name;
            si->fields[nfields].ty = typeref_to_ty(c, m->v.StructField.ty);
            nfields++;
        }
    }
}

static Ty *shallow_type(Checker *c, expr_ty e)
{
    if (!e)
        return ty_any(c);
    switch (e->kind) {
    case Constant_kind:
        return constant_type(c, e->v.Constant.value);
    case List_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.List.elements); i++)
            elem = unify(c, elem, shallow_type(c, asdl_seq_GET(e->v.List.elements, i)));
        return ty_seq(c, TY_LIST, elem);
    }
    case Tuple_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Tuple.elements); i++)
            elem = unify(c, elem, shallow_type(c, asdl_seq_GET(e->v.Tuple.elements, i)));
        return ty_seq(c, TY_TUPLE, elem);
    }
    case Dict_kind: {
        Ty *key = NULL, *value = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Dict.keys); i++) {
            expr_ty k = asdl_seq_GET(e->v.Dict.keys, i);
            if (k)
                key = unify(c, key, shallow_type(c, k));
            value = unify(c, value, shallow_type(c, asdl_seq_GET(e->v.Dict.values, i)));
        }
        return ty_dict(c, key, value);
    }
    case Set_kind: {
        Ty *elem = NULL;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(e->v.Set.elements); i++)
            elem = unify(c, elem, shallow_type(c, asdl_seq_GET(e->v.Set.elements, i)));
        return ty_seq(c, TY_SET, elem);
    }
    case Lambda_kind:
        return params_to_func(c, e->v.Lambda.params, e->v.Lambda.returns, 1);
    case New_kind: {
        StructInfo *si = find_struct(c, e->v.New.cls);
        if (si)
            return ty_struct(c, si->name);
        return ty_class(c, e->v.New.cls);
    }
    case Name_kind: {
        Ty *t = lookup(c, e->v.Name.id);
        return t ? t : ty_any(c);
    }
    default:
        return ty_any(c);
    }
}

static void collect_stmt(Checker *c, stmt_ty s)
{
    switch (s->kind) {
    case FunctionDef_kind: {
        TParamSave save;
        Ty *fn;
        push_tparams(c, s->v.FunctionDef.type_params, &save);
        fn = params_to_func(c, s->v.FunctionDef.params, s->v.FunctionDef.returns, 0);
        pop_tparams(c, &save);
        define_or_overload(c, s->v.FunctionDef.name, fn);
        break;
    }
    case ClassDef_kind:
        collect_class(c, s);
        define_or_update(c, s->v.ClassDef.name, ty_class(c, s->v.ClassDef.name));
        break;
    case InterfaceDef_kind:
        collect_interface(c, s);
        define_or_update(c, s->v.InterfaceDef.name, ty_class(c, s->v.InterfaceDef.name));
        break;
    case StructDef_kind:
        collect_struct(c, s);
        define_or_update(c, s->v.StructDef.tag, ty_struct(c, s->v.StructDef.tag));
        if (s->v.StructDef.alias)
            define_or_update(c, s->v.StructDef.alias, ty_struct(c, s->v.StructDef.tag));
        break;
    case ExceptionDef_kind: {
        ClassInfo *ci = find_class(c, s->v.ExceptionDef.name);
        if (!ci) {
            ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
            memset(ci, 0, sizeof(*ci));
            ci->name = s->v.ExceptionDef.name;
            ci->base_name = s->v.ExceptionDef.base ? s->v.ExceptionDef.base : "Exception";
            ci->is_exception = 1;
            ci->next = c->classes;
            c->classes = ci;
        }
        define_or_update(c, s->v.ExceptionDef.name, ty_class(c, s->v.ExceptionDef.name));
        break;
    }
    case VarDecl_kind: {
        Ty *ty;
        TParamSave save;
        push_tparams(c, NULL, &save);
        ty = typeref_to_ty(c, s->v.VarDecl.ty);
        pop_tparams(c, &save);
        if (ty->kind == TY_ANY && s->v.VarDecl.init &&
            s->v.VarDecl.init->kind == ExprInit_kind)
            ty = shallow_type(c, s->v.VarDecl.init->v.ExprInit.value);
        define_or_update(c, s->v.VarDecl.name, ty);
        break;
    }
    case Assign_kind:
        if (s->v.Assign.target->kind == Name_kind) {
            const char *name = s->v.Assign.target->v.Name.id;
            if (!lookup(c, name))
                define(c, name, shallow_type(c, s->v.Assign.value));
        }
        break;
    case Import_kind: {
        size_t n = asdl_seq_LEN(s->v.Import.path);
        const char *dotted = module_dotted_name(c, s->v.Import.path);
        const char *bind = s->v.Import.alias
            ? s->v.Import.alias : asdl_seq_GET(s->v.Import.path, n - 1);
        define_or_update(c, bind, ty_module(c, dotted));
        if (s->v.Import.wildcard)
            c->allow_unknown = 1;
        break;
    }
    case ImportFrom_kind:
        if (s->v.ImportFrom.name) {
            define_or_update(c, s->v.ImportFrom.alias ? s->v.ImportFrom.alias
                                                      : s->v.ImportFrom.name,
                             ty_any(c));
        }
        else {
            c->allow_unknown = 1;
        }
        break;
    case Export_kind:
        collect_stmt(c, s->v.Export.body);
        break;
    default:
        break;
    }
}

static void resolve_bases(Checker *c)
{
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        if (ci->base_name)
            ci->base = find_class(c, ci->base_name);
    }
}

/* implements한 인터페이스의 메서드가 모두 구현되었는지 확인한다. */
static void check_interfaces(Checker *c)
{
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        size_t k;
        if (ci->is_interface)
            continue;
        for (k = 0; k < ci->ninterfaces; k++) {
            ClassInfo *iface = find_class(c, ci->interfaces[k]);
            Member *im;
            if (!iface || !iface->is_interface)
                continue;            /* 외부 인터페이스는 모양을 모르므로 넘어간다 */
            for (im = iface->members; im; im = im->next) {
                Member *impl = class_member(ci, im->name);
                if (!impl || !impl->is_method) {
                    check_error(c, ci->line, ci->col,
                                "class '%s' does not implement interface '%s' method '%s'",
                                ci->name, iface->name, im->name);
                    continue;
                }
                if (!same_ty(c, impl->fn, im->fn))
                    check_error(c, ci->line, ci->col,
                                "'%s' method '%s' does not match interface '%s'",
                                ci->name, im->name, iface->name);
            }
        }
    }
}

/* ------------------------------------------------------------------ 문장 검사 */

static void check_body(Checker *c, asdl_seq *body);
static void check_stmt(Checker *c, stmt_ty s);

static void check_function(Checker *c, asdl_seq *type_params, asdl_seq *params,
                           ret_ty ret, asdl_seq *body, expr_ty body_expr,
                           ClassInfo *cls, int lambda)
{
    ClassInfo *saved_class = c->current_class;
    Ty *saved_ret = c->current_ret;
    int saved_known = c->current_ret_known;
    int saved_in_fn = c->in_function;
    TParamSave save;
    size_t i, n = params ? asdl_seq_LEN(params) : 0;

    c->current_class = cls;
    c->in_function = 1;
    if (ret) {
        c->current_ret = ret->kind == ValueRet_kind
            ? typeref_to_ty(c, ret->v.ValueRet.ty) : ty_void(c);
        c->current_ret_known = ret->kind == ValueRet_kind;
    }
    else {
        c->current_ret = ty_void(c);
        c->current_ret_known = 0;
    }

    push_tparams(c, type_params, &save);
    push_scope(c);
    if (cls)
        define(c, "self", ty_class(c, cls->name));
    for (i = 0; i < n; i++) {
        if (lambda) {
            lparam_ty p = asdl_seq_GET(params, i);
            define(c, p->name, p->ty ? typeref_to_ty(c, p->ty) : ty_any(c));
        }
        else {
            param_ty p = asdl_seq_GET(params, i);
            define(c, p->name, p->ty ? typeref_to_ty(c, p->ty) : ty_any(c));
        }
    }
    if (!lambda) {
        collect_body(c, body);
        check_body(c, body);
    }
    else {
        infer(c, body_expr);
    }
    pop_scope(c);
    pop_tparams(c, &save);

    c->current_class = saved_class;
    c->current_ret = saved_ret;
    c->current_ret_known = saved_known;
    c->in_function = saved_in_fn;
}

static void check_class(Checker *c, stmt_ty s)
{
    ClassInfo *ci = find_class(c, s->v.ClassDef.name);
    size_t i, n = asdl_seq_LEN(s->v.ClassDef.members);
    ClassInfo *saved = c->current_class;

    c->current_class = ci;
    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(s->v.ClassDef.members, i);
        switch (m->kind) {
        case Field_kind:
            if (m->v.Field.value) {
                Ty *declared = typeref_to_ty(c, m->v.Field.ty);
                Ty *actual = infer(c, m->v.Field.value);
                require_assignable(c, declared, actual, m->v.Field.value,
                                   "field initializer");
            }
            break;
        case Method_kind:
            check_function(c, m->v.Method.type_params, m->v.Method.params,
                           m->v.Method.returns, m->v.Method.body, NULL, ci, 0);
            break;
        case Constructor_kind:
            check_function(c, NULL, m->v.Constructor.params, NULL,
                           m->v.Constructor.body, NULL, ci, 0);
            break;
        case Getter_kind: {
            ret_ty ret = NULL;
            size_t k, kn = asdl_seq_LEN(s->v.ClassDef.members);
            for (k = 0; k < kn; k++) {
                member_ty pm = asdl_seq_GET(s->v.ClassDef.members, k);
                if (pm->kind == Property_kind &&
                    strcmp(pm->v.Property.name, m->v.Getter.name) == 0) {
                    ret = _Aster_ValueRet(pm->v.Property.ty, c->arena);
                    break;
                }
            }
            check_function(c, NULL, NULL, ret, m->v.Getter.body, NULL, ci, 0);
            break;
        }
        case Setter_kind: {
            asdl_seq *params = _Aster_seq_new(1, c->arena);
            asdl_seq_SET(params, 0, m->v.Setter.param);
            check_function(c, NULL, params, NULL, m->v.Setter.body, NULL, ci, 0);
            break;
        }
        default:
            break;
        }
    }
    c->current_class = saved;
}

static void check_struct(Checker *c, stmt_ty s)
{
    size_t i, n = asdl_seq_LEN(s->v.StructDef.members);
    for (i = 0; i < n; i++) {
        struct_member_ty m = asdl_seq_GET(s->v.StructDef.members, i);
        if (m->kind == StructCtor_kind) {
            size_t j;
            push_scope(c);
            for (j = 0; j < asdl_seq_LEN(m->v.StructCtor.params); j++) {
                param_ty p = asdl_seq_GET(m->v.StructCtor.params, j);
                define(c, p->name, p->ty ? typeref_to_ty(c, p->ty) : ty_any(c));
            }
            for (j = 0; j < asdl_seq_LEN(m->v.StructCtor.inits); j++) {
                member_init_ty init = asdl_seq_GET(m->v.StructCtor.inits, j);
                infer(c, init->value);
            }
            check_body(c, m->v.StructCtor.body);
            pop_scope(c);
        }
    }
}

static void check_assign_target(Checker *c, expr_ty target, Ty *value)
{
    switch (target->kind) {
    case Name_kind: {
        Ty *t = lookup(c, target->v.Name.id);
        if (!t) {
            if (is_container(value) || value->kind == TY_FUNC)
                define(c, target->v.Name.id, value);
            return;
        }
        if (t->kind == TY_ANY && value->kind != TY_ANY) {
            define_or_update(c, target->v.Name.id, value);
            return;
        }
        require_assignable(c, t, value, target, "assignment");
        break;
    }
    case Attribute_kind: {
        Ty *base = infer(c, target->v.Attribute.value);
        Ty *member = attr_type(c, target, base);
        require_assignable(c, member, value, target, "property assignment");
        break;
    }
    case PtrAttribute_kind: {
        Ty *base = infer(c, target->v.PtrAttribute.value);
        Ty *member = attr_type(c, target, base);
        require_assignable(c, member, value, target, "field assignment");
        break;
    }
    case Subscript_kind: {
        Ty *container = infer(c, target->v.Subscript.value);
        if (target->v.Subscript.index->kind == Slice_kind) {
            infer(c, target->v.Subscript.index);
            if (container->kind == TY_LIST && value->kind == TY_LIST)
                require_assignable(c, container->a, value->a, target, "slice assignment");
            break;
        }
        infer(c, target->v.Subscript.index);
        if (container->kind == TY_LIST || container->kind == TY_DICT ||
            container->kind == TY_PTR) {
            Ty *elem = container->kind == TY_DICT ? container->b
                     : container->kind == TY_PTR ? container->a
                     : container->a;
            require_assignable(c, elem, value, target, "element assignment");
        }
        break;
    }
    case Deref_kind: {
        Ty *t = infer(c, target->v.Deref.value);
        if (t->kind == TY_PTR && t->a)
            require_assignable(c, t->a, value, target, "dereference assignment");
        break;
    }
    case Tuple_kind: {
        Ty *elem = iter_element(c, value);
        size_t i;
        for (i = 0; i < asdl_seq_LEN(target->v.Tuple.elements); i++)
            check_assign_target(c, asdl_seq_GET(target->v.Tuple.elements, i), elem);
        break;
    }
    default:
        infer(c, target);
        break;
    }
}

static void check_body(Checker *c, asdl_seq *body)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        check_stmt(c, asdl_seq_GET(body, i));
}

static void check_stmt(Checker *c, stmt_ty s)
{
    switch (s->kind) {
    case FunctionDef_kind:
        check_function(c, s->v.FunctionDef.type_params, s->v.FunctionDef.params,
                       s->v.FunctionDef.returns, s->v.FunctionDef.body, NULL,
                       c->current_class, 0);
        break;
    case ClassDef_kind:
        check_class(c, s);
        break;
    case InterfaceDef_kind:
        break;
    case StructDef_kind:
        check_struct(c, s);
        break;
    case ExceptionDef_kind:
        break;
    case VarDecl_kind: {
        Ty *declared;
        TParamSave save;
        push_tparams(c, NULL, &save);
        declared = typeref_to_ty(c, s->v.VarDecl.ty);
        pop_tparams(c, &save);
        if (s->v.VarDecl.init) {
            initializer_ty init = s->v.VarDecl.init;
            if (init->kind == ExprInit_kind) {
                expr_ty value = init->v.ExprInit.value;
                Ty *actual = infer(c, value);
                if (value->kind == Lambda_kind) {
                    /* 람다는 함수 타입으로 선언하지 않는다: var 또는 반환 타입. */
                    if (declared->kind == TY_FUNC)
                        check_error(c, value->lineno, value->col_offset,
                                    "a lambda cannot be declared with a function type; "
                                    "use 'var' or the lambda's return type");
                    else if (declared->kind != TY_ANY && actual->kind == TY_FUNC &&
                             actual->ret && !assignable(c, declared, actual->ret))
                        check_error(c, value->lineno, value->col_offset,
                                    "lambda return type: expected %s but got %s",
                                    ty_name(declared), ty_name(actual->ret));
                    /* 변수에는 람다 자체가 들어간다. */
                    declared = actual;
                }
                else if (declared->kind == TY_ANY)
                    declared = actual;
                else
                    require_assignable(c, declared, actual, value,
                                       "variable initializer");
            }
        }
        define_or_update(c, s->v.VarDecl.name, declared);
        break;
    }
    case EnumDef_kind: {
        size_t i;
        for (i = 0; i < asdl_seq_LEN(s->v.EnumDef.members); i++) {
            enum_member_ty em = asdl_seq_GET(s->v.EnumDef.members, i);
            if (em->value)
                infer(c, em->value);
        }
        define(c, s->v.EnumDef.name, ty_any(c));
        break;
    }
    case Destructure_kind: {
        Ty *value = infer(c, s->v.Destructure.value);
        Ty *elem = iter_element(c, value);
        size_t i;
        for (i = 0; i < asdl_seq_LEN(s->v.Destructure.names); i++)
            define(c, asdl_seq_GET(s->v.Destructure.names, i), elem);
        break;
    }
    case Assert_kind:
        infer(c, s->v.Assert.test);
        if (s->v.Assert.message)
            infer(c, s->v.Assert.message);
        break;
    case Assign_kind: {
        Ty *value = infer(c, s->v.Assign.value);
        Ty *target_ty = lookup(c, s->v.Assign.target->kind == Name_kind
                                  ? s->v.Assign.target->v.Name.id : "");
        (void)target_ty;
        if (s->v.Assign.op != AssignEq) {
            Ty *current = NULL;
            if (s->v.Assign.target->kind == Name_kind)
                current = lookup(c, s->v.Assign.target->v.Name.id);
            if (current)
                value = binop_type(c, Add, current, value);
        }
        check_assign_target(c, s->v.Assign.target, value);
        break;
    }
    case If_kind:
        infer(c, s->v.If.test);
        push_scope(c);
        check_body(c, s->v.If.body);
        pop_scope(c);
        push_scope(c);
        check_body(c, s->v.If.orelse);
        pop_scope(c);
        break;
    case While_kind:
        infer(c, s->v.While.test);
        push_scope(c);
        check_body(c, s->v.While.body);
        pop_scope(c);
        break;
    case For_kind:
        push_scope(c);
        check_stmt(c, s->v.For.init);
        infer(c, s->v.For.test);
        push_scope(c);
        check_body(c, s->v.For.body);
        pop_scope(c);
        check_stmt(c, s->v.For.step);
        pop_scope(c);
        break;
    case ForRange_kind: {
        Ty *declared;
        TParamSave save;
        push_scope(c);
        if (s->v.ForRange.iter->start)
            infer(c, s->v.ForRange.iter->start);
        if (s->v.ForRange.iter->stop)
            infer(c, s->v.ForRange.iter->stop);
        if (s->v.ForRange.iter->step)
            infer(c, s->v.ForRange.iter->step);
        push_tparams(c, NULL, &save);
        declared = typeref_to_ty(c, s->v.ForRange.ty);
        pop_tparams(c, &save);
        define(c, s->v.ForRange.name, declared->kind == TY_ANY ? ty_int(c) : declared);
        push_scope(c);
        check_body(c, s->v.ForRange.body);
        pop_scope(c);
        pop_scope(c);
        break;
    }
    case ForIn_kind: {
        Ty *iterable = infer(c, s->v.ForIn.iter);
        Ty *elem = iter_element(c, iterable);
        Ty *declared;
        TParamSave save;
        push_scope(c);
        push_tparams(c, NULL, &save);
        declared = typeref_to_ty(c, s->v.ForIn.ty);
        pop_tparams(c, &save);
        if (declared->kind == TY_ANY)
            declared = elem;
        else if (elem->kind != TY_ANY && !assignable(c, declared, elem)) {
            check_error(c, s->lineno, s->col_offset,
                        "for-in variable: expected %s but iterating over %s",
                        ty_name(declared), ty_name(elem));
        }
        define(c, s->v.ForIn.name, declared);
        push_scope(c);
        check_body(c, s->v.ForIn.body);
        pop_scope(c);
        pop_scope(c);
        break;
    }
    case Try_kind: {
        size_t i;
        push_scope(c);
        check_body(c, s->v.Try.body);
        for (i = 0; i < asdl_seq_LEN(s->v.Try.handlers); i++) {
            handler_ty h = asdl_seq_GET(s->v.Try.handlers, i);
            push_scope(c);
            define(c, h->name, ty_class(c, h->ty && h->ty->kind == Named_kind
                                              ? h->ty->v.Named.name
                                              : "Exception"));
            check_body(c, h->body);
            pop_scope(c);
        }
        push_scope(c);
        check_body(c, s->v.Try.finalbody);
        pop_scope(c);
        pop_scope(c);
        break;
    }
    case Yield_kind:
        if (!c->in_function)
            check_error(c, s->lineno, s->col_offset,
                        "'yield' is only allowed inside functions");
        if (s->v.Yield.value)
            infer(c, s->v.Yield.value);
        break;
    case Return_kind:
        if (s->v.Return.value) {
            Ty *value = infer(c, s->v.Return.value);
            if (c->current_ret_known) {
                require_assignable(c, c->current_ret, value, s->v.Return.value,
                                   "return value");
            }
            else if (c->current_ret && c->current_ret->kind == TY_VOID) {
                check_error(c, s->lineno, s->col_offset,
                            "cannot return a value from a void function");
            }
        }
        else if (c->current_ret_known) {
            check_error(c, s->lineno, s->col_offset,
                        "must return a value of type %s", ty_name(c->current_ret));
        }
        break;
    case Throw_kind:
        infer(c, s->v.Throw.value);
        break;
    case Break_kind:
    case Continue_kind:
        break;
    case Import_kind:
        handle_import(c, s);
        break;
    case ImportFrom_kind:
        handle_import_from(c, s);
        break;
    case Export_kind:
        check_stmt(c, s->v.Export.body);
        break;
    case ExprStmt_kind:
        infer(c, s->v.ExprStmt.value);
        break;
    }
}

/* ------------------------------------------------------------------ 내장 */

static void define_builtin(Checker *c, const char *name, Ty *ty)
{
    define(c, name, ty);
}

static void register_exception_class(Checker *c, const char *name, const char *base)
{
    ClassInfo *ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
    memset(ci, 0, sizeof(*ci));
    ci->name = name;
    ci->base_name = base;
    ci->is_exception = 1;
    ci->next = c->classes;
    c->classes = ci;
}

static void init_builtins(Checker *c)
{
    static const char *exceptions[] = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "ValueError", "TypeError", "NameError", "AttributeError", "KeyError",
        "IndexError", "RuntimeError", "NullPointerError", "LookupError",
        "AssertionError", "OSError", "EOFError", "IOException",
        "ImportError", "ModuleNotFoundError", "RecursionError", "NumberFormatError",
        "NotImplementedError", "StopIteration"
    };
    static const char *exc_parents[][2] = {
        {"ArithmeticError", "Exception"},
        {"ZeroDivisionError", "ArithmeticError"},
        {"OverflowError", "ArithmeticError"},
        {"LookupError", "Exception"},
        {"IndexError", "LookupError"},
        {"KeyError", "LookupError"},
        {"RuntimeError", "Exception"},
        {"NullPointerError", "RuntimeError"},
        {"RecursionError", "RuntimeError"},
        {"ImportError", "Exception"},
        {"ModuleNotFoundError", "ImportError"}
    };
    size_t i;

    push_scope(c);       /* 전역 스코프 */
    define_builtin(c, "memset", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "len", ty_func(c, NULL, 0, ty_int(c), 1));
    define_builtin(c, "list", ty_func(c, NULL, 0, ty_seq(c, TY_LIST, ty_any(c)), 1));
    define_builtin(c, "tuple", ty_func(c, NULL, 0, ty_seq(c, TY_TUPLE, ty_any(c)), 1));
    define_builtin(c, "dict", ty_func(c, NULL, 0, ty_dict(c, NULL, NULL), 1));
    define_builtin(c, "set", ty_func(c, NULL, 0, ty_seq(c, TY_SET, ty_any(c)), 1));
    define_builtin(c, "frozenset",
                   ty_func(c, NULL, 0, ty_seq(c, TY_FROZENSET, ty_any(c)), 1));
    define_builtin(c, "range", ty_func(c, NULL, 0, ty_seq(c, TY_LIST, ty_int(c)), 1));
    define_builtin(c, "next", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "typeName", ty_func(c, NULL, 0, ty_str(c), 1));
    define_builtin(c, "repr", ty_func(c, NULL, 0, ty_str(c), 1));
    define_builtin(c, "chr", ty_func(c, NULL, 0, ty_str(c), 1));
    define_builtin(c, "ord", ty_func(c, NULL, 0, ty_int(c), 1));
    define_builtin(c, "Scanner", ty_class(c, "Scanner"));
    define_builtin(c, "Files", ty_module(c, "Files"));

    for (i = 0; i < sizeof(exceptions) / sizeof(exceptions[0]); i++) {
        const char *parent = i == 0 ? NULL : "Exception";
        size_t j;
        for (j = 0; j < sizeof(exc_parents) / sizeof(exc_parents[0]); j++) {
            if (!strcmp(exc_parents[j][0], exceptions[i])) {
                parent = exc_parents[j][1];
                break;
            }
        }
        register_exception_class(c, exceptions[i], parent);
        define_builtin(c, exceptions[i], ty_class(c, exceptions[i]));
    }
}

/* ------------------------------------------------------------------ 진입점 */

int TypeCheck_ModuleEx(Arena *arena, mod_ty mod, const char *filename,
                       const TypeCheckEnv *env)
{
    Checker c;
    asdl_seq *body;

    memset(&c, 0, sizeof(c));
    c.arena = arena;
    c.filename = filename;
    c.env = env;

    if (!mod || mod->kind != Module_kind)
        return 0;
    body = mod->v.Module.body;

    init_builtins(&c);
    push_scope(&c);              /* 모듈 전역 스코프 */

    /* 1단계: 전역 선언과 타입 이름 수집 */
    collect_body(&c, body);
    resolve_bases(&c);
    check_interfaces(&c);

    /* 2단계: 문장 검사 (같은 전역 스코프 위에서) */
    check_body(&c, body);

    return c.errors > 0 ? 1 : 0;
}

int TypeCheck_Module(Arena *arena, mod_ty mod, const char *filename)
{
    return TypeCheck_ModuleEx(arena, mod, filename, NULL);
}
