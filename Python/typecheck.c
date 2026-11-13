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
    TY_OVERLOAD,
    TY_UNION
} TyKind;

typedef struct Ty Ty;
typedef struct Sym Sym;
typedef struct Scope Scope;
typedef struct Member Member;
typedef struct ClassInfo ClassInfo;
typedef struct StructInfo StructInfo;
typedef struct EnumInfo EnumInfo;
typedef struct FieldInfo FieldInfo;
typedef struct Checker Checker;
typedef struct ModuleInfo ModuleInfo;
typedef struct PendingModule PendingModule;

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
    /* TY_FUNC: 매개변수 이름, 기본값이 없는 앞쪽 매개변수 수, 가변 인자 */
    const char **param_names;
    size_t nrequired;
    int has_vararg;
    Ty *vararg_elem;
    int has_varkwarg;
    Ty *varkwarg_elem;
    /* TY_OVERLOAD */
    Ty **overloads;
    size_t noverloads;
    /* TY_UNION */
    Ty **members;
    size_t nmembers;
    /* TY_CLASS, TY_STRUCT: 타입 인자 (Echoer<int>의 int) */
    Ty **targs;
    size_t ntargs;
    /* TY_ANY: 제네릭 타입 매개변수의 경계 (T extends Number) */
    Ty *bound;
    /* TY_FUNC: 제네릭 함수의 타입 매개변수 */
    const char **tparams;
    Ty **tbounds;
    size_t ntparams;
};

struct Sym {
    const char *name;
    Ty *ty;
    int narrowed;            /* 흐름 분석으로 좁혀진 임시 타입이면 1 */
    int is_const;            /* const 선언이면 1. 재대입을 정적으로 거부한다. */
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
    int is_enum;             /* enum을 클래스처럼 다루기 위한 표식 (멤버는 변형/메서드) */
    int line, col;
    const char **interfaces;
    typeref_ty *interface_uses;
    size_t ninterfaces;
    const char **tparams;
    Ty **tparam_bounds;
    size_t ntparams;
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

struct EnumInfo {
    const char *name;
    const char **variants;
    Ty ***params;            /* variants[i]의 페이로드 타입 배열 */
    size_t *nparams;
    size_t nvariants;
    EnumInfo *next;
};

struct Checker {
    Arena *arena;
    const char *filename;
    int errors;
    Scope *scope;
    ClassInfo *classes;
    StructInfo *structs;
    EnumInfo *enums;
    ClassInfo *current_class;
    Ty *current_ret;
    int current_ret_known;
    int in_function;
    int in_generator;
    int loop_depth;
    int allow_unknown;
    int last_error_line;
    int last_error_col;
    char last_error_msg[512];
    const char **tparams;
    Ty **tparam_bounds;
    size_t ntparams;
    const TypeCheckEnv *env;
    ModuleInfo *modules;
    Checker *root;           /* 최상위 모듈 검사기 (순환 import 큐 공유) */
    PendingModule *pending;  /* 상속·본문 검사를 기다리는 import 모듈들 */
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
    ClassInfo *classes;      /* 모듈이 선언한 클래스/인터페이스 */
    StructInfo *structs;
    EnumInfo *enums;
    ModuleInfo *next;
};

/* 모양만 먼저 등록해 두고, 전체 모듈 그래프를 읽은 뒤 상속·본문을 검사한다.
   C#처럼 순환 import에서 서로의 선언을 볼 수 있게 하기 위한 3단계 검사 큐. */
struct PendingModule {
    Checker *checker;
    ModuleInfo *module;
    asdl_seq *body;
    PendingModule *next;
};

/* ------------------------------------------------------------------ 공통 */

static Ty *ty_new(Checker *c, TyKind kind) {
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

static Ty *ty_class(Checker *c, const char *name) {
    Ty *t = ty_new(c, TY_CLASS);
    t->name = name;
    return t;
}

static Ty *ty_struct(Checker *c, const char *name) {
    Ty *t = ty_new(c, TY_STRUCT);
    t->name = name;
    return t;
}

static Ty *ty_module(Checker *c, const char *name) {
    Ty *t = ty_new(c, TY_MODULE);
    t->name = name;
    return t;
}

static Ty *ty_ptr(Checker *c, Ty *base) {
    Ty *t = ty_new(c, TY_PTR);
    t->a = base;
    return t;
}

static Ty *ty_seq(Checker *c, TyKind kind, Ty *elem) {
    Ty *t = ty_new(c, kind);
    t->a = elem ? elem : ty_any(c);
    return t;
}

static Ty *ty_dict(Checker *c, Ty *key, Ty *value) {
    Ty *t = ty_new(c, TY_DICT);
    t->a = key ? key : ty_any(c);
    t->b = value ? value : ty_any(c);
    return t;
}

static Ty *ty_func(Checker *c, Ty **params, size_t nparams, Ty *ret, int varargs) {
    Ty *t = ty_new(c, TY_FUNC);
    t->params = params;
    t->nparams = nparams;
    t->ret = ret ? ret : ty_void(c);
    t->varargs = varargs;
    return t;
}

static Ty *ty_overload_append(Checker *c, Ty *existing, Ty *sig) {
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

static Ty *ty_nullable(Checker *c, Ty *base) {
    Ty *t;
    if (!base || base->kind == TY_ANY || base->nullable)
        return base ? base : ty_any(c);
    t = Arena_Alloc(c->arena, sizeof(Ty));
    *t = *base;
    t->nullable = 1;
    return t;
}

static Ty *ty_non_null(Checker *c, Ty *base) {
    Ty *t;
    if (!base || !base->nullable)
        return base;
    t = Arena_Alloc(c->arena, sizeof(Ty));
    *t = *base;
    t->nullable = 0;
    return t;
}

static void ty_write(char *buf, size_t cap, size_t *o, Ty *t) {
    size_t i;

    if (*o >= cap)
        return;
    if (!t) {
        snprintf(buf + *o, cap - *o, "any");
        *o += strlen(buf + *o);
        return;
    }
    switch (t->kind) {
    case TY_ANY:
        snprintf(buf + *o, cap - *o, "%s", t->name ? t->name : "any");
        break;
    case TY_VOID: snprintf(buf + *o, cap - *o, "void"); break;
    case TY_NULL: snprintf(buf + *o, cap - *o, "null"); break;
    case TY_INT: snprintf(buf + *o, cap - *o, "int"); break;
    case TY_FLOAT: snprintf(buf + *o, cap - *o, "float"); break;
    case TY_BOOL: snprintf(buf + *o, cap - *o, "bool"); break;
    case TY_STR: snprintf(buf + *o, cap - *o, "str"); break;
    case TY_CLASS:
        snprintf(buf + *o, cap - *o, "%s", t->name ? t->name : "class");
        *o += strlen(buf + *o);
        if (t->ntargs) {
            snprintf(buf + *o, cap - *o, "<");
            *o += strlen(buf + *o);
            for (i = 0; i < t->ntargs; i++) {
                if (i) {
                    snprintf(buf + *o, cap - *o, ", ");
                    *o += strlen(buf + *o);
                }
                ty_write(buf, cap, o, t->targs[i]);
            }
            snprintf(buf + *o, cap - *o, ">");
        }
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
    case TY_UNION:
        for (i = 0; i < t->nmembers; i++) {
            if (i) {
                snprintf(buf + *o, cap - *o, " | ");
                *o += strlen(buf + *o);
            }
            ty_write(buf, cap, o, t->members[i]);
        }
        if (i == 0)
            snprintf(buf + *o, cap - *o, "null");
        break;
    }
    while (*o < cap && buf[*o])
        (*o)++;
    if (t->nullable && t->kind != TY_NULL && t->kind != TY_UNION &&
        t->kind != TY_ANY && t->kind != TY_FUNC && *o < cap)
        snprintf(buf + *o, cap - *o, "?");
}

static const char *ty_name(Ty *t) {
    static char bufs[4][256];
    static int turn;
    size_t o = 0;
    char *buf = bufs[turn++ & 3];
    buf[0] = 0;
    ty_write(buf, sizeof(bufs[0]), &o, t);
    return buf;
}

static int is_num(Ty *t) {
    return t && (t->kind == TY_INT || t->kind == TY_FLOAT);
}

static int same_ty(Checker *c, Ty *a, Ty *b) {
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
    case TY_UNION:
        if (a->nmembers != b->nmembers)
            return 0;
        for (i = 0; i < a->nmembers; i++) {
            if (!same_ty(c, a->members[i], b->members[i]))
                return 0;
        }
        return 1;
    default:
        return 1;
    }
}

/* 합집합 타입을 만든다. 중첩 합집합은 펼치고, null 멤버는 nullable 플래그로
   옮기며, any가 있으면 any가 전체를 흡수한다. */
static Ty *ty_union(Checker *c, Ty **items, size_t n) {
    Ty **flat;
    size_t nflat = 0, i, j;
    int nullable = 0;
    Ty *t;

    if (n == 0)
        return ty_any(c);
    flat = Arena_Alloc(c->arena, n * sizeof(Ty *) + 1);
    for (i = 0; i < n; i++) {
        Ty *it = items[i];
        if (!it)
            continue;
        if (it->kind == TY_ANY)
            return ty_any(c);
        if (it->nullable)
            nullable = 1;
        if (it->kind == TY_UNION) {
            for (j = 0; j < it->nmembers; j++) {
                Ty *m = it->members[j];
                if (m->kind == TY_ANY)
                    return ty_any(c);
                if (m->nullable)
                    nullable = 1;
                if (m->kind != TY_NULL)
                    flat[nflat++] = ty_non_null(c, m);
            }
            continue;
        }
        if (it->kind == TY_NULL) {
            nullable = 1;
            continue;
        }
        flat[nflat++] = ty_non_null(c, it);
    }
    for (i = 0; i < nflat; i++) {
        for (j = i + 1; j < nflat; j++) {
            if (same_ty(c, flat[i], flat[j])) {
                size_t k;
                for (k = j; k + 1 < nflat; k++)
                    flat[k] = flat[k + 1];
                nflat--;
                j--;
            }
        }
    }
    if (nflat == 0)
        return nullable ? ty_nullable(c, ty_null(c)) : ty_any(c);
    if (nflat == 1)
        return nullable ? ty_nullable(c, flat[0]) : flat[0];
    t = ty_new(c, TY_UNION);
    t->members = Arena_Alloc(c->arena, nflat * sizeof(Ty *));
    memcpy(t->members, flat, nflat * sizeof(Ty *));
    t->nmembers = nflat;
    t->nullable = nullable;
    return t;
}

static int is_container(Ty *t) {
    return t && (t->kind == TY_LIST || t->kind == TY_TUPLE ||
                 t->kind == TY_DICT || t->kind == TY_SET ||
                 t->kind == TY_FROZENSET);
}

static ClassInfo *find_class(Checker *c, const char *name) {
    ClassInfo *ci;
    ModuleInfo *mi;
    for (ci = c->classes; ci; ci = ci->next) {
        if (!strcmp(ci->name, name))
            return ci;
    }
    /* import한 모듈이 선언한 클래스/인터페이스도 상속·인터페이스 해석에 쓴다. */
    for (mi = c->modules; mi; mi = mi->next) {
        for (ci = mi->classes; ci; ci = ci->next) {
            if (!strcmp(ci->name, name))
                return ci;
        }
    }
    return NULL;
}

static StructInfo *find_struct(Checker *c, const char *name) {
    StructInfo *si;
    ModuleInfo *mi;
    for (si = c->structs; si; si = si->next) {
        if (!strcmp(si->name, name) ||
            (si->alias && !strcmp(si->alias, name)))
            return si;
    }
    for (mi = c->modules; mi; mi = mi->next) {
        for (si = mi->structs; si; si = si->next) {
            if (!strcmp(si->name, name) ||
                (si->alias && !strcmp(si->alias, name)))
                return si;
        }
    }
    return NULL;
}

static EnumInfo *find_enum(Checker *c, const char *name) {
    EnumInfo *ei;
    ModuleInfo *mi;
    if (!name)
        return NULL;
    for (ei = c->enums; ei; ei = ei->next) {
        if (!strcmp(ei->name, name))
            return ei;
    }
    for (mi = c->modules; mi; mi = mi->next) {
        for (ei = mi->enums; ei; ei = ei->next) {
            if (!strcmp(ei->name, name))
                return ei;
        }
    }
    return NULL;
}

/* 모듈 자신의 선언만 찾는다. 모양 선등록 단계에서 import한 사본과 헷갈리지
   않도록 클래스/구조체/열거형을 로컬 목록에서만 조회한다. */
static ClassInfo *local_class(Checker *c, const char *name) {
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        if (!strcmp(ci->name, name))
            return ci;
    }
    return NULL;
}

static StructInfo *local_struct(Checker *c, const char *name) {
    StructInfo *si;
    for (si = c->structs; si; si = si->next) {
        if (!strcmp(si->name, name) ||
            (si->alias && !strcmp(si->alias, name)))
            return si;
    }
    return NULL;
}

static EnumInfo *local_enum(Checker *c, const char *name) {
    EnumInfo *ei;
    for (ei = c->enums; ei; ei = ei->next) {
        if (!strcmp(ei->name, name))
            return ei;
    }
    return NULL;
}

static Member *class_member(ClassInfo *ci, const char *name) {
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
static ClassInfo *member_owner(ClassInfo *ci, const char *name) {
    for (; ci; ci = ci->base) {
        Member *m;
        for (m = ci->members; m; m = m->next) {
            if (!strcmp(m->name, name))
                return ci;
        }
    }
    return NULL;
}

/* 순환 import로 로드 시점에 못 붙인 상위 클래스를 그때그때 다시 찾아 붙인다. */
static void ensure_base(Checker *c, ClassInfo *ci) {
    for (; ci; ci = ci->base) {
        if (!ci->base && ci->base_name)
            ci->base = find_class(c, ci->base_name);
    }
}

static int class_derives(Checker *c, const char *from, const char *to) {
    ClassInfo *ci = find_class(c, from);
    if (!ci)
        return 1;                    /* 바깥 클래스는 상속 관계를 모르므로 허용 */
    ensure_base(c, ci);
    for (; ci; ci = ci->base) {
        if (!strcmp(ci->name, to))
            return 1;
    }
    return 0;
}

/* 클래스(또는 그 조상)가 인터페이스를 구현하는지. */
static int class_implements(Checker *c, const char *from, const char *iface) {
    ClassInfo *ci = find_class(c, from);
    if (!ci)
        return 1;                    /* 바깥 클래스는 관계를 모르므로 허용 */
    if (ci->is_interface)
        return !strcmp(ci->name, iface);
    ensure_base(c, ci);
    for (; ci; ci = ci->base) {
        size_t i;
        for (i = 0; i < ci->ninterfaces; i++)
            if (!strcmp(ci->interfaces[i], iface))
                return 1;
    }
    return 0;
}

static void check_error(Checker *c, int line, int col, const char *fmt, ...);

static int is_type_param(Checker *c, const char *name) {
    size_t i;
    for (i = 0; i < c->ntparams; i++) {
        if (!strcmp(c->tparams[i], name))
            return 1;
    }
    return 0;
}

static Ty *tparam_bound(Checker *c, const char *name) {
    size_t i;
    for (i = 0; i < c->ntparams; i++) {
        if (!strcmp(c->tparams[i], name))
            return c->tparam_bounds ? c->tparam_bounds[i] : NULL;
    }
    return NULL;
}

static Ty *typeref_to_ty(Checker *c, typeref_ty ty);

static Ty *named_to_ty(Checker *c, const char *name, asdl_seq *args) {
    Ty *elem = NULL;
    size_t nargs = args ? asdl_seq_LEN(args) : 0;

    if (!strcmp(name, "var") || !strcmp(name, "any") || !strcmp(name, "void"))
        return ty_any(c);
    if (is_type_param(c, name)) {
        Ty *param = ty_any(c);
        param->name = name;          /* 제네릭 인자 치환용 표식 */
        param->bound = tparam_bound(c, name);
        return param;
    }
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
    {
        Ty *t = ty_class(c, name);
        if (nargs > 0) {
            size_t i;
            t->targs = Arena_Alloc(c->arena, nargs * sizeof(Ty *));
            for (i = 0; i < nargs; i++)
                t->targs[i] = typeref_to_ty(c, asdl_seq_GET(args, i));
            t->ntargs = nargs;
        }
        return t;
    }
}

static Ty *typeref_to_ty(Checker *c, typeref_ty ty) {
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
    case UnionTy_kind: {
        asdl_seq *members = ty->v.UnionTy.members;
        size_t n = members ? asdl_seq_LEN(members) : 0;
        Ty **tys = n ? Arena_Alloc(c->arena, n * sizeof(Ty *)) : NULL;
        size_t i;
        for (i = 0; i < n; i++)
            tys[i] = typeref_to_ty(c, asdl_seq_GET(members, i));
        return ty_union(c, tys, n);
    }
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

/*
 * 선언 쪽 타입(decl)의 타입 매개변수를 사용 쪽 인자(use)로 바꾼다.
 * 예: Mapper<T>의 T를 Mapper<int>의 int로.
 */
static Ty *substitute_ty(Checker *c, Ty *t, ClassInfo *decl, Ty *use) {
    size_t i;

    if (!t || !decl || !use)
        return t;
    if (t->kind == TY_ANY && t->name) {
        for (i = 0; i < decl->ntparams; i++) {
            if (!strcmp(decl->tparams[i], t->name))
                return i < use->ntargs ? use->targs[i] : t;
        }
        return t;
    }
    switch (t->kind) {
    case TY_PTR:
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET: {
        Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
        *out = *t;
        out->a = substitute_ty(c, t->a, decl, use);
        return out;
    }
    case TY_DICT: {
        Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
        *out = *t;
        out->a = substitute_ty(c, t->a, decl, use);
        out->b = substitute_ty(c, t->b, decl, use);
        return out;
    }
    case TY_UNION: {
        Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
        *out = *t;
        if (t->nmembers) {
            out->members = Arena_Alloc(c->arena, t->nmembers * sizeof(Ty *));
            for (i = 0; i < t->nmembers; i++)
                out->members[i] = substitute_ty(c, t->members[i], decl, use);
        }
        return out;
    }
    case TY_FUNC: {
        Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
        *out = *t;
        if (t->nparams) {
            out->params = Arena_Alloc(c->arena, t->nparams * sizeof(Ty *));
            for (i = 0; i < t->nparams; i++)
                out->params[i] = substitute_ty(c, t->params[i], decl, use);
        }
        out->ret = substitute_ty(c, t->ret, decl, use);
        return out;
    }
    case TY_OVERLOAD: {
        Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
        *out = *t;
        if (t->noverloads) {
            out->overloads = Arena_Alloc(c->arena, t->noverloads * sizeof(Ty *));
            for (i = 0; i < t->noverloads; i++)
                out->overloads[i] = substitute_ty(c, t->overloads[i], decl, use);
        }
        return out;
    }
    case TY_CLASS:
    case TY_STRUCT:
        if (t->ntargs) {
            Ty *out = Arena_Alloc(c->arena, sizeof(Ty));
            *out = *t;
            out->targs = Arena_Alloc(c->arena, t->ntargs * sizeof(Ty *));
            for (i = 0; i < t->ntargs; i++)
                out->targs[i] = substitute_ty(c, t->targs[i], decl, use);
            return out;
        }
        return t;
    default:
        return t;
    }
}

static Ty *unify(Checker *c, Ty *a, Ty *b) {
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
    if (a->kind == TY_UNION || b->kind == TY_UNION) {
        Ty *items[2];
        items[0] = a;
        items[1] = b;
        return ty_union(c, items, 2);
    }
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

/* 메서드가 하나뿐인 인터페이스(함수형 인터페이스)의 그 메서드. */
static Member *interface_single_method(Checker *c, Ty *iface) {
    ClassInfo *ci;
    Member *m, *only = NULL;
    size_t count = 0;

    if (!iface || iface->kind != TY_CLASS)
        return NULL;
    ci = find_class(c, iface->name);
    if (!ci || !ci->is_interface)
        return NULL;
    for (m = ci->members; m; m = m->next) {
        if (!m->is_method)
            continue;
        if (m->fn && m->fn->kind == TY_OVERLOAD)
            return NULL;              /* 추상 메서드가 둘 이상이다 */
        count++;
        only = m;
    }
    return count == 1 ? only : NULL;
}

static int lambda_fits_interface(Checker *c, Ty *iface, Ty *fn) {
    Member *m = interface_single_method(c, iface);
    ClassInfo *ci = find_class(c, iface->name);
    Ty *want;
    size_t i;

    if (!m || !m->fn || m->fn->kind != TY_FUNC || fn->kind != TY_FUNC)
        return 0;
    want = substitute_ty(c, m->fn, ci, iface);
    if (want->varargs || fn->varargs || want->nparams != fn->nparams)
        return 0;
    for (i = 0; i < fn->nparams; i++) {
        if (!same_ty(c, want->params[i], fn->params[i]))
            return 0;
    }
    return same_ty(c, want->ret, fn->ret);
}

static int assignable(Checker *c, Ty *dst, Ty *src) {
    if (!dst || !src)
        return 1;
    if (src->kind == TY_ANY)
        return 1;
    if (dst->kind == TY_ANY)
        return dst->bound ? assignable(c, dst->bound, src) : 1;
    if (src->kind == TY_NULL)
        return dst->nullable || dst->kind == TY_PTR || dst->kind == TY_NULL;
    if (dst->nullable) {
        if (src->kind == TY_NULL)
            return 1;
        return assignable(c, ty_non_null(c, dst), ty_non_null(c, src));
    }
    if (src->nullable)
        return 0;
    if (src->kind == TY_UNION) {
        size_t i;
        for (i = 0; i < src->nmembers; i++) {
            if (!assignable(c, dst, src->members[i]))
                return 0;
        }
        return 1;
    }
    if (dst->kind == TY_UNION) {
        size_t i;
        for (i = 0; i < dst->nmembers; i++) {
            if (assignable(c, dst->members[i], src))
                return 1;
        }
        return 0;
    }
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
        if (src->kind == TY_PTR)
            return assignable(c, dst, src->a ? src->a : ty_any(c));
        if (src->kind == TY_INT || src->kind == TY_FLOAT) {
            /* 원시 수 타입은 aster.lang.Number 경계를 만족한다. */
            return !strcmp(dst->name, "Number");
        }
        if (src->kind == TY_CLASS) {
            ClassInfo *dst_ci = find_class(c, dst->name);
            if (!strcmp(dst->name, src->name))
                return 1;
            if (dst_ci && dst_ci->is_interface)
                return class_implements(c, src->name, dst->name);
            return class_derives(c, src->name, dst->name);
        }
        if (src->kind == TY_STRUCT)
            return !strcmp(dst->name, src->name);
        if (src->kind == TY_FUNC) {
            ClassInfo *dst_ci = find_class(c, dst->name);
            return dst_ci && dst_ci->is_interface &&
                   lambda_fits_interface(c, dst, src);
        }
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

static void push_scope(Checker *c) {
    Scope *s = Arena_Alloc(c->arena, sizeof(Scope));
    s->parent = c->scope;
    s->syms = NULL;
    c->scope = s;
}

static void pop_scope(Checker *c) {
    if (c->scope)
        c->scope = c->scope->parent;
}

static void define(Checker *c, const char *name, Ty *ty) {
    Sym *s = Arena_Alloc(c->arena, sizeof(Sym));
    s->name = name;
    s->ty = ty ? ty : ty_any(c);
    s->narrowed = 0;
    s->next = c->scope->syms;
    c->scope->syms = s;
}

static void define_narrowed(Checker *c, const char *name, Ty *ty) {
    Sym *s = Arena_Alloc(c->arena, sizeof(Sym));
    s->name = name;
    s->ty = ty ? ty : ty_any(c);
    s->narrowed = 1;
    s->next = c->scope->syms;
    c->scope->syms = s;
}

/* skip_narrowed가 1이면 흐름 분석으로 좁힌 그림자를 건너뛴다. */
static Sym *lookup_sym(Checker *c, const char *name, int skip_narrowed) {
    Scope *sc;
    for (sc = c->scope; sc; sc = sc->parent) {
        Sym *s;
        for (s = sc->syms; s; s = s->next) {
            if (!strcmp(s->name, name) && !(skip_narrowed && s->narrowed))
                return s;
        }
    }
    return NULL;
}

static Ty *lookup(Checker *c, const char *name) {
    Sym *s = lookup_sym(c, name, 0);
    return s ? s->ty : NULL;
}

/* 이름이 이미 있으면 타입만 갱신한다. */
static void define_or_update(Checker *c, const char *name, Ty *ty) {
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

/* import한 const도 재대입 금지 정보를 함께 가져온다. */
static void define_or_update_const(Checker *c, const char *name, Ty *ty,
                                   int is_const) {
    define_or_update(c, name, ty);
    if (is_const) {
        Sym *s = lookup_sym(c, name, 0);
        if (s)
            s->is_const = 1;
    }
}

/*
 * 선언은 현재 스코프에만 넣는다. 바깥 스코프의 같은 이름(내장 함수 등)은
 * 가려져야 하므로 define_or_update처럼 바깥 심볼을 고치면 안 된다.
 */
static void define_shadow_const(Checker *c, const char *name, Ty *ty, int is_const) {
    Sym *s;
    for (s = c->scope->syms; s; s = s->next) {
        if (!strcmp(s->name, name)) {
            if (s->ty->kind == TY_ANY && ty && ty->kind != TY_ANY)
                s->ty = ty;
            if (is_const)
                s->is_const = 1;
            return;
        }
    }
    define(c, name, ty);
    if (is_const)
        c->scope->syms->is_const = 1;
}

/* 같은 이름이 있으면 타입을 덮어쓴다(import 바인딩용). */
static void define_binding(Checker *c, const char *name, Ty *ty) {
    Scope *sc;
    for (sc = c->scope; sc; sc = sc->parent) {
        Sym *s;
        for (s = sc->syms; s; s = s->next) {
            if (!strcmp(s->name, name)) {
                s->ty = ty;
                return;
            }
        }
    }
    define(c, name, ty);
}

/* 같은 이름의 함수 선언은 오버로드로 합친다. */
static void define_or_overload(Checker *c, const char *name, Ty *fn) {
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

static void check_error(Checker *c, int line, int col, const char *fmt, ...) {
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* 같은 식을 두 번 추론하면서 같은 진단이 반복되는 경우를 걸러낸다. */
    if (line == c->last_error_line && col == c->last_error_col &&
        !strcmp(c->last_error_msg, buf))
        return;
    c->last_error_line = line;
    c->last_error_col = col;
    snprintf(c->last_error_msg, sizeof c->last_error_msg, "%s", buf);
    fprintf(stderr, "%s:%d:%d: error: %s\n",
            c->filename ? c->filename : "<input>", line, col + 1, buf);
    c->errors++;
}

static void require_assignable(Checker *c, Ty *dst, Ty *src, expr_ty at, const char *what) {
    if (!assignable(c, dst, src)) {
        check_error(c, at->lineno, at->col_offset,
                    "%s: expected %s but got %s",
                    what, ty_name(dst), ty_name(src));
    }
}

/* ------------------------------------------------------------------ 함수 시그니처 */

static Ty *params_to_func(Checker *c, asdl_seq *params, ret_ty ret, int lambda_params) {
    size_t n = params ? asdl_seq_LEN(params) : 0;
    Ty **tys = n ? Arena_Alloc(c->arena, n * sizeof(Ty *)) : NULL;
    const char **names = n ? Arena_Alloc(c->arena, n * sizeof(const char *)) : NULL;
    Ty *rt, *fn;
    Ty *vararg_elem = NULL;
    Ty *varkwarg_elem = NULL;
    int has_vararg = 0;
    int has_varkwarg = 0;
    size_t count = 0, nrequired = 0, i;

    for (i = 0; i < n; i++) {
        typeref_ty ty;
        const char *pname;
        Ty *pt;

        if (lambda_params) {
            lparam_ty p = asdl_seq_GET(params, i);
            ty = p->ty;
            pname = p->name;
            pt = ty ? typeref_to_ty(c, ty) : ty_any(c);
        }
        else {
            param_ty p = asdl_seq_GET(params, i);
            ty = p->ty;
            pname = p->name;
            pt = ty ? typeref_to_ty(c, ty) : ty_any(c);
            if (p->variadic == 1) {
                has_vararg = 1;
                vararg_elem = pt;
                continue;
            }
            if (p->variadic == 2) {
                has_varkwarg = 1;
                varkwarg_elem = pt;
                continue;
            }
            tys[count] = pt;
            names[count] = pname;
            if (!p->default_value)
                nrequired = count + 1;
            count++;
            continue;
        }
        tys[count] = pt;
        names[count] = pname;
        nrequired = count + 1;
        count++;
    }
    if (ret) {
        rt = ret->kind == ValueRet_kind ? typeref_to_ty(c, ret->v.ValueRet.ty)
                                        : ty_void(c);
    }
    else {
        rt = ty_void(c);
    }
    fn = ty_func(c, tys, count, rt, 0);
    fn->param_names = names;
    fn->nrequired = nrequired;
    fn->has_vararg = has_vararg;
    fn->vararg_elem = vararg_elem;
    fn->has_varkwarg = has_varkwarg;
    fn->varkwarg_elem = varkwarg_elem;
    return fn;
}

/* 제네릭 함수의 타입 매개변수 정보를 함수 타입에 붙인다. */
static void attach_tparams(Checker *c, Ty *fn, asdl_seq *type_params) {
    size_t n = type_params ? asdl_seq_LEN(type_params) : 0;
    size_t i;

    if (!fn || fn->kind != TY_FUNC || n == 0)
        return;
    fn->tparams = Arena_Alloc(c->arena, n * sizeof(const char *));
    fn->tbounds = Arena_Alloc(c->arena, n * sizeof(Ty *));
    for (i = 0; i < n; i++) {
        type_param_ty tp = asdl_seq_GET(type_params, i);
        fn->tparams[i] = tp->name;
        fn->tbounds[i] = tp->bound ? typeref_to_ty(c, tp->bound) : NULL;
    }
    fn->ntparams = n;
}

/* ------------------------------------------------------------------ 타입 파라미터 문맥 */

typedef struct {
    const char **saved;
    Ty **saved_bounds;
    size_t saved_n;
} TParamSave;

static void push_tparams(Checker *c, asdl_seq *type_params, TParamSave *save) {
    size_t n = type_params ? asdl_seq_LEN(type_params) : 0;
    size_t nold = c->ntparams;
    size_t i;
    save->saved = c->tparams;
    save->saved_bounds = c->tparam_bounds;
    save->saved_n = c->ntparams;
    /* 새 타입 매개변수는 바깥 문맥(제네릭 클래스의 T 등) 위에 쌓인다. */
    if (n == 0)
        return;
    c->tparams = Arena_Alloc(c->arena, (nold + n) * sizeof(const char *));
    c->tparam_bounds = Arena_Alloc(c->arena, (nold + n) * sizeof(Ty *));
    for (i = 0; i < nold; i++) {
        c->tparams[i] = save->saved[i];
        c->tparam_bounds[i] = save->saved_bounds ? save->saved_bounds[i] : NULL;
    }
    for (i = 0; i < n; i++) {
        type_param_ty tp = asdl_seq_GET(type_params, i);
        c->tparams[nold + i] = tp->name;
    }
    c->ntparams = nold + n;
    for (i = 0; i < n; i++) {
        type_param_ty tp = asdl_seq_GET(type_params, i);
        c->tparam_bounds[nold + i] = tp->bound ? typeref_to_ty(c, tp->bound) : NULL;
    }
}

static void pop_tparams(Checker *c, TParamSave *save) {
    c->tparams = save->saved;
    c->tparam_bounds = save->saved_bounds;
    c->ntparams = save->saved_n;
}

/* ------------------------------------------------------------------ 표현식 */

static Ty *infer(Checker *c, expr_ty e);
static void narrow_true(Checker *c, expr_ty e);
static void narrow_false(Checker *c, expr_ty e);

static Ty *constant_type(Checker *c, Value v) {
    switch (v.tag) {
    case VT_NULL: return ty_null(c);
    case VT_BOOL: return ty_bool(c);
    case VT_INT: return ty_int(c);
    case VT_FLOAT: return ty_float(c);
    case VT_OBJ: return ty_str(c);
    }
    return ty_any(c);
}

static Ty *iter_element(Checker *c, Ty *t) {
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

static Ty *str_method_type(Checker *c, const char *name) {
    static const struct {
        const char *name;
        char ret;                /* i=int, f=float, s=str, b=bool, l=list<str>, v=void */
    } table[] = {
        {"upper", 's'}, {"lower", 's'}, {"strip", 's'}, {"trim", 's'},
        {"toUpperCase", 's'}, {"toLowerCase", 's'}, {"replace", 's'}, {"format", 's'},
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
static void preload_imports(Checker *c, asdl_seq *body);

static void pending_push(Checker *root, Checker *mc, ModuleInfo *mi,
                         asdl_seq *body) {
    PendingModule *pm = Arena_Alloc(root->arena, sizeof(PendingModule));
    PendingModule **tail = &root->pending;

    pm->checker = mc;
    pm->module = mi;
    pm->body = body;
    pm->next = NULL;
    while (*tail)
        tail = &(*tail)->next;
    *tail = pm;
}

/* 모듈 그래프를 모두 읽은 뒤 상속·인터페이스·본문을 순서대로 검사한다.
   검사 중 새 모듈이 로드되면 큐 꼬리에 붙으므로 끝까지 따라간다. */
static void flush_pending(Checker *c) {
    PendingModule *pm = c->pending;

    while (pm) {
        Checker *mc = pm->checker;
        resolve_bases(mc);
        check_interfaces(mc);
        check_body(mc, pm->body);
        c->errors += mc->errors;
        c->modules = mc->modules;
        pm = pm->next;
    }
    c->pending = NULL;
}

static const char *module_dotted_name(Checker *c, asdl_seq *path) {
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

static ModuleInfo *find_module(Checker *c, const char *name) {
    ModuleInfo *mi;
    for (mi = c->modules; mi; mi = mi->next) {
        if (!strcmp(mi->name, name))
            return mi;
    }
    return NULL;
}

static Ty *module_export_type(ModuleInfo *mi, const char *name) {
    Sym *s;
    if (!mi)
        return NULL;
    for (s = mi->exports; s; s = s->next) {
        if (!strcmp(s->name, name))
            return s->ty;
    }
    return NULL;
}

static Sym *module_export_sym(ModuleInfo *mi, const char *name) {
    Sym *s;
    if (!mi)
        return NULL;
    for (s = mi->exports; s; s = s->next) {
        if (!strcmp(s->name, name))
            return s;
    }
    return NULL;
}

/* C 런타임이 직접 제공해서 .ast 파일이 없는 모듈. */
static int is_opaque_module(const char *name) {
    return !strcmp(name, "System") || !strncmp(name, "System.", 7) ||
           !strcmp(name, "Files") || !strcmp(name, "aster.util.regex.Native");
}

static char *read_source_file(const char *path, size_t *size) {
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

static const char *exported_name(stmt_ty s) {
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

static void module_add_export(Checker *c, ModuleInfo *mi, const char *name) {
    Sym *s;
    Sym *local;
    Ty *ty;

    if (module_export_type(mi, name))
        return;
    ty = lookup(c, name);
    local = lookup_sym(c, name, 0);
    s = Arena_Alloc(c->arena, sizeof(Sym));
    s->name = name;
    s->ty = ty ? ty : ty_any(c);
    s->is_const = local ? local->is_const : 0;
    s->next = mi->exports;
    mi->exports = s;
    mi->nexports++;
}

static int has_export_wrappers(asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (((stmt_ty)asdl_seq_GET(body, i))->kind == Export_kind)
            return 1;
    }
    return 0;
}

static void snapshot_exports(Checker *c, ModuleInfo *mi, asdl_seq *body) {
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

static void preregister_stmt(Checker *c, stmt_ty s) {
    switch (s->kind) {
    case ClassDef_kind:
        if (!local_class(c, s->v.ClassDef.name)) {
            ClassInfo *ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
            memset(ci, 0, sizeof(*ci));
            ci->name = s->v.ClassDef.name;
            ci->line = s->lineno;
            ci->col = s->col_offset;
            ci->next = c->classes;
            c->classes = ci;
        }
        break;
    case InterfaceDef_kind:
        if (!local_class(c, s->v.InterfaceDef.name)) {
            ClassInfo *ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
            memset(ci, 0, sizeof(*ci));
            ci->name = s->v.InterfaceDef.name;
            ci->is_interface = 1;
            ci->line = s->lineno;
            ci->col = s->col_offset;
            ci->next = c->classes;
            c->classes = ci;
        }
        break;
    case ExceptionDef_kind:
        if (!local_class(c, s->v.ExceptionDef.name)) {
            ClassInfo *ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
            memset(ci, 0, sizeof(*ci));
            ci->name = s->v.ExceptionDef.name;
            ci->base_name = s->v.ExceptionDef.base ? s->v.ExceptionDef.base : "Exception";
            ci->is_exception = 1;
            ci->line = s->lineno;
            ci->col = s->col_offset;
            ci->next = c->classes;
            c->classes = ci;
        }
        break;
    case StructDef_kind:
        if (!local_struct(c, s->v.StructDef.tag)) {
            StructInfo *si = Arena_Alloc(c->arena, sizeof(StructInfo));
            memset(si, 0, sizeof(*si));
            si->name = s->v.StructDef.tag;
            si->alias = s->v.StructDef.alias;
            si->next = c->structs;
            c->structs = si;
        }
        break;
    case EnumDef_kind:
        if (!local_enum(c, s->v.EnumDef.name)) {
            EnumInfo *ei = Arena_Alloc(c->arena, sizeof(EnumInfo));
            memset(ei, 0, sizeof(*ei));
            ei->name = s->v.EnumDef.name;
            ei->next = c->enums;
            c->enums = ei;
        }
        break;
    case Export_kind:
        preregister_stmt(c, s->v.Export.body);
        break;
    default:
        break;
    }
}

/* 타입 이름과 위치만 먼저 등록한다. 순환 import에서 서로의 선언을
   참조할 수 있도록 전체 모듈 그래프를 읽기 전에 실행한다. */
static void preregister_shapes(Checker *c, asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        preregister_stmt(c, asdl_seq_GET(body, i));
}

static ModuleInfo *load_module(Checker *c, const char *name, int line, int col) {
    ModuleInfo *mi = find_module(c, name);
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
        Checker *mc;

        mc = Arena_Alloc(c->arena, sizeof(Checker));
        memset(mc, 0, sizeof(*mc));
        mc->arena = c->arena;
        mc->filename = Arena_Strndup(c->arena, file, strlen(file));
        mc->env = c->env;
        mc->modules = c->modules;
        mc->root = c->root ? c->root : c;

        init_builtins(mc);
        push_scope(mc);
        /* 순환 import 중에도 이 모듈의 타입 이름을 먼저 보이게 한다. */
        preregister_shapes(mc, body);
        mi->classes = mc->classes;
        mi->structs = mc->structs;
        mi->enums = mc->enums;
        c->modules = mc->modules;

        /* 상속·인터페이스·본문 검사는 전체 그래프를 읽은 뒤로 미룬다. */
        pending_push(mc->root, mc, mi, body);

        preload_imports(mc, body);
        collect_body(mc, body);
        /* 전부 모았으니 다른 모듈이 이 모듈의 타입을 온전히 쓸 수 있다. */
        mi->classes = mc->classes;
        mi->structs = mc->structs;
        mi->enums = mc->enums;
        snapshot_exports(mc, mi, body);

        c->modules = mc->modules;
    }
    mi->loading = 0;
    mi->resolved = 1;
    mi->ok = 1;
    return mi;
}

/* collect_body보다 먼저 import한 모듈을 읽어 클래스/인터페이스 정보를 준비한다. */
static void preload_imports(Checker *c, asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        stmt_ty s = asdl_seq_GET(body, i);
        if (s->kind == Import_kind) {
            const char *dotted = module_dotted_name(c, s->v.Import.path);
            load_module(c, dotted, s->lineno, s->col_offset);
        }
        else if (s->kind == ImportFrom_kind) {
            const char *dotted = module_dotted_name(c, s->v.ImportFrom.module);
            load_module(c, dotted, s->lineno, s->col_offset);
        }
    }
}

/* 모듈의 공개 이름을 현재 스코프에 정의한다. */
static void import_module_exports(Checker *c, ModuleInfo *mi) {
    Sym *s;
    if (!mi || !mi->ok || mi->opaque)
        return;
    for (s = mi->exports; s; s = s->next)
        define_or_update_const(c, s->name, s->ty, s->is_const);
}

static void handle_import(Checker *c, stmt_ty s) {
    size_t n = asdl_seq_LEN(s->v.Import.path);
    const char *dotted = module_dotted_name(c, s->v.Import.path);
    const char *bind = s->v.Import.alias
        ? s->v.Import.alias : asdl_seq_GET(s->v.Import.path, n - 1);
    ModuleInfo *mi = load_module(c, dotted, s->lineno, s->col_offset);
    Ty *direct = mi && !mi->opaque ? module_export_type(mi, bind) : NULL;

    define_binding(c, bind, direct ? direct : ty_module(c, dotted));
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

static void handle_import_from(Checker *c, stmt_ty s) {
    const char *dotted = module_dotted_name(c, s->v.ImportFrom.module);
    ModuleInfo *mi = load_module(c, dotted, s->lineno, s->col_offset);
    asdl_seq *names = s->v.ImportFrom.names;

    if (asdl_seq_LEN(names) > 0) {
        for (size_t i = 0; i < asdl_seq_LEN(names); i++) {
            import_name_ty item = asdl_seq_GET(names, i);
            const char *name = item->name;
            Sym *exported = module_export_sym(mi, name);
            Ty *ty = exported ? exported->ty : NULL;

            if (!ty && (!strcmp(dotted, "System.stdout") ||
                        !strcmp(dotted, "System.stderr"))) {
                if (!strcmp(name, "print") || !strcmp(name, "println"))
                    ty = ty_func(c, NULL, 0, ty_void(c), 1);
                else
                    check_error(c, s->lineno, s->col_offset,
                                "module '%s' has no member '%s'", dotted, name);
            }
            if (!ty && mi && mi->ok && !mi->opaque && !mi->loading)
                check_error(c, s->lineno, s->col_offset,
                            "module '%s' has no member '%s'", dotted, name);
            define_or_update_const(c, item->alias ? item->alias : name,
                                   ty ? ty : ty_any(c),
                                   exported ? exported->is_const : 0);
        }
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

/* 컬렉션 메서드의 함수 타입을 만든다. nrequired개는 생략할 수 없다. */
static Ty **method_params(Checker *c, size_t n) {
    return n ? Arena_Alloc(c->arena, n * sizeof(Ty *)) : NULL;
}

static Ty *method_func(Checker *c, size_t nrequired, size_t nparams,
                       Ty **params, Ty *ret) {
    Ty *t = ty_func(c, params, nparams, ret, 0);
    t->nrequired = nrequired;
    return t;
}

static Ty *one_param_func(Checker *c, Ty *param, Ty *ret) {
    Ty **p = method_params(c, 1);
    p[0] = param;
    return method_func(c, 1, 1, p, ret);
}

static Ty *list_method_type(Checker *c, Ty *elem, const char *name) {
    Ty **p;
    if (!strcmp(name, "append"))
        return one_param_func(c, elem, ty_void(c));
    if (!strcmp(name, "extend"))
        return one_param_func(c, ty_seq(c, TY_LIST, elem), ty_void(c));
    if (!strcmp(name, "insert")) {
        p = method_params(c, 2);
        p[0] = ty_int(c);
        p[1] = elem;
        return method_func(c, 2, 2, p, ty_void(c));
    }
    if (!strcmp(name, "pop")) {
        p = method_params(c, 1);
        p[0] = ty_int(c);
        return method_func(c, 0, 1, p, elem);
    }
    if (!strcmp(name, "remove"))
        return one_param_func(c, elem, ty_void(c));
    if (!strcmp(name, "clear") || !strcmp(name, "reverse"))
        return method_func(c, 0, 0, NULL, ty_void(c));
    if (!strcmp(name, "index") || !strcmp(name, "count"))
        return one_param_func(c, elem, ty_int(c));
    if (!strcmp(name, "copy"))
        return method_func(c, 0, 0, NULL, ty_seq(c, TY_LIST, elem));
    if (!strcmp(name, "map"))
        return one_param_func(c, one_param_func(c, elem, ty_any(c)),
                              ty_seq(c, TY_LIST, ty_any(c)));
    if (!strcmp(name, "filter"))
        return one_param_func(c, one_param_func(c, elem, ty_bool(c)),
                              ty_seq(c, TY_LIST, elem));
    if (!strcmp(name, "sort")) {
        p = method_params(c, 1);
        p[0] = one_param_func(c, elem, ty_any(c));
        return method_func(c, 0, 1, p, ty_void(c));
    }
    return NULL;
}

static Ty *tuple_method_type(Checker *c, Ty *elem, const char *name) {
    if (!strcmp(name, "index") || !strcmp(name, "count"))
        return one_param_func(c, elem, ty_int(c));
    return NULL;
}

static Ty *dict_method_type(Checker *c, Ty *key, Ty *value, const char *name) {
    Ty **p;
    if (!strcmp(name, "get")) {
        p = method_params(c, 2);
        p[0] = key;
        p[1] = value;
        return method_func(c, 1, 2, p, ty_nullable(c, value));
    }
    if (!strcmp(name, "keys"))
        return method_func(c, 0, 0, NULL, ty_seq(c, TY_LIST, key));
    if (!strcmp(name, "values"))
        return method_func(c, 0, 0, NULL, ty_seq(c, TY_LIST, value));
    if (!strcmp(name, "items"))
        return method_func(c, 0, 0, NULL, ty_seq(c, TY_LIST, ty_any(c)));
    if (!strcmp(name, "pop")) {
        p = method_params(c, 2);
        p[0] = key;
        p[1] = value;
        return method_func(c, 1, 2, p, value);
    }
    if (!strcmp(name, "setdefault")) {
        p = method_params(c, 2);
        p[0] = key;
        p[1] = value;
        return method_func(c, 2, 2, p, value);
    }
    if (!strcmp(name, "update"))
        return one_param_func(c, ty_dict(c, key, value), ty_void(c));
    if (!strcmp(name, "clear"))
        return method_func(c, 0, 0, NULL, ty_void(c));
    if (!strcmp(name, "copy"))
        return method_func(c, 0, 0, NULL, ty_dict(c, key, value));
    return NULL;
}

static Ty *set_method_type(Checker *c, TyKind kind, Ty *elem, const char *name) {
    Ty *self = ty_seq(c, kind, elem);
    if (!strcmp(name, "add") || !strcmp(name, "remove") ||
        !strcmp(name, "discard") || !strcmp(name, "clear") ||
        !strcmp(name, "pop")) {
        if (kind == TY_FROZENSET)
            return NULL;
        if (!strcmp(name, "clear"))
            return method_func(c, 0, 0, NULL, ty_void(c));
        if (!strcmp(name, "pop"))
            return method_func(c, 0, 0, NULL, elem);
        return one_param_func(c, elem, ty_void(c));
    }
    if (!strcmp(name, "copy"))
        return method_func(c, 0, 0, NULL, self);
    if (!strcmp(name, "union") || !strcmp(name, "intersection") ||
        !strcmp(name, "difference"))
        return one_param_func(c, ty_any(c), self);
    if (!strcmp(name, "issubset") || !strcmp(name, "issuperset"))
        return one_param_func(c, ty_any(c), ty_bool(c));
    return NULL;
}

static Ty *member_type(Checker *c, Ty *base, const char *name, int line, int col) {
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
    if (base->kind == TY_LIST || base->kind == TY_TUPLE ||
        base->kind == TY_DICT || base->kind == TY_SET ||
        base->kind == TY_FROZENSET) {
        Ty *t = NULL;
        const char *what = "list";
        switch (base->kind) {
        case TY_LIST:
            what = "list";
            t = list_method_type(c, base->a, name);
            break;
        case TY_TUPLE:
            what = "tuple";
            t = tuple_method_type(c, base->a, name);
            break;
        case TY_DICT:
            what = "dict";
            t = dict_method_type(c, base->a, base->b, name);
            break;
        case TY_SET:
            what = "set";
            t = set_method_type(c, TY_SET, base->a, name);
            break;
        case TY_FROZENSET:
            what = "frozenset";
            t = set_method_type(c, TY_FROZENSET, base->a, name);
            break;
        default:
            break;
        }
        if (t)
            return t;
        check_error(c, line, col, "%s has no attribute '%s'", what, name);
        return ty_any(c);
    }
    if (base->kind == TY_STRUCT) {
        StructInfo *si = find_struct(c, base->name);
        if (si) {
            size_t i;
            for (i = 0; i < si->nfields; i++) {
                if (!strcmp(si->fields[i].name, name))
                    return si->fields[i].ty;
            }
            check_error(c, line, col, "struct '%s' has no member '%s'",
                        base->name, name);
        }
        return ty_any(c);
    }
    return ty_any(c);
}

static Ty *attr_type(Checker *c, expr_ty e, Ty *base) {
    const char *name;
    if (e->kind == Attribute_kind)
        name = e->v.Attribute.attr;
    else if (e->kind == PtrAttribute_kind)
        name = e->v.PtrAttribute.attr;
    else
        name = e->v.OptionalAttribute.attr;

    if (e->kind != OptionalAttribute_kind && base && base->nullable) {
        check_error(c, e->lineno, e->col_offset,
                    "possible null access to member '%s': value is %s; "
                    "check for null or use '?.'",
                    name, ty_name(base));
    }
    if (base && base->kind == TY_PTR)
        base = base->a ? base->a : ty_any(c);
    /* 경계가 있는 타입 매개변수는 경계 타입의 멤버를 쓸 수 있다. */
    while (base && base->kind == TY_ANY && base->bound && base->bound != base)
        base = base->bound;

    if (base && base->kind == TY_CLASS) {
        ClassInfo *ci = find_class(c, base->name);
        if (ci) {
            Member *m;
            ensure_base(c, ci);
            m = class_member(ci, name);
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
                {
                    Ty *mt = m->is_method ? m->fn : m->ty;
                    if (ci->ntparams && base->ntargs)
                        mt = substitute_ty(c, mt, ci, base);
                    return mt;
                }
            }
            if (ci->is_exception && (!strcmp(name, "name") || !strcmp(name, "message")))
                return ty_str(c);
            if (ci->is_enum) {
                EnumInfo *ei;
                size_t i;
                if (!strcmp(name, "name") || !strcmp(name, "variant"))
                    return ty_str(c);
                if (!strcmp(name, "ordinal") || !strcmp(name, "arity") ||
                    !strcmp(name, "value"))
                    return ty_int(c);
                if (!strcmp(name, "payload"))
                    return ty_seq(c, TY_LIST, ty_any(c));
                /* 변형 멤버는 위에서 이미 찾았다. 없는 이름이면 오류다. */
                ei = find_enum(c, base->name);
                if (ei) {
                    for (i = 0; i < ei->nvariants; i++) {
                        if (!strcmp(ei->variants[i], name))
                            return ty_class(c, base->name);
                    }
                    check_error(c, e->lineno, e->col_offset,
                                "enum '%s' has no member '%s'", base->name, name);
                }
                return ty_class(c, base->name);
            }
            /* 순환 import로 상위 클래스를 붙이지 못했으면 상속 멤버를 알 수 없다. */
            if (ci->base_name && !ci->base)
                return ty_any(c);
            check_error(c, e->lineno, e->col_offset,
                        "class '%s' has no member '%s'", base->name, name);
            return ty_any(c);
        }
        return ty_any(c);
    }
    return member_type(c, base, name, e->lineno, e->col_offset);
}

/* 인자 타입에서 제네릭 함수의 타입 인자를 추론한다. */
static void infer_targs_from(Checker *c, Ty *pat, Ty *actual,
                             const char **names, Ty **found, size_t n) {
    size_t i;

    if (!pat || !actual || actual->kind == TY_ANY)
        return;
    if (pat->kind == TY_ANY && pat->name) {
        for (i = 0; i < n; i++) {
            if (!strcmp(names[i], pat->name)) {
                if (!found[i])
                    found[i] = actual;
                else if (found[i]->kind == TY_INT && actual->kind == TY_FLOAT)
                    found[i] = ty_float(c);
                return;
            }
        }
        return;
    }
    switch (pat->kind) {
    case TY_PTR:
        infer_targs_from(c, pat->a, actual->kind == TY_PTR ? actual->a : actual,
                         names, found, n);
        break;
    case TY_LIST:
    case TY_TUPLE:
    case TY_SET:
    case TY_FROZENSET:
        if (actual->kind == pat->kind)
            infer_targs_from(c, pat->a, actual->a, names, found, n);
        break;
    case TY_DICT:
        if (actual->kind == TY_DICT) {
            infer_targs_from(c, pat->a, actual->a, names, found, n);
            infer_targs_from(c, pat->b, actual->b, names, found, n);
        }
        break;
    case TY_FUNC:
        if (actual->kind == TY_FUNC) {
            for (i = 0; i < pat->nparams && i < actual->nparams; i++)
                infer_targs_from(c, pat->params[i], actual->params[i], names, found, n);
            infer_targs_from(c, pat->ret, actual->ret, names, found, n);
        }
        break;
    case TY_CLASS:
    case TY_STRUCT:
        if (actual->kind == pat->kind && !strcmp(pat->name, actual->name)) {
            for (i = 0; i < pat->ntargs && i < actual->ntargs; i++)
                infer_targs_from(c, pat->targs[i], actual->targs[i], names, found, n);
        }
        break;
    default:
        break;
    }
}

/* 추론한 타입 인자로 제네릭 함수의 반환 타입을 바꾼다. */
static Ty *generic_ret(Checker *c, Ty *fn, Ty **found) {
    ClassInfo fake;
    Ty use;
    Ty **targs;
    size_t i;

    memset(&fake, 0, sizeof(fake));
    memset(&use, 0, sizeof(use));
    targs = Arena_Alloc(c->arena, fn->ntparams * sizeof(Ty *));
    for (i = 0; i < fn->ntparams; i++) {
        if (found[i]) {
            targs[i] = found[i];
        }
        else {
            Ty *marker = ty_any(c);
            marker->name = fn->tparams[i];
            targs[i] = marker;
        }
    }
    fake.tparams = fn->tparams;
    fake.ntparams = fn->ntparams;
    use.kind = TY_CLASS;
    use.targs = targs;
    use.ntargs = fn->ntparams;
    return substitute_ty(c, fn->ret, &fake, &use);
}

/* 호출 인자를 시그니처의 매개변수 슬롯에 배치한 결과. */
typedef struct {
    expr_ty *slot;         /* 노멀 매개변수 수만큼, 빈 슬롯은 NULL */
    unsigned char *given;
    size_t nnormal;
    Ty *vararg_elem;       /* 가변 위치 인자의 원소 타입 (없으면 NULL) */
    expr_ty *extras;
    size_t nextras;
    size_t npos;
    int err;               /* 1 위치 인자 과다, 2 모르는 키워드, 3 중복 키워드, 4 필수 인자 누락 */
    const char *err_name;
} ArgLayout;

static int layout_call(Checker *c, Ty *sig, asdl_seq *args, asdl_seq *kws, ArgLayout *out) {
    size_t nargs = args ? asdl_seq_LEN(args) : 0;
    size_t nkw = kws ? asdl_seq_LEN(kws) : 0;
    size_t i, j;

    memset(out, 0, sizeof(*out));
    out->npos = nargs;
    out->nnormal = sig->nparams;
    out->slot = sig->nparams ? Arena_Alloc(c->arena, sig->nparams * sizeof(expr_ty)) : NULL;
    out->given = sig->nparams ? Arena_Alloc(c->arena, sig->nparams) : NULL;
    if (sig->nparams)
        memset(out->given, 0, sig->nparams);
    if (sig->has_vararg || sig->varargs)
        out->vararg_elem = sig->vararg_elem ? sig->vararg_elem : ty_any(c);

    if (nargs > sig->nparams && !out->vararg_elem) {
        out->err = 1;
        return 0;
    }
    for (i = 0; i < nargs && i < sig->nparams; i++) {
        out->slot[i] = asdl_seq_GET(args, i);
        out->given[i] = 1;
    }
    if (nargs > sig->nparams) {
        out->nextras = nargs - sig->nparams;
        out->extras = Arena_Alloc(c->arena, out->nextras * sizeof(expr_ty));
        for (i = sig->nparams; i < nargs; i++)
            out->extras[i - sig->nparams] = asdl_seq_GET(args, i);
    }
    for (j = 0; j < nkw; j++) {
        keyword_ty kw = asdl_seq_GET(kws, j);
        int found = -1;
        for (i = 0; i < sig->nparams; i++) {
            if (sig->param_names && sig->param_names[i] &&
                !strcmp(sig->param_names[i], kw->name)) {
                found = (int)i;
                break;
            }
        }
        if (found >= 0) {
            if (out->given[found]) {
                out->err = 3;
                out->err_name = kw->name;
                return 0;
            }
            out->slot[found] = kw->value;
            out->given[found] = 1;
        }
        else if (!sig->has_varkwarg) {
            out->err = 2;
            out->err_name = kw->name;
            return 0;
        }
    }
    for (i = 0; i < sig->nparams; i++) {
        if (!out->given[i] && i < sig->nrequired) {
            out->err = 4;
            out->err_name = sig->param_names ? sig->param_names[i] : NULL;
            return 0;
        }
    }
    return 1;
}

static void report_layout_error(Checker *c, Ty *sig, ArgLayout *lay, expr_ty at) {
    switch (lay->err) {
    case 1:
        check_error(c, at->lineno, at->col_offset,
                    "function takes %zu argument(s) but %zu were given",
                    sig->nparams, lay->npos);
        break;
    case 2:
        check_error(c, at->lineno, at->col_offset,
                    "unexpected keyword argument '%s'",
                    lay->err_name ? lay->err_name : "");
        break;
    case 3:
        check_error(c, at->lineno, at->col_offset,
                    "got multiple values for argument '%s'",
                    lay->err_name ? lay->err_name : "");
        break;
    case 4:
        check_error(c, at->lineno, at->col_offset,
                    "missing required argument '%s'",
                    lay->err_name ? lay->err_name : "");
        break;
    default:
        check_error(c, at->lineno, at->col_offset,
                    "no function definition accepts %zu argument(s)", lay->npos);
        break;
    }
}

/* 시그니처에 맞춰 인자를 검사한다. report=0이면 진단 없이 성공 여부만 본다. */
static int check_call_args(Checker *c, Ty *sig, asdl_seq *args, asdl_seq *kws,
                           int report, expr_ty at, const char *what, Ty **found) {
    ArgLayout lay;
    size_t i, nkw;

    if (!layout_call(c, sig, args, kws, &lay)) {
        if (report)
            report_layout_error(c, sig, &lay, at);
        return 0;
    }
    for (i = 0; i < lay.nnormal; i++) {
        Ty *arg_ty;
        if (!lay.given[i])
            continue;
        arg_ty = infer(c, lay.slot[i]);
        if (found && sig->tparams)
            infer_targs_from(c, sig->params[i], arg_ty, sig->tparams, found, sig->ntparams);
        if (report)
            require_assignable(c, sig->params[i], arg_ty, lay.slot[i], what);
        else if (!assignable(c, sig->params[i], arg_ty))
            return 0;
    }
    if (lay.vararg_elem) {
        for (i = 0; i < lay.nextras; i++) {
            Ty *arg_ty = infer(c, lay.extras[i]);
            if (found && sig->tparams)
                infer_targs_from(c, lay.vararg_elem, arg_ty, sig->tparams,
                                 found, sig->ntparams);
            if (report)
                require_assignable(c, lay.vararg_elem, arg_ty, lay.extras[i], what);
            else if (!assignable(c, lay.vararg_elem, arg_ty))
                return 0;
        }
    }
    if (sig->has_varkwarg) {
        Ty *want = sig->varkwarg_elem ? sig->varkwarg_elem : ty_any(c);
        nkw = kws ? asdl_seq_LEN(kws) : 0;
        for (i = 0; i < nkw; i++) {
            keyword_ty kw = asdl_seq_GET(kws, i);
            size_t k;
            int is_normal = 0;
            Ty *arg_ty;
            for (k = 0; k < sig->nparams; k++) {
                if (sig->param_names && sig->param_names[k] &&
                    !strcmp(sig->param_names[k], kw->name)) {
                    is_normal = 1;
                    break;
                }
            }
            if (is_normal)
                continue;
            arg_ty = infer(c, kw->value);
            if (found && sig->tparams)
                infer_targs_from(c, want, arg_ty, sig->tparams, found, sig->ntparams);
            if (report)
                require_assignable(c, want, arg_ty, kw->value, what);
            else if (!assignable(c, want, arg_ty))
                return 0;
        }
    }
    return 1;
}

/* 생성자 호출(new)은 위치 인자만 받는다. */
static int ctor_accepts(Checker *c, Ty *sig, asdl_seq *args, size_t nargs) {
    size_t i;
    int variadic = sig->has_vararg || sig->varargs;

    if (nargs > sig->nparams && !variadic)
        return 0;
    if (nargs < sig->nrequired)
        return 0;
    for (i = 0; i < nargs; i++) {
        Ty *at = infer(c, asdl_seq_GET(args, i));
        Ty *want = i < sig->nparams ? sig->params[i] : sig->vararg_elem;
        if (want && !assignable(c, want, at))
            return 0;
    }
    return 1;
}

/* 생성자 오버로드 중 인자와 맞는 시그니처를 고르고 인자를 검사한다. */
static void check_ctor_call(Checker *c, const char *cls, Ty *sig,
                            asdl_seq *args, size_t nargs, int line, int col) {
    size_t i, k;
    Ty *fallback = NULL;

    if (!sig)
        return;
    if (sig->kind == TY_OVERLOAD) {
        for (k = 0; k < sig->noverloads; k++) {
            Ty *s = sig->overloads[k];
            if (!s || s->kind != TY_FUNC)
                continue;
            if (!fallback)
                fallback = s;
            if (ctor_accepts(c, s, args, nargs))
                return;
        }
        if (fallback) {
            for (i = 0; i < nargs; i++) {
                Ty *want = i < fallback->nparams ? fallback->params[i]
                                                 : fallback->vararg_elem;
                if (want)
                    require_assignable(c, want, infer(c, asdl_seq_GET(args, i)),
                                       asdl_seq_GET(args, i), "constructor argument");
            }
        }
        check_error(c, line, col, "no constructor of '%s' accepts %zu argument(s)",
                    cls, nargs);
        return;
    }
    if (nargs > sig->nparams && !sig->has_vararg) {
        check_error(c, line, col, "constructor of '%s' takes %zu argument(s)",
                    cls, sig->nparams);
        return;
    }
    if (nargs < sig->nrequired) {
        check_error(c, line, col, "constructor of '%s' requires at least %zu argument(s)",
                    cls, sig->nrequired);
        return;
    }
    for (i = 0; i < nargs; i++) {
        Ty *want = i < sig->nparams ? sig->params[i] : sig->vararg_elem;
        if (want)
            require_assignable(c, want, infer(c, asdl_seq_GET(args, i)),
                               asdl_seq_GET(args, i), "constructor argument");
    }
}

static Ty *call_result(Checker *c, expr_ty call, Ty *callee) {
    asdl_seq *args = call->v.Call.args;
    asdl_seq *kws = call->v.Call.keywords;
    size_t nargs = args ? asdl_seq_LEN(args) : 0;
    size_t nkw = kws ? asdl_seq_LEN(kws) : 0;
    size_t i;

    for (i = 0; i < nargs; i++)
        infer(c, asdl_seq_GET(args, i));
    for (i = 0; i < nkw; i++)
        infer(c, ((keyword_ty)asdl_seq_GET(kws, i))->value);

    if (!callee || callee->kind == TY_ANY)
        return ty_any(c);
    if (callee->kind == TY_OVERLOAD) {
        Ty *fallback = NULL;
        size_t k;
        for (k = 0; k < callee->noverloads; k++) {
            Ty *sig = callee->overloads[k];
            ArgLayout lay;
            if (!sig || sig->kind != TY_FUNC)
                continue;
            if (layout_call(c, sig, args, kws, &lay) && !fallback)
                fallback = sig;
            if (check_call_args(c, sig, args, kws, 0, call, "argument", NULL))
                return sig->ret;
        }
        if (fallback) {
            check_call_args(c, fallback, args, kws, 1, call, "argument", NULL);
            return fallback->ret;
        }
        check_error(c, call->lineno, call->col_offset,
                    "no function definition accepts %zu argument(s)", nargs);
        return ty_any(c);
    }
    if (callee->kind == TY_FUNC) {
        Ty **found = NULL;
        ArgLayout lay;
        if (!layout_call(c, callee, args, kws, &lay)) {
            report_layout_error(c, callee, &lay, call);
            return callee->ret;
        }
        if (callee->ntparams) {
            found = Arena_Alloc(c->arena, callee->ntparams * sizeof(Ty *));
            memset(found, 0, callee->ntparams * sizeof(Ty *));
        }
        check_call_args(c, callee, args, kws, 1, call, "argument", found);
        if (found) {
            for (i = 0; i < callee->ntparams; i++) {
                if (found[i] && callee->tbounds[i] &&
                    !assignable(c, callee->tbounds[i], found[i]))
                    check_error(c, call->lineno, call->col_offset,
                                "type argument '%s' must satisfy bound '%s' but got '%s'",
                                callee->tparams[i], ty_name(callee->tbounds[i]),
                                ty_name(found[i]));
            }
            return generic_ret(c, callee, found);
        }
        return callee->ret;
    }
    if (callee->kind == TY_CLASS) {
        ClassInfo *ci = find_class(c, callee->name);
        if (ci && ci->is_exception)
            return callee;
        if (ci && ci->is_enum)
            return callee;           /* 페이로드 변형 생성자: 인자는 런타임이 검사한다 */
        if (ci && ci->ctor) {
            Ty *sig = ci->ctor;
            if (ci->ntparams && callee->ntargs)
                sig = substitute_ty(c, sig, ci, callee);
            check_ctor_call(c, callee->name, sig, args, nargs,
                            call->lineno, call->col_offset);
            return callee;
        }
        if (nargs != 0 && ci)
            check_error(c, call->lineno, call->col_offset,
                        "'%s' has no constructor", callee->name);
        return callee;
    }
    return ty_any(c);
}

static const char *binop_hook(operator_ty op) {
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
static Ty *operator_hook_ret(Checker *c, Ty *base, const char *name) {
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

static Ty *binop_type(Checker *c, operator_ty op, Ty *l, Ty *r) {
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

static Ty *infer_lambda(Checker *c, expr_ty e) {
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

static Ty *infer(Checker *c, expr_ty e) {
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
    case Ternary_kind: {
        Ty *body, *orelse, *t;
        int nullable;
        infer(c, e->v.Ternary.test);
        body = infer(c, e->v.Ternary.body);
        orelse = infer(c, e->v.Ternary.orelse);
        nullable = body->kind == TY_NULL || orelse->kind == TY_NULL ||
                   body->nullable || orelse->nullable;
        t = unify(c, ty_non_null(c, body), ty_non_null(c, orelse));
        if (t->kind == TY_ANY && body->kind != TY_ANY && orelse->kind != TY_ANY)
            check_error(c, e->lineno, e->col_offset,
                        "ternary branches have incompatible types: %s and %s",
                        ty_name(body), ty_name(orelse));
        return nullable ? ty_nullable(c, t) : t;
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
        push_scope(c);
        if (e->v.BoolOp.op == And)
            narrow_true(c, e->v.BoolOp.left);
        else
            narrow_false(c, e->v.BoolOp.left);
        infer(c, e->v.BoolOp.right);
        pop_scope(c);
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
        if (t->nullable)
            check_error(c, e->lineno, e->col_offset,
                        "possible null dereference: value is %s; "
                        "check for null before dereferencing",
                        ty_name(t));
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
        if (callee && callee->nullable &&
            e->v.Call.func->kind != OptionalAttribute_kind)
            check_error(c, e->lineno, e->col_offset,
                        "possible null call: value is %s; "
                        "check for null or use '?.'",
                        ty_name(callee));
        return call_result(c, e, callee);
    }
    case New_kind: {
        StructInfo *si = find_struct(c, e->v.New.cls);
        ClassInfo *ci = find_class(c, e->v.New.cls);
        size_t nargs = asdl_seq_LEN(e->v.New.args);
        size_t ntype_args = asdl_seq_LEN(e->v.New.type_args);
        Ty *result = ty_class(c, e->v.New.cls);
        size_t i;
        if (ntype_args) {
            result->targs = Arena_Alloc(c->arena, ntype_args * sizeof(Ty *));
            for (i = 0; i < ntype_args; i++)
                result->targs[i] = typeref_to_ty(c, asdl_seq_GET(e->v.New.type_args, i));
            result->ntargs = ntype_args;
        }
        if (si)
            return ty_struct(c, si->name);
        if (ci && ci->is_interface) {
            check_error(c, e->lineno, e->col_offset,
                        "interface '%s' cannot be instantiated", e->v.New.cls);
            for (i = 0; i < nargs; i++)
                infer(c, asdl_seq_GET(e->v.New.args, i));
            return ty_any(c);
        }
        if (ci && ci->is_enum) {
            check_error(c, e->lineno, e->col_offset,
                        "enum '%s' cannot be instantiated; use a variant", e->v.New.cls);
            for (i = 0; i < nargs; i++)
                infer(c, asdl_seq_GET(e->v.New.args, i));
            return ty_any(c);
        }
        if (ci && ci->ntparams != ntype_args) {
            check_error(c, e->lineno, e->col_offset,
                        "'%s' expects %zu type argument(s) but %zu were given",
                        e->v.New.cls, ci->ntparams, ntype_args);
        }
        else if (ci && ci->tparam_bounds) {
            for (i = 0; i < ntype_args; i++) {
                if (ci->tparam_bounds[i] &&
                    !assignable(c, ci->tparam_bounds[i], result->targs[i]))
                    check_error(c, e->lineno, e->col_offset,
                                "type argument '%s' of '%s' must satisfy bound '%s' but got '%s'",
                                ci->tparams[i], e->v.New.cls,
                                ty_name(ci->tparam_bounds[i]),
                                ty_name(result->targs[i]));
            }
        }
        if (ci && ci->ctor) {
            Ty *ctor_sig = ci->ctor;
            if (ci->ntparams && ci->ntparams == ntype_args)
                ctor_sig = substitute_ty(c, ci->ctor, ci, result);
            check_ctor_call(c, e->v.New.cls, ctor_sig, e->v.New.args, nargs,
                            e->lineno, e->col_offset);
        }
        else if (ci && nargs != 0) {
            check_error(c, e->lineno, e->col_offset,
                        "'%s' has no constructor", e->v.New.cls);
        }
        for (i = 0; i < nargs; i++)
            infer(c, asdl_seq_GET(e->v.New.args, i));
        return result;
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
        {
            Ty *index = infer(c, e->v.Subscript.index);
            if (index && index->kind != TY_ANY) {
                if (container->kind == TY_DICT) {
                    if (container->a && container->a->kind != TY_ANY &&
                        !assignable(c, container->a, index))
                        check_error(c, e->lineno, e->col_offset,
                                    "dict key: expected %s but got %s",
                                    ty_name(container->a), ty_name(index));
                }
                else if ((container->kind == TY_LIST ||
                          container->kind == TY_TUPLE ||
                          container->kind == TY_STR) &&
                         index->kind != TY_INT)
                    check_error(c, e->lineno, e->col_offset,
                                "index must be int but got %s", ty_name(index));
                else if (container->kind == TY_SET ||
                         container->kind == TY_FROZENSET)
                    check_error(c, e->lineno, e->col_offset,
                                "'%s' is not subscriptable",
                                container->kind == TY_SET ? "set" : "frozenset");
            }
        }
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

static void collect_body(Checker *c, asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        collect_stmt(c, asdl_seq_GET(body, i));
}

static void class_add_member(Checker *c, ClassInfo *ci, const char *name, Ty *ty,
                             Ty *fn, int is_method, access_ty access) {
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

static void collect_class(Checker *c, stmt_ty s) {
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
        ci->interface_uses = Arena_Alloc(c->arena, ci->ninterfaces * sizeof(typeref_ty));
        for (i = 0; i < ci->ninterfaces; i++) {
            typeref_ty iface = asdl_seq_GET(s->v.ClassDef.interfaces, i);
            ci->interfaces[i] = iface && iface->kind == Named_kind
                ? iface->v.Named.name : "";
            ci->interface_uses[i] = iface;
        }
    }
    ci->ntparams = asdl_seq_LEN(s->v.ClassDef.type_params);
    push_tparams(c, s->v.ClassDef.type_params, &save);
    ci->tparams = c->tparams;
    ci->tparam_bounds = c->tparam_bounds;
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
        case Method_kind: {
            TParamSave msave;
            Ty *fn;
            push_tparams(c, m->v.Method.type_params, &msave);
            fn = params_to_func(c, m->v.Method.params, m->v.Method.returns, 0);
            attach_tparams(c, fn, m->v.Method.type_params);
            pop_tparams(c, &msave);
            class_add_member(c, ci, m->v.Method.name, NULL, fn,
                             1, m->v.Method.access);
            break;
        }
        case Constructor_kind:
            ci->ctor = ty_overload_append(c, ci->ctor,
                                          params_to_func(c, m->v.Constructor.params, NULL, 0));
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

static void collect_interface(Checker *c, stmt_ty s) {
    ClassInfo *ci = find_class(c, s->v.InterfaceDef.name);
    size_t i, n = asdl_seq_LEN(s->v.InterfaceDef.methods);
    TParamSave save;

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
    ci->ntparams = asdl_seq_LEN(s->v.InterfaceDef.type_params);
    push_tparams(c, s->v.InterfaceDef.type_params, &save);
    ci->tparams = c->tparams;
    ci->tparam_bounds = c->tparam_bounds;
    for (i = 0; i < n; i++) {
        signature_ty sig = asdl_seq_GET(s->v.InterfaceDef.methods, i);
        class_add_member(c, ci, sig->name, NULL,
                         params_to_func(c, sig->params, sig->returns, 0), 1, Public);
    }
    pop_tparams(c, &save);
}

static void collect_struct(Checker *c, stmt_ty s) {
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

static Ty *shallow_type(Checker *c, expr_ty e) {
    if (!e)
        return ty_any(c);
    switch (e->kind) {
    case Constant_kind:
        return constant_type(c, e->v.Constant.value);
    case UnaryOp_kind:
        if (e->v.UnaryOp.op == Not)
            return ty_bool(c);
        return shallow_type(c, e->v.UnaryOp.operand);
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
    case Ternary_kind: {
        Ty *body = shallow_type(c, e->v.Ternary.body);
        Ty *orelse = shallow_type(c, e->v.Ternary.orelse);
        int nullable = body->kind == TY_NULL || orelse->kind == TY_NULL ||
                       body->nullable || orelse->nullable;
        Ty *t = unify(c, ty_non_null(c, body), ty_non_null(c, orelse));
        return nullable ? ty_nullable(c, t) : t;
    }
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

static void collect_stmt(Checker *c, stmt_ty s) {
    switch (s->kind) {
    case FunctionDef_kind: {
        TParamSave save;
        Ty *fn;
        push_tparams(c, s->v.FunctionDef.type_params, &save);
        fn = params_to_func(c, s->v.FunctionDef.params, s->v.FunctionDef.returns, 0);
        attach_tparams(c, fn, s->v.FunctionDef.type_params);
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
    case EnumDef_kind: {
        EnumInfo *ei = find_enum(c, s->v.EnumDef.name);
        size_t i, j, n = asdl_seq_LEN(s->v.EnumDef.members);
        if (!ei) {
            ei = Arena_Alloc(c->arena, sizeof(EnumInfo));
            memset(ei, 0, sizeof(*ei));
            ei->name = s->v.EnumDef.name;
            ei->next = c->enums;
            c->enums = ei;
        }
        if (!ei->variants) {
            ei->nvariants = n;
            if (n) {
                ei->variants = Arena_Alloc(c->arena, n * sizeof(const char *));
                ei->params = Arena_Alloc(c->arena, n * sizeof(Ty **));
                ei->nparams = Arena_Alloc(c->arena, n * sizeof(size_t));
            }
            for (i = 0; i < n; i++) {
                enum_member_ty em = asdl_seq_GET(s->v.EnumDef.members, i);
                size_t np = asdl_seq_LEN(em->params);
                ei->variants[i] = em->name;
                ei->nparams[i] = np;
                ei->params[i] = np ? Arena_Alloc(c->arena, np * sizeof(Ty *)) : NULL;
                for (j = 0; j < np; j++) {
                    param_ty p = asdl_seq_GET(em->params, j);
                    ei->params[i][j] = p->ty ? typeref_to_ty(c, p->ty) : ty_any(c);
                }
            }
        }
        {
            /* enum 값을 클래스처럼 다룬다: 변형은 값 멤버, 메서드는 메서드 멤버. */
            ClassInfo *ci = find_class(c, s->v.EnumDef.name);
            if (!ci) {
                size_t k;
                ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
                memset(ci, 0, sizeof(*ci));
                ci->name = s->v.EnumDef.name;
                ci->is_enum = 1;
                ci->line = s->lineno;
                ci->col = s->col_offset;
                ci->next = c->classes;
                c->classes = ci;
                for (k = 0; k < n; k++) {
                    enum_member_ty em = asdl_seq_GET(s->v.EnumDef.members, k);
                    class_add_member(c, ci, em->name,
                                     ty_class(c, s->v.EnumDef.name),
                                     NULL, 0, Public);
                }
                for (k = 0; k < asdl_seq_LEN(s->v.EnumDef.methods); k++) {
                    member_ty m = asdl_seq_GET(s->v.EnumDef.methods, k);
                    if (m->kind == Method_kind) {
                        TParamSave msave;
                        Ty *fn;
                        push_tparams(c, m->v.Method.type_params, &msave);
                        fn = params_to_func(c, m->v.Method.params,
                                            m->v.Method.returns, 0);
                        attach_tparams(c, fn, m->v.Method.type_params);
                        pop_tparams(c, &msave);
                        class_add_member(c, ci, m->v.Method.name, NULL, fn, 1,
                                         m->v.Method.access);
                    }
                }
            }
        }
        define_or_update(c, s->v.EnumDef.name, ty_class(c, s->v.EnumDef.name));
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
        define_shadow_const(c, s->v.VarDecl.name, ty, s->v.VarDecl.is_const);
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
        if (s->v.Import.wildcard && strcmp(dotted, "System.stdout") &&
            strcmp(dotted, "System.stderr"))
            c->allow_unknown = 1;
        break;
    }
    case ImportFrom_kind: {
        asdl_seq *names = s->v.ImportFrom.names;
        if (asdl_seq_LEN(names) == 0) {
            c->allow_unknown = 1;
            break;
        }
        for (size_t i = 0; i < asdl_seq_LEN(names); i++) {
            import_name_ty item = asdl_seq_GET(names, i);
            define_or_update(c, item->alias ? item->alias : item->name, ty_any(c));
        }
        break;
    }
    case Export_kind:
        collect_stmt(c, s->v.Export.body);
        break;
    default:
        break;
    }
}

static void resolve_bases(Checker *c) {
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        if (ci->base_name)
            ci->base = find_class(c, ci->base_name);
    }
}

/* 인터페이스 시그니처와 구현 메서드(오버로드 묶음)가 맞는지. */
static int func_overload_matches(Checker *c, Ty *impl, Ty *want) {
    size_t i;
    if (!impl || !want)
        return 0;
    if (want->kind == TY_OVERLOAD) {
        if (want->noverloads == 0)
            return 0;
        for (i = 0; i < want->noverloads; i++) {
            if (!func_overload_matches(c, impl, want->overloads[i]))
                return 0;
        }
        return 1;
    }
    if (impl->kind == TY_OVERLOAD) {
        for (i = 0; i < impl->noverloads; i++) {
            if (same_ty(c, impl->overloads[i], want))
                return 1;
        }
        return 0;
    }
    return same_ty(c, impl, want);
}

/* implements한 인터페이스의 메서드가 모두 구현되었는지 확인한다. */
static void check_interfaces(Checker *c) {
    ClassInfo *ci;
    for (ci = c->classes; ci; ci = ci->next) {
        size_t k;
        if (ci->is_interface)
            continue;
        for (k = 0; k < ci->ninterfaces; k++) {
            ClassInfo *iface = find_class(c, ci->interfaces[k]);
            typeref_ty use = ci->interface_uses ? ci->interface_uses[k] : NULL;
            size_t nargs = use && use->kind == Named_kind && use->v.Named.args
                ? asdl_seq_LEN(use->v.Named.args) : 0;
            Member *im;
            if (!iface || !iface->is_interface)
                continue;            /* 외부 인터페이스는 모양을 모르므로 넘어간다 */
            if (iface->ntparams != nargs) {
                check_error(c, ci->line, ci->col,
                            "interface '%s' expects %zu type argument(s) but %zu were given",
                            iface->name, iface->ntparams, nargs);
                continue;
            }
            if (iface->tparam_bounds) {
                size_t ai;
                for (ai = 0; ai < nargs; ai++) {
                    typeref_ty aty = asdl_seq_GET(use->v.Named.args, ai);
                    const char *aname = aty && aty->kind == Named_kind
                        ? aty->v.Named.name : NULL;
                    Ty *arg_ty;
                    size_t tj;
                    int own = 0;
                    if (!iface->tparam_bounds[ai])
                        continue;
                    if (aname) {
                        for (tj = 0; tj < ci->ntparams; tj++) {
                            if (!strcmp(ci->tparams[tj], aname)) {
                                own = 1;
                                break;
                            }
                        }
                    }
                    if (own)
                        continue;
                    arg_ty = typeref_to_ty(c, aty);
                    if (!assignable(c, iface->tparam_bounds[ai], arg_ty))
                        check_error(c, ci->line, ci->col,
                                    "type argument '%s' of interface '%s' must satisfy bound '%s' but got '%s'",
                                    iface->tparams[ai], iface->name,
                                    ty_name(iface->tparam_bounds[ai]),
                                    ty_name(arg_ty));
                }
            }
            for (im = iface->members; im; im = im->next) {
                Member *impl = class_member(ci, im->name);
                Ty *want;
                if (!impl || !impl->is_method) {
                    check_error(c, ci->line, ci->col,
                                "class '%s' does not implement interface '%s' method '%s'",
                                ci->name, iface->name, im->name);
                    continue;
                }
                want = im->fn;
                if (iface->ntparams && nargs == iface->ntparams) {
                    Ty *use_ty = ty_class(c, iface->name);
                    size_t ai;
                    use_ty->targs = Arena_Alloc(c->arena, nargs * sizeof(Ty *));
                    for (ai = 0; ai < nargs; ai++)
                        use_ty->targs[ai] = typeref_to_ty(c, asdl_seq_GET(use->v.Named.args, ai));
                    use_ty->ntargs = nargs;
                    want = substitute_ty(c, want, iface, use_ty);
                }
                {
                    /* 오버로드 묶음: 인터페이스의 모든 시그니처가 구현 쪽 하나와 맞아야 한다. */
                    Ty *impl_fn = impl->fn;
                    if (!func_overload_matches(c, impl_fn, want))
                        check_error(c, ci->line, ci->col,
                                    "'%s' method '%s' does not match interface '%s'",
                                    ci->name, im->name, iface->name);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ 문장 검사 */

static void check_body(Checker *c, asdl_seq *body);
static void check_stmt(Checker *c, stmt_ty s);

static int stmt_has_yield(stmt_ty s);
static int body_has_yield(asdl_seq *body) {
    size_t i;
    if (!body)
        return 0;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (stmt_has_yield(asdl_seq_GET(body, i)))
            return 1;
    }
    return 0;
}

/* 중첩 함수·람다는 별도의 제너레이터이므로 여기서 내려가지 않는다. */
static int stmt_has_yield(stmt_ty s) {
    size_t i;
    if (!s)
        return 0;
    switch (s->kind) {
    case Yield_kind:
        return 1;
    case If_kind:
        return body_has_yield(s->v.If.body) || body_has_yield(s->v.If.orelse);
    case While_kind:
        return body_has_yield(s->v.While.body);
    case For_kind:
        return body_has_yield(s->v.For.body);
    case ForRange_kind:
        return body_has_yield(s->v.ForRange.body);
    case ForIn_kind:
        return body_has_yield(s->v.ForIn.body);
    case Switch_kind:
        for (i = 0; i < asdl_seq_LEN(s->v.Switch.cases); i++) {
            match_case_ty mc = asdl_seq_GET(s->v.Switch.cases, i);
            if (body_has_yield(mc->body))
                return 1;
        }
        return 0;
    case Try_kind:
        if (body_has_yield(s->v.Try.body) ||
            body_has_yield(s->v.Try.finalbody))
            return 1;
        for (i = 0; i < asdl_seq_LEN(s->v.Try.handlers); i++) {
            handler_ty h = asdl_seq_GET(s->v.Try.handlers, i);
            if (body_has_yield(h->body))
                return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static int always_exits(Checker *c, asdl_seq *body);
static const char *pattern_variant_name(expr_ty path, const char **enum_name);

static void check_function(Checker *c, asdl_seq *type_params, asdl_seq *params,
                           ret_ty ret, asdl_seq *body, expr_ty body_expr,
                           ClassInfo *cls, int lambda, int line, int col) {
    ClassInfo *saved_class = c->current_class;
    Ty *saved_ret = c->current_ret;
    int saved_known = c->current_ret_known;
    int saved_in_fn = c->in_function;
    int saved_gen = c->in_generator;
    int saved_loop = c->loop_depth;
    TParamSave save;
    size_t i, n = params ? asdl_seq_LEN(params) : 0;

    c->current_class = cls;
    c->in_function = 1;
    c->in_generator = !lambda && body_has_yield(body);
    c->loop_depth = 0;
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
            Ty *pt = p->ty ? typeref_to_ty(c, p->ty) : ty_any(c);
            if (p->variadic == 1)
                define(c, p->name, ty_seq(c, TY_LIST, pt));
            else if (p->variadic == 2)
                define(c, p->name, ty_dict(c, ty_str(c), pt));
            else
                define(c, p->name, pt);
        }
    }
    if (!lambda) {
        for (i = 0; i < n; i++) {
            param_ty p = asdl_seq_GET(params, i);
            if (!p->default_value)
                continue;
            require_assignable(c, p->ty ? typeref_to_ty(c, p->ty) : ty_any(c),
                               infer(c, p->default_value), p->default_value, "default argument");
        }
    }
    if (!lambda) {
        collect_body(c, body);
        check_body(c, body);
        if (c->current_ret_known && c->current_ret &&
            c->current_ret->kind != TY_VOID && !c->in_generator &&
            !always_exits(c, body))
            check_error(c, line, col,
                        "missing return: function must return a value of type %s on all paths",
                        ty_name(c->current_ret));
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
    c->in_generator = saved_gen;
    c->loop_depth = saved_loop;
}

static void check_class(Checker *c, stmt_ty s) {
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
                           m->v.Method.returns, m->v.Method.body, NULL, ci, 0,
                           m->lineno, m->col_offset);
            break;
        case Constructor_kind:
            check_function(c, NULL, m->v.Constructor.params, NULL,
                           m->v.Constructor.body, NULL, ci, 0, 0, 0);
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
            check_function(c, NULL, NULL, ret, m->v.Getter.body, NULL, ci, 0,
                           m->lineno, m->col_offset);
            break;
        }
        case Setter_kind: {
            asdl_seq *params = _Aster_seq_new(1, c->arena);
            asdl_seq_SET(params, 0, m->v.Setter.param);
            check_function(c, NULL, params, NULL, m->v.Setter.body, NULL, ci, 0,
                           m->lineno, m->col_offset);
            break;
        }
        default:
            break;
        }
    }
    c->current_class = saved;
}

static void check_struct(Checker *c, stmt_ty s) {
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

static void check_assign_target(Checker *c, expr_ty target, Ty *value) {
    switch (target->kind) {
    case Name_kind: {
        Sym *cur = lookup_sym(c, target->v.Name.id, 0);
        Sym *decl;
        if (!cur) {
            if (is_container(value) || value->kind == TY_FUNC)
                define(c, target->v.Name.id, value);
            return;
        }
        decl = cur->narrowed ? lookup_sym(c, target->v.Name.id, 1) : cur;
        if (decl && decl->is_const) {
            check_error(c, target->lineno, target->col_offset,
                        "cannot assign to const variable '%s'",
                        target->v.Name.id);
            return;
        }
        if (cur->narrowed) {
            /* 좁힌 그림자에 대입하면 선언 타입 기준으로 검사하고 그림자를 푼다. */
            Sym *decl = lookup_sym(c, target->v.Name.id, 1);
            Ty *base = decl ? decl->ty : ty_any(c);
            require_assignable(c, base, value, target, "assignment");
            define(c, target->v.Name.id, base);
            break;
        }
        if (cur->ty->kind == TY_ANY && value->kind != TY_ANY) {
            define_or_update(c, target->v.Name.id, value);
            return;
        }
        require_assignable(c, cur->ty, value, target, "assignment");
        break;
    }
    case Attribute_kind: {
        Ty *base = infer(c, target->v.Attribute.value);
        if (base && base->kind == TY_MODULE) {
            ModuleInfo *mi = load_module(c, base->name, target->lineno,
                                         target->col_offset);
            Sym *exported = module_export_sym(mi, target->v.Attribute.attr);
            if (exported && exported->is_const) {
                check_error(c, target->lineno, target->col_offset,
                            "cannot assign to const variable '%s'",
                            target->v.Attribute.attr);
                return;
            }
        }
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

static void check_body(Checker *c, asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        check_stmt(c, asdl_seq_GET(body, i));
}

/* 흐름 좁히기: 조건이 참/거짓일 때 이름의 null 여부를 정적으로 좁힌다. */

static int is_null_literal(expr_ty e) {
    return e && e->kind == Constant_kind && e->v.Constant.value.tag == VT_NULL;
}

static void narrow_name(Checker *c, expr_ty e) {
    Ty *t;
    if (!e || e->kind != Name_kind)
        return;
    t = lookup(c, e->v.Name.id);
    if (!t || !t->nullable)
        return;
    define_narrowed(c, e->v.Name.id, ty_non_null(c, t));
}

static void narrow_true(Checker *c, expr_ty e);

static void narrow_false(Checker *c, expr_ty e) {
    if (!e)
        return;
    switch (e->kind) {
    case Compare_kind:
        if (e->v.Compare.op == Eq) {
            if (is_null_literal(e->v.Compare.right))
                narrow_name(c, e->v.Compare.left);
            else if (is_null_literal(e->v.Compare.left))
                narrow_name(c, e->v.Compare.right);
        }
        break;
    case UnaryOp_kind:
        if (e->v.UnaryOp.op == Not)
            narrow_true(c, e->v.UnaryOp.operand);
        break;
    case BoolOp_kind:
        if (e->v.BoolOp.op == Or) {
            narrow_false(c, e->v.BoolOp.left);
            narrow_false(c, e->v.BoolOp.right);
        }
        break;
    default:
        break;
    }
}

static void narrow_true(Checker *c, expr_ty e) {
    if (!e)
        return;
    switch (e->kind) {
    case Compare_kind:
        if (e->v.Compare.op == NotEq) {
            if (is_null_literal(e->v.Compare.right))
                narrow_name(c, e->v.Compare.left);
            else if (is_null_literal(e->v.Compare.left))
                narrow_name(c, e->v.Compare.right);
        }
        break;
    case UnaryOp_kind:
        if (e->v.UnaryOp.op == Not)
            narrow_false(c, e->v.UnaryOp.operand);
        break;
    case BoolOp_kind:
        if (e->v.BoolOp.op == And) {
            narrow_true(c, e->v.BoolOp.left);
            narrow_true(c, e->v.BoolOp.right);
        }
        break;
    case Name_kind:
        narrow_name(c, e);
        break;
    default:
        break;
    }
}

static int always_exits(Checker *c, asdl_seq *body) {
    stmt_ty last;
    size_t n = asdl_seq_LEN(body);
    if (!n)
        return 0;
    last = asdl_seq_GET(body, n - 1);
    if (!last)
        return 0;
    switch (last->kind) {
    case Return_kind:
    case Throw_kind:
    case Break_kind:
    case Continue_kind:
        return 1;
    case If_kind:
        return asdl_seq_LEN(last->v.If.orelse) > 0 &&
               always_exits(c, last->v.If.body) &&
               always_exits(c, last->v.If.orelse);
    case While_kind: {
        expr_ty test = last->v.While.test;
        return test && test->kind == Constant_kind &&
               test->v.Constant.value.tag == VT_BOOL &&
               test->v.Constant.value.u.i != 0;
    }
    case Try_kind: {
        int ok = always_exits(c, last->v.Try.body);
        size_t i;
        for (i = 0; i < asdl_seq_LEN(last->v.Try.handlers); i++) {
            handler_ty h = asdl_seq_GET(last->v.Try.handlers, i);
            ok = ok && always_exits(c, h->body);
        }
        if (asdl_seq_LEN(last->v.Try.finalbody) &&
            always_exits(c, last->v.Try.finalbody))
            ok = 1;
        return ok;
    }
    case Switch_kind: {
        asdl_seq *cases = last->v.Switch.cases;
        Ty *subject = infer(c, last->v.Switch.subject);
        EnumInfo *ei = subject && subject->kind == TY_CLASS
            ? find_enum(c, subject->name) : NULL;
        int *covered = NULL;
        int all = 1, catch_all = 0;
        size_t i, k;

        if (ei && ei->nvariants) {
            covered = Arena_Alloc(c->arena, ei->nvariants * sizeof(int));
            memset(covered, 0, ei->nvariants * sizeof(int));
        }
        for (i = 0; i < asdl_seq_LEN(cases); i++) {
            match_case_ty mc = asdl_seq_GET(cases, i);
            if (!always_exits(c, mc->body))
                all = 0;
            if (!mc->guard && mc->pattern &&
                mc->pattern->kind == PBind_kind)
                catch_all = 1;
            if (ei && !mc->guard && mc->pattern &&
                mc->pattern->kind == PVariant_kind) {
                const char *ename = NULL;
                const char *vname = pattern_variant_name(
                    mc->pattern->v.PVariant.path, &ename);
                if (vname && ename && !strcmp(ename, ei->name)) {
                    for (k = 0; k < ei->nvariants; k++) {
                        if (!strcmp(ei->variants[k], vname))
                            covered[k] = 1;
                    }
                }
            }
        }
        if (!all)
            return 0;
        if (catch_all)
            return 1;
        if (ei && covered) {
            for (k = 0; k < ei->nvariants; k++) {
                if (!covered[k])
                    return 0;
            }
            return 1;
        }
        return 0;
    }
    default:
        return 0;
    }
}

/* case 패턴의 enum 이름과 변형 이름을 뽑는다 (Shape.Circle -> "Shape", "Circle"). */
static const char *pattern_variant_name(expr_ty path, const char **enum_name) {
    *enum_name = NULL;
    if (!path || path->kind != Attribute_kind)
        return NULL;
    if (path->v.Attribute.value->kind == Name_kind)
        *enum_name = path->v.Attribute.value->v.Name.id;
    else if (path->v.Attribute.value->kind == Attribute_kind)
        *enum_name = path->v.Attribute.value->v.Attribute.attr;
    return path->v.Attribute.attr;
}

static void check_pattern(Checker *c, match_pattern_ty pat, Ty *expected,
                          EnumInfo *subject_enum, int *covered, int *catch_all,
                          int line, int col) {
    switch (pat->kind) {
    case PLiteral_kind: {
        expr_ty lit = pat->v.PLiteral.value;
        Ty *t = infer(c, lit);
        if (expected && expected->kind != TY_ANY && !assignable(c, expected, t))
            check_error(c, lit->lineno, lit->col_offset,
                        "case value: expected %s but got %s",
                        ty_name(expected), ty_name(t));
        break;
    }
    case PBind_kind:
        *catch_all = 1;
        if (strcmp(pat->v.PBind.name, "_"))
            define(c, pat->v.PBind.name, expected ? ty_non_null(c, expected) : ty_any(c));
        break;
    case PList_kind: {
        asdl_seq *items = pat->v.PList.items;
        size_t nitems = asdl_seq_LEN(items), i;
        Ty *elem = ty_any(c);
        if (expected && (expected->kind == TY_LIST || expected->kind == TY_TUPLE))
            elem = expected->a;
        else if (expected && expected->kind != TY_ANY && expected->kind != TY_UNION)
            check_error(c, line, col,
                        "case pattern expects a list but the subject is %s",
                        ty_name(expected));
        for (i = 0; i < nitems; i++) {
            int inner = 0;
            check_pattern(c, asdl_seq_GET(items, i), elem, NULL, NULL, &inner, line, col);
        }
        if (pat->v.PList.rest && strcmp(pat->v.PList.rest, "_"))
            define(c, pat->v.PList.rest, ty_seq(c, TY_LIST, elem));
        break;
    }
    case PDict_kind: {
        asdl_seq *items = pat->v.PDict.items;
        size_t nitems = asdl_seq_LEN(items), i;
        Ty *kt = ty_any(c), *vt = ty_any(c);
        if (expected && expected->kind == TY_DICT) {
            kt = expected->a;
            vt = expected->b;
        }
        else if (expected && expected->kind != TY_ANY && expected->kind != TY_UNION) {
            check_error(c, line, col,
                        "case pattern expects a dict but the subject is %s",
                        ty_name(expected));
        }
        for (i = 0; i < nitems; i++) {
            pdict_item_ty d = asdl_seq_GET(items, i);
            int inner = 0;
            Ty *key_ty = infer(c, d->key);
            if (kt && kt->kind != TY_ANY && !assignable(c, kt, key_ty))
                check_error(c, d->key->lineno, d->key->col_offset,
                            "case key: expected %s but got %s",
                            ty_name(kt), ty_name(key_ty));
            check_pattern(c, d->value, vt, NULL, NULL, &inner, line, col);
        }
        if (pat->v.PDict.rest && strcmp(pat->v.PDict.rest, "_"))
            define(c, pat->v.PDict.rest, ty_dict(c, kt, vt));
        break;
    }
    case PVariant_kind: {
        const char *enum_name = NULL;
        const char *variant = pattern_variant_name(pat->v.PVariant.path, &enum_name);
        EnumInfo *ei = enum_name ? find_enum(c, enum_name) : NULL;
        asdl_seq *items = pat->v.PVariant.items;
        size_t nitems = asdl_seq_LEN(items), vi, i;

        if (!variant) {
            infer(c, pat->v.PVariant.path);
            for (i = 0; i < nitems; i++) {
                int inner = 0;
                check_pattern(c, asdl_seq_GET(items, i), ty_any(c), NULL, NULL,
                              &inner, line, col);
            }
            break;
        }
        if (!ei) {
            /* enum이 아닌 경로: 일반 식으로 검사만 하고 바인딩은 any로 둔다. */
            infer(c, pat->v.PVariant.path);
            for (i = 0; i < nitems; i++) {
                int inner = 0;
                check_pattern(c, asdl_seq_GET(items, i), ty_any(c), NULL, NULL,
                              &inner, line, col);
            }
            break;
        }
        for (vi = 0; vi < ei->nvariants; vi++)
            if (!strcmp(ei->variants[vi], variant))
                break;
        if (vi == ei->nvariants) {
            check_error(c, pat->v.PVariant.path->lineno,
                        pat->v.PVariant.path->col_offset,
                        "enum '%s' has no variant '%s'", enum_name, variant);
            break;
        }
        if (covered && ei == subject_enum)
            covered[vi] = 1;
        if (nitems != ei->nparams[vi]) {
            check_error(c, pat->v.PVariant.path->lineno,
                        pat->v.PVariant.path->col_offset,
                        "pattern '%s.%s' takes %zu value(s) but %zu given",
                        enum_name, variant, ei->nparams[vi], nitems);
            break;
        }
        for (i = 0; i < nitems; i++) {
            Ty *pt = ei->params[vi][i];
            EnumInfo *inner = pt->kind == TY_CLASS ? find_enum(c, pt->name) : NULL;
            int sub_catch = 0;
            check_pattern(c, asdl_seq_GET(items, i), pt, inner, NULL, &sub_catch,
                          line, col);
        }
        break;
    }
    }
}

static void check_switch(Checker *c, stmt_ty s) {
    Ty *subject = infer(c, s->v.Switch.subject);
    EnumInfo *ei = subject && subject->kind == TY_CLASS
        ? find_enum(c, subject->name) : NULL;
    asdl_seq *cases = s->v.Switch.cases;
    size_t ncases = asdl_seq_LEN(cases), i, k;
    int *covered = NULL;
    int catch_all = 0;

    if (ei && ei->nvariants) {
        covered = Arena_Alloc(c->arena, ei->nvariants * sizeof(int));
        memset(covered, 0, ei->nvariants * sizeof(int));
    }
    for (i = 0; i < ncases; i++) {
        match_case_ty mc = asdl_seq_GET(cases, i);
        int this_catch = 0;
        push_scope(c);
        check_pattern(c, mc->pattern, subject, ei, mc->guard ? NULL : covered,
                      &this_catch, s->lineno, s->col_offset);
        if (mc->guard) {
            Ty *gt = infer(c, mc->guard);
            require_assignable(c, ty_bool(c), gt, mc->guard, "case guard");
        }
        else if (this_catch) {
            catch_all = 1;
        }
        check_body(c, mc->body);
        pop_scope(c);
    }
    if (ei && covered && !catch_all) {
        for (k = 0; k < ei->nvariants; k++) {
            if (!covered[k])
                check_error(c, s->lineno, s->col_offset,
                            "switch on enum '%s' is missing case '%s.%s'",
                            ei->name, ei->name, ei->variants[k]);
        }
    }
}

static void check_stmt(Checker *c, stmt_ty s) {
    switch (s->kind) {
    case FunctionDef_kind:
        if (!s->v.FunctionDef.is_extern)
            check_function(c, s->v.FunctionDef.type_params, s->v.FunctionDef.params,
                           s->v.FunctionDef.returns, s->v.FunctionDef.body, NULL,
                           c->current_class, 0, s->lineno, s->col_offset);
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
        if (s->v.VarDecl.is_const &&
            (!s->v.VarDecl.init || s->v.VarDecl.init->kind == NoInit_kind))
            check_error(c, s->lineno, s->col_offset,
                        "const declaration '%s' requires an initializer",
                        s->v.VarDecl.name);
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
                    if (declared->kind == TY_FUNC) {
                        check_error(c, value->lineno, value->col_offset,
                                    "a lambda cannot be declared with a function type; "
                                    "use 'var' or the lambda's return type");
                        declared = actual;
                    }
                    else if (declared->kind == TY_ANY) {
                        declared = actual;
                    }
                    else if (assignable(c, declared, actual)) {
                        /* 함수형 인터페이스: 변수 타입은 인터페이스로 유지한다. */
                    }
                    else if (actual->kind == TY_FUNC && actual->ret &&
                             assignable(c, declared, actual->ret)) {
                        /* 반환 타입으로 선언한 기존 형태(var 대체). */
                        declared = actual;
                    }
                    else {
                        check_error(c, value->lineno, value->col_offset,
                                    "variable initializer: expected %s but got %s",
                                    ty_name(declared), ty_name(actual));
                        declared = actual;
                    }
                }
                else if (declared->kind == TY_ANY)
                    declared = actual;
                else
                    require_assignable(c, declared, actual, value,
                                       "variable initializer");
            }
        }
        define_shadow_const(c, s->v.VarDecl.name, declared,
                           s->v.VarDecl.is_const);
        break;
    }
    case EnumDef_kind: {
        size_t i;
        ClassInfo *ci;
        for (i = 0; i < asdl_seq_LEN(s->v.EnumDef.members); i++) {
            enum_member_ty em = asdl_seq_GET(s->v.EnumDef.members, i);
            if (em->value)
                infer(c, em->value);
        }
        ci = find_class(c, s->v.EnumDef.name);
        for (i = 0; i < asdl_seq_LEN(s->v.EnumDef.methods); i++) {
            member_ty m = asdl_seq_GET(s->v.EnumDef.methods, i);
            if (m->kind == Method_kind)
                check_function(c, m->v.Method.type_params, m->v.Method.params,
                               m->v.Method.returns, m->v.Method.body, NULL,
                               ci, 0, m->lineno, m->col_offset);
        }
        define_or_update(c, s->v.EnumDef.name, ty_class(c, s->v.EnumDef.name));
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
    case If_kind: {
        expr_ty test = s->v.If.test;
        int then_exits, else_exits;
        infer(c, test);
        push_scope(c);
        narrow_true(c, test);
        check_body(c, s->v.If.body);
        pop_scope(c);
        push_scope(c);
        narrow_false(c, test);
        check_body(c, s->v.If.orelse);
        pop_scope(c);
        then_exits = always_exits(c, s->v.If.body);
        else_exits = asdl_seq_LEN(s->v.If.orelse)
            ? always_exits(c, s->v.If.orelse) : 0;
        if (then_exits && !else_exits)
            narrow_false(c, test);
        else if (else_exits && !then_exits)
            narrow_true(c, test);
        break;
    }
    case While_kind:
        infer(c, s->v.While.test);
        push_scope(c);
        narrow_true(c, s->v.While.test);
        c->loop_depth++;
        check_body(c, s->v.While.body);
        c->loop_depth--;
        pop_scope(c);
        break;
    case For_kind:
        push_scope(c);
        check_stmt(c, s->v.For.init);
        infer(c, s->v.For.test);
        push_scope(c);
        c->loop_depth++;
        check_body(c, s->v.For.body);
        c->loop_depth--;
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
        c->loop_depth++;
        check_body(c, s->v.ForRange.body);
        c->loop_depth--;
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
        c->loop_depth++;
        check_body(c, s->v.ForIn.body);
        c->loop_depth--;
        pop_scope(c);
        pop_scope(c);
        break;
    }
    case Switch_kind:
        check_switch(c, s);
        break;
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
        if (s->v.Yield.value) {
            Ty *value = infer(c, s->v.Yield.value);
            if (c->current_ret_known && c->current_ret->kind == TY_LIST)
                require_assignable(c, c->current_ret->a, value, s->v.Yield.value,
                                   "yield value");
        }
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
        else if (c->current_ret_known && !c->in_generator) {
            check_error(c, s->lineno, s->col_offset,
                        "must return a value of type %s", ty_name(c->current_ret));
        }
        break;
    case Throw_kind:
        infer(c, s->v.Throw.value);
        break;
    case Break_kind:
    case Continue_kind:
        if (c->loop_depth == 0)
            check_error(c, s->lineno, s->col_offset,
                        "'%s' is only allowed inside a loop",
                        s->kind == Break_kind ? "break" : "continue");
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

static void define_builtin(Checker *c, const char *name, Ty *ty) {
    define(c, name, ty);
}

static void register_exception_class(Checker *c, const char *name, const char *base) {
    ClassInfo *ci = Arena_Alloc(c->arena, sizeof(ClassInfo));
    memset(ci, 0, sizeof(*ci));
    ci->name = name;
    ci->base_name = base;
    ci->is_exception = 1;
    ci->next = c->classes;
    c->classes = ci;
}

static void init_builtins(Checker *c) {
    static const char *exceptions[] = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "ValueError", "TypeError", "NameError", "AttributeError", "KeyError",
        "IndexError", "RuntimeError", "NullPointerError", "LookupError",
        "AssertionError", "OSError", "EOFError", "IOException",
        "ImportError", "ModuleNotFoundError", "RecursionError", "NumberFormatError",
        "NotImplementedError", "StopIteration", "NoSuchElementError",
        "RegexError"
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
    define_builtin(c, "abs", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "sum", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "min", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "max", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "all", ty_func(c, NULL, 0, ty_bool(c), 1));
    define_builtin(c, "any", ty_func(c, NULL, 0, ty_bool(c), 1));
    define_builtin(c, "zip", ty_func(c, NULL, 0, ty_seq(c, TY_LIST, ty_any(c)), 1));
    define_builtin(c, "enumerate",
                   ty_func(c, NULL, 0, ty_seq(c, TY_LIST, ty_any(c)), 1));
    define_builtin(c, "sorted", ty_func(c, NULL, 0, ty_seq(c, TY_LIST, ty_any(c)), 1));
    define_builtin(c, "round", ty_func(c, NULL, 0, ty_any(c), 1));
    define_builtin(c, "input", ty_func(c, NULL, 0, ty_str(c), 1));
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
                       const TypeCheckEnv *env) {
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
    c.root = &c;

    /* 1단계: 타입 이름을 먼저 등록하고 import 모듈을 읽는다. 순환 import도
       서로의 선언을 볼 수 있도록 모듈 그래프 전체를 먼저 모은다. */
    preregister_shapes(&c, body);
    preload_imports(&c, body);
    collect_body(&c, body);

    /* 2단계: import한 모듈의 상속·인터페이스·본문을 검사한다. */
    flush_pending(&c);

    /* 3단계: 최상위 모듈의 상속·본문 검사 */
    resolve_bases(&c);
    check_interfaces(&c);
    check_body(&c, body);

    /* 본문 검사 중 지연 로드된 모듈이 있으면 마저 검사한다. */
    flush_pending(&c);

    return c.errors > 0 ? 1 : 0;
}

int TypeCheck_Module(Arena *arena, mod_ty mod, const char *filename) {
    return TypeCheck_ModuleEx(arena, mod, filename, NULL);
}
