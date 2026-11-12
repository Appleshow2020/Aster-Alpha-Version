/* Grammar/aster.gram을 옮긴 재귀 하강 파서.
 *
 * 규칙 하나가 함수 하나이고, 실패하면 위치를 규칙 시작으로 되돌린다 (PEG의 되돌아가기).
 * 합치기(팩트)는 하지 않는다. Aster 문법은 결정적으로 읽히도록 설계되어 있다.
 * 확실히 키워드를 읽은 뒤의 실패는 되돌아가지 않고 syntax_error()로 바로 끝낸다.
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/parser.h"
#include "internal/object.h"

typedef struct {
    void **items;
    size_t count, cap;
} Seq;

typedef struct {
    identifier *items;
    size_t count, cap;
} NameSet;

typedef struct {
    size_t tag_index;
    identifier alias;
} TypedefAlias;

/* 같은 철자의 식별자를 한 벌만 두어 이름 비교를 포인터 비교로 빠르게 한다. */
typedef struct InternEntry {
    const char *s;
    size_t len;
    struct InternEntry *next;
} InternEntry;

#define INTERN_BUCKETS 256

typedef struct Parser {
    AsterTokenList list;
    size_t n;
    size_t pos;
    Arena *arena;
    AsterError *err;
    int no_arrow;
    size_t farthest;
    char expected[32][64];
    int nexp;
    NameSet type_names;
    NameSet struct_type_names;
    TypedefAlias aliases[64];
    size_t naliases;
    NameSet *type_param_stack;
    size_t n_type_params, cap_type_params;
    NameSet *struct_stack;
    size_t n_structs, cap_structs;
    InternEntry *intern[INTERN_BUCKETS];
    jmp_buf jmp;
} Parser;

static const char *const HARD_KEYWORDS[] = {
    "package", "import", "from", "as", "fn", "class", "extends", "implements",
    "interface", "new", "self", "public", "protected", "private", "internal",
    "if", "else", "while", "for", "break", "continue", "return", "throw",
    "switch", "case", "default",
    "try", "catch", "finally", "typedef", "struct", "and", "or", "not", "is",
    "in", "true", "false", "sizeof", "int", "float", "bool", "str", "null", "void",
    "super", "exception", "export", "yield",
};

/* ------------------------------------------------------------- 기본 도우미 */

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("out of memory\n", stderr);
        abort();
    }
    return q;
}

static void seq_push(Seq *s, const void *item) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = xrealloc(s->items, s->cap * sizeof *s->items);
    }
    s->items[s->count++] = (void *)item;
}

static void seq_clear(Seq *s) {
    free(s->items);
    s->items = NULL;
    s->count = s->cap = 0;
}

static asdl_seq *seq_make(Seq *s, Arena *arena) {
    asdl_seq *r = _Aster_seq_new(s->count, arena);
    size_t i;
    for (i = 0; i < s->count; i++)
        asdl_seq_SET(r, i, s->items[i]);
    seq_clear(s);
    return r;
}

static asdl_seq *empty_seq(Parser *p) {
    return _Aster_seq_new(0, p->arena);
}

static void ns_add(NameSet *s, identifier id) {
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = xrealloc(s->items, s->cap * sizeof *s->items);
    }
    s->items[s->count++] = id;
}

static int ns_has_token(const NameSet *s, const AsterToken *t) {
    size_t i;
    for (i = 0; i < s->count; i++) {
        size_t len = strlen(s->items[i]);
        if ((int)len == t->length && memcmp(s->items[i], t->start, len) == 0)
            return 1;
    }
    return 0;
}

static int ns_has_name(const NameSet *s, identifier name) {
    size_t i;
    for (i = 0; i < s->count; i++) {
        if (s->items[i] == name || !strcmp(s->items[i], name))
            return 1;
    }
    return 0;
}

/* 중괄호 초기화({1, 2})는 구조체 타입에서만 쓴다. 그 밖에는 세트/딕셔너리
 * 리터럴 식으로 읽는다. 그렇지 않으면 'var s = {1, 2};'가 구조체 초기화가 된다. */
static int type_is_struct(Parser *p, typeref_ty ty) {
    if (!ty)
        return 0;
    if (ty->kind == StructRef_kind)
        return 1;
    if (ty->kind == Named_kind)
        return ns_has_name(&p->struct_type_names, ty->v.Named.name);
    return 0;
}

static AsterToken *cur(Parser *p) {
    return &p->list.items[p->pos];
}

static AsterToken *tok_at(Parser *p, size_t i) {
    return &p->list.items[i];
}

static int tok_eq(const AsterToken *t, const char *text) {
    size_t len = strlen(text);
    return t->type != TOK_STRING && (size_t)t->length == len &&
           memcmp(t->start, text, len) == 0;
}

static int is_hard_token(const AsterToken *t) {
    size_t i;
    if (t->type != TOK_NAME)
        return 0;
    for (i = 0; i < sizeof HARD_KEYWORDS / sizeof *HARD_KEYWORDS; i++)
        if (tok_eq(t, HARD_KEYWORDS[i]))
            return 1;
    return 0;
}

static identifier intern_token(Parser *p, const AsterToken *t) {
    size_t len = (size_t)t->length;
    const char *s = t->start;
    uint32_t h = 2166136261u;
    InternEntry *e;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 16777619u;
    }
    for (e = p->intern[h % INTERN_BUCKETS]; e; e = e->next) {
        if (e->len == len && memcmp(e->s, s, len) == 0)
            return (identifier)e->s;
    }
    e = Arena_Alloc(p->arena, sizeof(InternEntry));
    e->s = Arena_Strndup(p->arena, s, len);
    e->len = len;
    e->next = p->intern[h % INTERN_BUCKETS];
    p->intern[h % INTERN_BUCKETS] = e;
    return (identifier)e->s;
}

static void expect_text(Parser *p, const char *what) {
    int i;
    if (p->pos > p->farthest) {
        p->farthest = p->pos;
        p->nexp = 0;
    }
    else if (p->pos < p->farthest) {
        return;
    }
    for (i = 0; i < p->nexp; i++)
        if (strcmp(p->expected[i], what) == 0)
            return;
    if (p->nexp < 32) {
        snprintf(p->expected[p->nexp], sizeof p->expected[0], "%s", what);
        p->nexp++;
    }
}

static void expect_quoted(Parser *p, const char *text) {
    char buf[64];
    snprintf(buf, sizeof buf, "'%s'", text);
    expect_text(p, buf);
}

static int at(Parser *p, const char *text) {
    return tok_eq(cur(p), text);
}

static int accept(Parser *p, const char *text) {
    if (at(p, text)) {
        p->pos++;
        return 1;
    }
    expect_quoted(p, text);
    return 0;
}

static identifier p_name(Parser *p) {
    AsterToken *t = cur(p);
    if (t->type == TOK_NAME && !is_hard_token(t)) {
        p->pos++;
        return intern_token(p, t);
    }
    expect_text(p, "a name");
    return NULL;
}

static identifier p_attr_name(Parser *p) {
    AsterToken *t = cur(p);
    if (t->type == TOK_NAME) {
        p->pos++;
        return intern_token(p, t);
    }
    expect_text(p, "a name");
    return NULL;
}

static int is_declared_type_name(Parser *p, const AsterToken *t) {
    size_t i;
    if (t->type != TOK_NAME || is_hard_token(t))
        return 0;
    if (ns_has_token(&p->type_names, t))
        return 1;
    for (i = 0; i < p->n_type_params; i++)
        if (ns_has_token(&p->type_param_stack[i], t))
            return 1;
    return 0;
}

static void syntax_error_at(Parser *p, size_t index, const char *fmt, ...) {
    AsterToken *t = tok_at(p, index);
    int width = t->end_line == t->line ? t->end_col - t->col : 1;
    va_list ap;
    p->err->line = t->line;
    p->err->col = t->col;
    p->err->length = width > 0 ? width : 1;
    va_start(ap, fmt);
    vsnprintf(p->err->message, sizeof p->err->message, fmt, ap);
    va_end(ap);
    longjmp(p->jmp, 1);
}

static int pos_line(Parser *p, size_t start) { return tok_at(p, start)->line; }
static int pos_col(Parser *p, size_t start) { return tok_at(p, start)->col; }
static int end_line(Parser *p) { return p->pos ? tok_at(p, p->pos - 1)->end_line : 1; }
static int end_col(Parser *p) { return p->pos ? tok_at(p, p->pos - 1)->end_col : 0; }

#define ATTRS(p, start) pos_line(p, start), pos_col(p, start), end_line(p), end_col(p)

/* --------------------------------------------------------------- 미리 훑기 */

static void prescan(Parser *p) {
    size_t i;
    for (i = 0; i < p->n; i++) {
        AsterToken *t = tok_at(p, i);
        AsterToken *nxt = i + 1 < p->n ? tok_at(p, i + 1) : NULL;
        if (t->type != TOK_NAME)
            continue;
        if ((tok_eq(t, "class") || tok_eq(t, "interface")) && nxt &&
            nxt->type == TOK_NAME) {
            ns_add(&p->type_names, intern_token(p, nxt));
        }
        else if (tok_eq(t, "struct") && nxt && nxt->type == TOK_NAME &&
                 i + 2 < p->n && tok_eq(tok_at(p, i + 2), "{")) {
            ns_add(&p->type_names, intern_token(p, nxt));
            ns_add(&p->struct_type_names, intern_token(p, nxt));
            if (i > 0 && tok_eq(tok_at(p, i - 1), "typedef")) {
                int depth = 0;
                size_t j = i + 2;
                while (j < p->n) {
                    if (tok_eq(tok_at(p, j), "{"))
                        depth++;
                    else if (tok_eq(tok_at(p, j), "}"))
                        depth--;
                    if (depth == 0)
                        break;
                    j++;
                }
                if (j + 1 < p->n && tok_at(p, j + 1)->type == TOK_NAME) {
                    identifier alias = intern_token(p, tok_at(p, j + 1));
                    ns_add(&p->type_names, alias);
                    ns_add(&p->struct_type_names, alias);
                    if (p->naliases < 64) {
                        p->aliases[p->naliases].tag_index = i + 1;
                        p->aliases[p->naliases].alias = alias;
                        p->naliases++;
                    }
                }
            }
        }
        else if (tok_eq(t, "import")) {
            size_t j = i + 1;
            while (j < p->n && !tok_eq(tok_at(p, j), ";") &&
                   tok_at(p, j)->type != TOK_ENDMARKER)
                j++;
            if (j - 1 > i && tok_at(p, j - 1)->type == TOK_NAME)
                ns_add(&p->type_names, intern_token(p, tok_at(p, j - 1)));
        }
    }
}

static void push_type_params(Parser *p, asdl_seq *params) {
    NameSet ns = {0};
    size_t i;
    for (i = 0; i < asdl_seq_LEN(params); i++)
        ns_add(&ns, (identifier)asdl_seq_GET(params, i));
    if (p->n_type_params == p->cap_type_params) {
        p->cap_type_params = p->cap_type_params ? p->cap_type_params * 2 : 4;
        p->type_param_stack = xrealloc(p->type_param_stack,
                                       p->cap_type_params * sizeof *p->type_param_stack);
    }
    p->type_param_stack[p->n_type_params++] = ns;
}

static void pop_type_params(Parser *p) {
    NameSet *ns = &p->type_param_stack[--p->n_type_params];
    free(ns->items);
    ns->items = NULL;
    ns->count = ns->cap = 0;
}

static void push_struct_names(Parser *p, NameSet ns) {
    if (p->n_structs == p->cap_structs) {
        p->cap_structs = p->cap_structs ? p->cap_structs * 2 : 4;
        p->struct_stack = xrealloc(p->struct_stack,
                                   p->cap_structs * sizeof *p->struct_stack);
    }
    p->struct_stack[p->n_structs++] = ns;
}

static void pop_struct_names(Parser *p) {
    NameSet *ns = &p->struct_stack[--p->n_structs];
    free(ns->items);
    ns->items = NULL;
    ns->count = ns->cap = 0;
}

/* -------------------------------------------------------------- 타입 규칙 */

static typeref_ty p_type(Parser *p);
static typeref_ty p_type_base(Parser *p);
static typeref_ty p_primitive_type(Parser *p);
static typeref_ty p_cast_type(Parser *p);
static typeref_ty p_sized_type(Parser *p);
static typeref_ty p_declared_named_type(Parser *p);
static asdl_seq *p_type_args(Parser *p);
static ret_ty p_return_type(Parser *p);

static int p_pointer_suffix(Parser *p) {
    if (tok_eq(cur(p), "*")) {
        p->pos++;
        return 1;
    }
    if (tok_eq(cur(p), "**")) {
        p->pos++;
        return 2;
    }
    return 0;
}

static typeref_ty p_pointer_suffixes(Parser *p, typeref_ty base) {
    int count;
    while ((count = p_pointer_suffix(p)) != 0) {
        int i;
        for (i = 0; i < count; i++)
            base = _Aster_Pointer(base, p->arena);
    }
    return base;
}

static typeref_ty p_primitive_type(Parser *p) {
    AsterToken *t = cur(p);
    prim_ty kind;
    if (t->type != TOK_NAME) {
        expect_text(p, "a primitive type");
        return NULL;
    }
    if (tok_eq(t, "int"))
        kind = IntT;
    else if (tok_eq(t, "float"))
        kind = FloatT;
    else if (tok_eq(t, "bool"))
        kind = BoolT;
    else if (tok_eq(t, "str"))
        kind = StrT;
    else if (tok_eq(t, "null"))
        kind = NullT;
    else {
        expect_text(p, "a primitive type");
        return NULL;
    }
    p->pos++;
    return _Aster_Primitive(kind, p->arena);
}

static typeref_ty p_function_type(Parser *p) {
    size_t start = p->pos;
    Seq params = {0};
    ret_ty ret;

    if (!tok_eq(cur(p), "fn") || !tok_eq(tok_at(p, start + 1), "("))
        return NULL;
    p->pos++;
    if (!accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    if (!accept(p, ")")) {
        for (;;) {
            typeref_ty param = p_type(p);
            if (!param) {
                seq_clear(&params);
                p->pos = start;
                return NULL;
            }
            seq_push(&params, param);
            if (!accept(p, ","))
                break;
        }
        if (!accept(p, ")")) {
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, "->")) {
        seq_clear(&params);
        p->pos = start;
        return NULL;
    }
    if (tok_eq(cur(p), "void")) {
        p->pos++;
        ret = _Aster_VoidRet(p->arena);
    }
    else {
        typeref_ty result = p_type(p);
        if (!result) {
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
        ret = _Aster_ValueRet(result, p->arena);
    }
    return _Aster_FunctionTy(seq_make(&params, p->arena), ret, p->arena);
}

static typeref_ty p_type_base(Parser *p) {
    typeref_ty base = p_function_type(p);
    if (base)
        return base;
    base = p_primitive_type(p);
    if (base)
        return base;
    if (accept(p, "struct")) {
        identifier tag = p_name(p);
        if (!tag)
            return NULL;
        return _Aster_StructRef(tag, p->arena);
    }
    {
        identifier name = p_name(p);
        asdl_seq *args;
        if (!name)
            return NULL;
        args = p_type_args(p);
        return _Aster_Named(name, args ? args : empty_seq(p), p->arena);
    }
}

static typeref_ty p_union_arm(Parser *p) {
    size_t start = p->pos;
    typeref_ty base = p_type_base(p);
    if (!base) {
        p->pos = start;
        return NULL;
    }
    base = p_pointer_suffixes(p, base);
    if (accept(p, "?"))
        base = _Aster_Nullable(base, p->arena);
    return base;
}

static typeref_ty p_type(Parser *p) {
    size_t start = p->pos;
    typeref_ty first = p_union_arm(p);
    Seq arms = {0};
    if (!first) {
        p->pos = start;
        return NULL;
    }
    if (!at(p, "|"))
        return first;
    seq_push(&arms, first);
    while (accept(p, "|")) {
        typeref_ty arm = p_union_arm(p);
        if (!arm) {
            seq_clear(&arms);
            p->pos = start;
            return NULL;
        }
        seq_push(&arms, arm);
    }
    return _Aster_UnionTy(seq_make(&arms, p->arena), p->arena);
}

static asdl_seq *p_type_args(Parser *p) {
    size_t start = p->pos;
    Seq args = {0};
    if (!accept(p, "<")) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        typeref_ty t = p_type(p);
        if (!t) {
            seq_clear(&args);
            p->pos = start;
            return NULL;
        }
        seq_push(&args, t);
        if (!accept(p, ","))
            break;
    }
    if (!accept(p, ">")) {
        seq_clear(&args);
        p->pos = start;
        return NULL;
    }
    return seq_make(&args, p->arena);
}

static typeref_ty p_named_type(Parser *p) {
    size_t start = p->pos;
    identifier name = p_name(p);
    asdl_seq *args;
    if (!name) {
        p->pos = start;
        return NULL;
    }
    args = p_type_args(p);
    return _Aster_Named(name, args ? args : empty_seq(p), p->arena);
}

static ret_ty p_return_type(Parser *p) {
    if (accept(p, "void"))
        return _Aster_VoidRet(p->arena);
    {
        typeref_ty t = p_type(p);
        return t ? _Aster_ValueRet(t, p->arena) : NULL;
    }
}

static typeref_ty p_declared_named_type(Parser *p) {
    AsterToken *t = cur(p);
    identifier name;
    asdl_seq *args;
    if (!is_declared_type_name(p, t)) {
        expect_text(p, "a declared type name");
        return NULL;
    }
    name = intern_token(p, t);
    p->pos++;
    args = p_type_args(p);
    return _Aster_Named(name, args ? args : empty_seq(p), p->arena);
}

static typeref_ty p_cast_type(Parser *p) {
    typeref_ty base = p_primitive_type(p);
    if (!base) {
        if (accept(p, "struct")) {
            identifier tag = p_name(p);
            if (!tag)
                return NULL;
            base = _Aster_StructRef(tag, p->arena);
        }
        else {
            base = p_declared_named_type(p);
            if (!base)
                return NULL;
        }
    }
    base = p_pointer_suffixes(p, base);
    if (accept(p, "?"))
        base = _Aster_Nullable(base, p->arena);
    return base;
}

static typeref_ty p_sized_type(Parser *p) {
    typeref_ty base = p_primitive_type(p);
    if (!base) {
        if (accept(p, "struct")) {
            identifier tag = p_name(p);
            if (!tag)
                return NULL;
            base = _Aster_StructRef(tag, p->arena);
        }
        else {
            base = p_declared_named_type(p);
            if (!base)
                return NULL;
        }
    }
    return p_pointer_suffixes(p, base);
}

/* -------------------------------------------------------------- 식 규칙 */

static expr_ty p_expression(Parser *p);
static expr_ty p_pipe_expr(Parser *p);
static expr_ty p_unary(Parser *p);
static expr_ty p_postfix_expr(Parser *p);
static expr_ty p_primary(Parser *p);
static asdl_seq *p_arguments(Parser *p);
static asdl_seq *p_parameters(Parser *p);

static int64_t token_int(Parser *p, size_t index) {
    AsterToken *t = tok_at(p, index);
    char small[64];
    char *buf = small;
    long long v;
    if ((size_t)t->length >= sizeof small)
        buf = xrealloc(NULL, (size_t)t->length + 1);
    memcpy(buf, t->start, (size_t)t->length);
    buf[t->length] = 0;
    errno = 0;
    v = strtoll(buf, NULL, 10);
    if (errno == ERANGE)
        syntax_error_at(p, index, "integer literal is too large");
    if (buf != small)
        free(buf);
    return (int64_t)v;
}

static double token_float(Parser *p, size_t index) {
    AsterToken *t = tok_at(p, index);
    char small[64];
    char *buf = small;
    double v;
    if ((size_t)t->length >= sizeof small)
        buf = xrealloc(NULL, (size_t)t->length + 1);
    memcpy(buf, t->start, (size_t)t->length);
    buf[t->length] = 0;
    v = strtod(buf, NULL);
    if (buf != small)
        free(buf);
    return v;
}

static asdl_seq *p_call_arguments(Parser *p) {
    size_t start = p->pos;
    int saved = p->no_arrow;
    asdl_seq *args;
    p->no_arrow = 0;
    args = at(p, ")") ? empty_seq(p) : p_arguments(p);
    if (!args || !accept(p, ")")) {
        p->no_arrow = saved;
        p->pos = start;
        return NULL;
    }
    p->no_arrow = saved;
    return args;
}

static expr_ty p_new_expr(Parser *p) {
    size_t start = p->pos;
    identifier cls;
    asdl_seq *type_args, *args;
    p->pos++;                                   /* 'new' */
    cls = p_name(p);
    if (!cls) {
        p->pos = start;
        return NULL;
    }
    type_args = p_type_args(p);
    if (!accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    args = p_call_arguments(p);
    if (!args) {
        p->pos = start;
        return NULL;
    }
    return _Aster_New(cls, type_args ? type_args : empty_seq(p), args,
                      ATTRS(p, start), p->arena);
}

static expr_ty p_sizeof_expr(Parser *p) {
    size_t start = p->pos;
    int saved = p->no_arrow;
    typeref_ty ty;
    if (!(accept(p, "sizeof") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    p->no_arrow = 0;
    ty = p_sized_type(p);
    if (!ty || !accept(p, ")")) {
        p->no_arrow = saved;
        p->pos = start;
        return NULL;
    }
    p->no_arrow = saved;
    return _Aster_SizeOf(ty, ATTRS(p, start), p->arena);
}

static expr_ty p_list_display(Parser *p) {
    size_t start = p->pos;
    Seq elements = {0};
    p->pos++;                                   /* '[' */
    if (accept(p, "]"))
        return _Aster_List(empty_seq(p), ATTRS(p, start), p->arena);
    for (;;) {
        expr_ty e;
        if (accept(p, "*")) {
            expr_ty inner = p_expression(p);
            if (!inner) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
            e = _Aster_Spread(inner, ATTRS(p, start), p->arena);
        }
        else {
            e = p_expression(p);
            if (!e) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
        }
        if (elements.count == 0 && accept(p, "for")) {
            identifier name = p_name(p);
            expr_ty iter, cond = NULL;
            if (!name || !accept(p, "in")) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
            iter = p_expression(p);
            if (!iter) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
            if (accept(p, "if")) {
                cond = p_expression(p);
                if (!cond) {
                    seq_clear(&elements);
                    p->pos = start;
                    return NULL;
                }
            }
            if (!accept(p, "]")) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
            seq_clear(&elements);
            return _Aster_ListComp(e, name, iter, cond, ATTRS(p, start), p->arena);
        }
        seq_push(&elements, e);
        if (!accept(p, ","))
            break;
        if (at(p, "]"))
            break;
    }
    if (!accept(p, "]")) {
        seq_clear(&elements);
        p->pos = start;
        return NULL;
    }
    return _Aster_List(seq_make(&elements, p->arena), ATTRS(p, start), p->arena);
}

static expr_ty p_dict_or_set_display(Parser *p) {
    size_t start = p->pos;
    Seq keys = {0}, values = {0}, elements = {0};
    expr_ty first;
    p->pos++;                                   /* '{' */

    if (accept(p, "}"))
        return _Aster_Dict(empty_seq(p), empty_seq(p), ATTRS(p, start), p->arena);

    if (accept(p, "**")) {
        first = p_expression(p);
        if (!first) {
            p->pos = start;
            return NULL;
        }
        seq_push(&keys, _Aster_Spread(first, ATTRS(p, start), p->arena));
        seq_push(&values, NULL);
        while (accept(p, ",")) {
            expr_ty k, v;
            if (at(p, "}"))
                break;
            if (accept(p, "**")) {
                k = p_expression(p);
                if (!k) {
                    seq_clear(&keys);
                    seq_clear(&values);
                    p->pos = start;
                    return NULL;
                }
                seq_push(&keys, _Aster_Spread(k, ATTRS(p, start), p->arena));
                seq_push(&values, NULL);
                continue;
            }
            k = p_expression(p);
            if (!k || !accept(p, ":")) {
                seq_clear(&keys);
                seq_clear(&values);
                p->pos = start;
                return NULL;
            }
            v = p_expression(p);
            if (!v) {
                seq_clear(&keys);
                seq_clear(&values);
                p->pos = start;
                return NULL;
            }
            seq_push(&keys, k);
            seq_push(&values, v);
        }
        if (!accept(p, "}")) {
            seq_clear(&keys);
            seq_clear(&values);
            p->pos = start;
            return NULL;
        }
        return _Aster_Dict(seq_make(&keys, p->arena), seq_make(&values, p->arena),
                           ATTRS(p, start), p->arena);
    }

    if (accept(p, "*")) {
        expr_ty inner = p_expression(p);
        if (!inner) {
            p->pos = start;
            return NULL;
        }
        first = _Aster_Spread(inner, ATTRS(p, start), p->arena);
    }
    else {
        first = p_expression(p);
        if (!first) {
            p->pos = start;
            return NULL;
        }
    }
    if (accept(p, ":")) {
        expr_ty value = p_expression(p);
        if (!value) {
            p->pos = start;
            return NULL;
        }
        if (accept(p, "for")) {
            identifier name = p_name(p);
            expr_ty iter, cond = NULL;
            if (!name || !accept(p, "in")) {
                p->pos = start;
                return NULL;
            }
            iter = p_expression(p);
            if (!iter) {
                p->pos = start;
                return NULL;
            }
            if (accept(p, "if")) {
                cond = p_expression(p);
                if (!cond) {
                    p->pos = start;
                    return NULL;
                }
            }
            if (!accept(p, "}")) {
                p->pos = start;
                return NULL;
            }
            return _Aster_DictComp(first, value, name, iter, cond,
                                   ATTRS(p, start), p->arena);
        }
        seq_push(&keys, first);
        seq_push(&values, value);
        while (accept(p, ",")) {
            expr_ty k, v;
            if (at(p, "}"))
                break;
            if (accept(p, "**")) {
                k = p_expression(p);
                if (!k) {
                    seq_clear(&keys);
                    seq_clear(&values);
                    p->pos = start;
                    return NULL;
                }
                seq_push(&keys, _Aster_Spread(k, ATTRS(p, start), p->arena));
                seq_push(&values, NULL);
                continue;
            }
            k = p_expression(p);
            if (!k || !accept(p, ":")) {
                seq_clear(&keys);
                seq_clear(&values);
                p->pos = start;
                return NULL;
            }
            v = p_expression(p);
            if (!v) {
                seq_clear(&keys);
                seq_clear(&values);
                p->pos = start;
                return NULL;
            }
            seq_push(&keys, k);
            seq_push(&values, v);
        }
        if (!accept(p, "}")) {
            seq_clear(&keys);
            seq_clear(&values);
            p->pos = start;
            return NULL;
        }
        return _Aster_Dict(seq_make(&keys, p->arena), seq_make(&values, p->arena),
                           ATTRS(p, start), p->arena);
    }

    if (accept(p, "for")) {
        identifier name = p_name(p);
        expr_ty iter, cond = NULL;
        if (!name || !accept(p, "in")) {
            p->pos = start;
            return NULL;
        }
        iter = p_expression(p);
        if (!iter) {
            p->pos = start;
            return NULL;
        }
        if (accept(p, "if")) {
            cond = p_expression(p);
            if (!cond) {
                p->pos = start;
                return NULL;
            }
        }
        if (!accept(p, "}")) {
            p->pos = start;
            return NULL;
        }
        return _Aster_SetComp(first, name, iter, cond, ATTRS(p, start), p->arena);
    }

    seq_push(&elements, first);
    while (accept(p, ",")) {
        expr_ty e;
        if (at(p, "}"))
            break;
        if (accept(p, "*")) {
            expr_ty inner = p_expression(p);
            if (!inner) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
            e = _Aster_Spread(inner, ATTRS(p, start), p->arena);
        }
        else {
            e = p_expression(p);
            if (!e) {
                seq_clear(&elements);
                p->pos = start;
                return NULL;
            }
        }
        seq_push(&elements, e);
    }
    if (!accept(p, "}")) {
        seq_clear(&elements);
        p->pos = start;
        return NULL;
    }
    return _Aster_Set(seq_make(&elements, p->arena), ATTRS(p, start), p->arena);
}

/* f"..." 리터럴. {식}과 {{ }} 이스케이프를 처리한다. 'f'는 이미 확인했다. */
static void fstr_push_char(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 1 > *cap) {
        *cap = *cap ? *cap * 2 : 32;
        *buf = xrealloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
}

static expr_ty p_fstring(Parser *p) {
    size_t start = p->pos;
    Seq parts = {0};
    char *lit = NULL;
    size_t lit_len = 0, lit_cap = 0;
    int saw_formatted = 0;

    p->pos++;                                   /* 'f' */
    while (cur(p)->type == TOK_STRING) {
        AsterToken *t = cur(p);
        int len = 0;
        AsterError err;
        char *decoded = Aster_DecodeString(t->start, t->length, &len, &err);
        int line = t->line, col = t->col, eline = t->end_line, ecol = t->end_col;
        size_t i = 0;

        if (!decoded)
            syntax_error_at(p, p->pos, "invalid string literal: %s", err.message);

        while (i < (size_t)len) {
            char c = decoded[i];
            if (c == '{' && i + 1 < (size_t)len && decoded[i + 1] == '{') {
                fstr_push_char(&lit, &lit_len, &lit_cap, '{');
                i += 2;
                continue;
            }
            if (c == '}' && i + 1 < (size_t)len && decoded[i + 1] == '}') {
                fstr_push_char(&lit, &lit_len, &lit_cap, '}');
                i += 2;
                continue;
            }
            if (c == '}') {
                free(decoded);
                syntax_error_at(p, p->pos, "unmatched '}' in f-string");
            }
            if (c != '{') {
                fstr_push_char(&lit, &lit_len, &lit_cap, c);
                i++;
                continue;
            }
            {
                size_t j = i + 1;
                int depth = 0;
                size_t expr_end, spec_start = (size_t)-1;
                int conversion = -1;
                expr_ty value;
                AsterError e;

                if (j >= (size_t)len || decoded[j] == '}')
                    syntax_error_at(p, p->pos, "empty braces in f-string");

                while (j < (size_t)len) {
                    char d = decoded[j];
                    if (d == '(' || d == '[' || d == '{') {
                        depth++;
                    }
                    else if (d == ')' || d == ']' || d == '}') {
                        if (depth == 0)
                            break;
                        depth--;
                    }
                    else if (depth == 0 && d == '!' &&
                             !(j + 1 < (size_t)len && decoded[j + 1] == '=')) {
                        break;
                    }
                    else if (depth == 0 && d == ':') {
                        break;
                    }
                    j++;
                }
                if (j >= (size_t)len)
                    syntax_error_at(p, p->pos, "'}' expected in f-string");
                expr_end = j;

                if (decoded[j] == '!') {
                    char conv = j + 1 < (size_t)len ? decoded[j + 1] : 0;
                    if (conv != 'r' && conv != 's')
                        syntax_error_at(p, p->pos,
                                        "f-string conversion must be !r or !s");
                    conversion = conv;
                    j += 2;
                }
                if (j < (size_t)len && decoded[j] == ':') {
                    spec_start = j + 1;
                    j++;
                    while (j < (size_t)len && decoded[j] != '}')
                        j++;
                }
                if (j >= (size_t)len || decoded[j] != '}')
                    syntax_error_at(p, p->pos, "'}' expected in f-string");

                if (lit_len > 0) {
                    Value v = Value_Obj((Object *)AsterString_FromUTF8(lit, lit_len));
                    seq_push(&parts, _Aster_Constant(v, line, col, eline, ecol, p->arena));
                    lit_len = 0;
                }
                value = Aster_ParseExpression(decoded + i + 1, expr_end - i - 1,
                                              p->arena, &e);
                if (!value) {
                    free(decoded);
                    syntax_error_at(p, p->pos, "invalid f-string expression: %s",
                                    e.message);
                }
                {
                    expr_ty spec = NULL;
                    if (spec_start != (size_t)-1) {
                        Value v = Value_Obj((Object *)AsterString_FromUTF8(
                            decoded + spec_start, j - spec_start));
                        spec = _Aster_Constant(v, line, col, eline, ecol, p->arena);
                    }
                    seq_push(&parts, _Aster_FormattedValue(value, conversion, spec,
                                                           line, col, eline, ecol,
                                                           p->arena));
                }
                saw_formatted = 1;
                i = j + 1;
            }
        }
        free(decoded);
        p->pos++;
    }
    if (!saw_formatted && lit_len == 0) {
        free(lit);
        p->pos = start;
        return NULL;
    }
    if (lit_len > 0) {
        AsterToken *first = tok_at(p, start);
        AsterToken *last = tok_at(p, p->pos - 1);
        Value v = Value_Obj((Object *)AsterString_FromUTF8(lit, lit_len));
        seq_push(&parts, _Aster_Constant(v, first->line, first->col, last->end_line,
                                         last->end_col, p->arena));
    }
    free(lit);
    return _Aster_FString(seq_make(&parts, p->arena), ATTRS(p, start), p->arena);
}

/* int(x), str(x) 같은 기본 타입 변환 호출. 인자가 없으면 기본값으로 바꾼다. */
static expr_ty p_conversion_expr(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty = p_primitive_type(p);
    asdl_seq *args;
    if (!ty || !accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    args = p_call_arguments(p);
    if (!args) {
        p->pos = start;
        return NULL;
    }
    if (asdl_seq_LEN(args) == 0) {
        Value v;
        switch (ty->v.Primitive.kind) {
        case IntT:
            v = Value_Int(0);
            break;
        case FloatT:
            v = Value_Float(0.0);
            break;
        case BoolT:
            v = Value_Bool(0);
            break;
        case StrT:
            v = Value_Obj((Object *)AsterString_FromCStr(""));
            break;
        default:
            p->pos = start;
            return NULL;
        }
        return _Aster_Cast(ty, _Aster_Constant(v, ATTRS(p, start), p->arena),
                           ATTRS(p, start), p->arena);
    }
    if (asdl_seq_LEN(args) != 1)
        syntax_error_at(p, start, "conversion takes exactly one argument");
    return _Aster_Cast(ty, (expr_ty)asdl_seq_GET(args, 0), ATTRS(p, start), p->arena);
}

static int is_conversion_type(const AsterToken *t) {
    return t->type == TOK_NAME &&
           (tok_eq(t, "int") || tok_eq(t, "float") || tok_eq(t, "bool") ||
            tok_eq(t, "str"));
}

static expr_ty p_primary(Parser *p) {
    size_t start = p->pos;
    AsterToken *t = cur(p);

    if (t->type == TOK_NAME) {
        if (tok_eq(t, "sizeof"))
            return p_sizeof_expr(p);
        if (tok_eq(t, "new"))
            return p_new_expr(p);
        if (tok_eq(t, "f") && tok_at(p, start + 1)->type == TOK_STRING) {
            expr_ty fs = p_fstring(p);
            if (fs)
                return fs;
        }
        if (is_conversion_type(t) && tok_eq(tok_at(p, start + 1), "(")) {
            expr_ty conv = p_conversion_expr(p);
            if (conv)
                return conv;
        }
        if (tok_eq(t, "super")) {
            p->pos++;
            return _Aster_Name("super", ATTRS(p, start), p->arena);
        }
        if (tok_eq(t, "self") || tok_eq(t, "true") || tok_eq(t, "false") ||
            tok_eq(t, "null") || !is_hard_token(t)) {
            p->pos++;
            if (tok_eq(t, "self"))
                return _Aster_Self(ATTRS(p, start), p->arena);
            if (tok_eq(t, "true"))
                return _Aster_Constant(Value_Bool(1), ATTRS(p, start), p->arena);
            if (tok_eq(t, "false"))
                return _Aster_Constant(Value_Bool(0), ATTRS(p, start), p->arena);
            if (tok_eq(t, "null"))
                return _Aster_Constant(Value_Null(), ATTRS(p, start), p->arena);
            return _Aster_Name(intern_token(p, t), ATTRS(p, start), p->arena);
        }
    }
    else if (t->type == TOK_INT) {
        int64_t v = token_int(p, start);
        p->pos++;
        return _Aster_Constant(Value_Int(v), ATTRS(p, start), p->arena);
    }
    else if (t->type == TOK_FLOAT) {
        double v = token_float(p, start);
        p->pos++;
        return _Aster_Constant(Value_Float(v), ATTRS(p, start), p->arena);
    }
    else if (t->type == TOK_STRING) {
        int len = 0;
        AsterError e;
        char *decoded;
        Value v;
        decoded = Aster_DecodeString(t->start, t->length, &len, &e);
        if (!decoded)
            syntax_error_at(p, start, "invalid string literal: %s", e.message);
        v = Value_Obj((Object *)AsterString_FromUTF8(decoded, (size_t)len));
        free(decoded);
        p->pos++;
        return _Aster_Constant(v, ATTRS(p, start), p->arena);
    }
    else if (t->type == TOK_LSQB) {
        expr_ty e = p_list_display(p);
        if (e)
            return e;
    }
    else if (t->type == TOK_LBRACE) {
        expr_ty e = p_dict_or_set_display(p);
        if (e)
            return e;
    }
    else if (accept(p, "(")) {
        int saved = p->no_arrow;
        Seq elements = {0};
        expr_ty first;
        p->no_arrow = 0;

        if (accept(p, ")")) {
            p->no_arrow = saved;
            return _Aster_Tuple(empty_seq(p), ATTRS(p, start), p->arena);
        }
        first = p_expression(p);
        if (first && accept(p, ",")) {
            seq_push(&elements, first);
            while (!at(p, ")")) {
                expr_ty e = p_expression(p);
                if (!e) {
                    seq_clear(&elements);
                    p->no_arrow = saved;
                    p->pos = start;
                    return NULL;
                }
                seq_push(&elements, e);
                if (!accept(p, ","))
                    break;
            }
            if (accept(p, ")")) {
                p->no_arrow = saved;
                return _Aster_Tuple(seq_make(&elements, p->arena), ATTRS(p, start), p->arena);
            }
            seq_clear(&elements);
        }
        else if (first && accept(p, ")")) {
            p->no_arrow = saved;
            return first;
        }
        p->no_arrow = saved;
        p->pos = start;
        return NULL;
    }

    expect_text(p, "an expression");
    p->pos = start;
    return NULL;
}

/* '['와 ']' 사이. 인덱스 또는 슬라이스(경계 생략 가능)를 읽는다. */
static expr_ty p_subscript_index(Parser *p) {
    size_t start = p->pos;
    expr_ty lower = NULL, upper = NULL, step = NULL;

    if (!at(p, ":")) {
        lower = p_expression(p);
        if (!lower)
            return NULL;
        if (!accept(p, ":"))
            return lower;
    }
    else {
        p->pos++;                                   /* ':' */
    }
    if (!at(p, "]") && !at(p, ":"))
        upper = p_expression(p);
    if (accept(p, ":")) {
        if (!at(p, "]"))
            step = p_expression(p);
    }
    return _Aster_Slice(lower, upper, step, ATTRS(p, start), p->arena);
}

static expr_ty p_postfix_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty value = p_primary(p);
    if (!value) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        size_t mark = p->pos;
        if (tok_eq(cur(p), "++") || tok_eq(cur(p), "--")) {
            int is_inc = tok_eq(cur(p), "++");
            expr_ty target = value;
            if (target->kind != Name_kind && target->kind != Attribute_kind &&
                target->kind != PtrAttribute_kind && target->kind != Subscript_kind &&
                target->kind != Deref_kind)
                return value;
            p->pos++;
            value = _Aster_Incr(target, is_inc, 0, ATTRS(p, start), p->arena);
            continue;
        }
        if (tok_eq(cur(p), "?.")) {
            identifier attr;
            p->pos++;
            attr = p_attr_name(p);
            if (!attr) {
                p->pos = mark;
                return value;
            }
            value = _Aster_OptionalAttribute(value, attr, ATTRS(p, start), p->arena);
        }
        else if (tok_eq(cur(p), ".")) {
            identifier attr;
            p->pos++;
            attr = p_attr_name(p);
            if (!attr) {
                p->pos = mark;
                return value;
            }
            value = _Aster_Attribute(value, attr, ATTRS(p, start), p->arena);
        }
        else if (!p->no_arrow && tok_eq(cur(p), "->")) {
            identifier attr;
            p->pos++;
            attr = p_attr_name(p);
            if (!attr) {
                p->pos = mark;
                return value;
            }
            value = _Aster_PtrAttribute(value, attr, ATTRS(p, start), p->arena);
        }
        else if (tok_eq(cur(p), "(")) {
            asdl_seq *args;
            p->pos++;
            args = p_call_arguments(p);
            if (!args) {
                p->pos = mark;
                return value;
            }
            value = _Aster_Call(value, args, ATTRS(p, start), p->arena);
        }
        else if (tok_eq(cur(p), "[")) {
            expr_ty index;
            p->pos++;
            index = p_subscript_index(p);
            if (!index || !accept(p, "]")) {
                p->pos = mark;
                return value;
            }
            value = _Aster_Subscript(value, index, ATTRS(p, start), p->arena);
        }
        else {
            return value;
        }
    }
}

static expr_ty p_cast(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty;
    expr_ty operand;
    if (!accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    ty = p_cast_type(p);
    if (!ty || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    operand = p_unary(p);
    if (!operand) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Cast(ty, operand, ATTRS(p, start), p->arena);
}

static expr_ty p_power(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_postfix_expr(p);
    size_t mark;
    if (!left) {
        p->pos = start;
        return NULL;
    }
    mark = p->pos;
    if (accept(p, "**")) {
        expr_ty right = p_unary(p);
        if (right)
            return _Aster_BinOp(left, Pow, right, ATTRS(p, start), p->arena);
        p->pos = mark;
    }
    return left;
}

static expr_ty p_unary(Parser *p) {
    size_t start = p->pos;
    if (tok_eq(cur(p), "++") || tok_eq(cur(p), "--")) {
        int is_inc = tok_eq(cur(p), "++");
        expr_ty target, operand;
        p->pos++;
        target = p_unary(p);
        if (!target) {
            p->pos = start;
            return NULL;
        }
        operand = target;
        if (operand->kind != Name_kind && operand->kind != Attribute_kind &&
            operand->kind != PtrAttribute_kind && operand->kind != Subscript_kind &&
            operand->kind != Deref_kind) {
            expect_text(p, "an incrementable target");
            p->pos = start;
            return NULL;
        }
        return _Aster_Incr(target, is_inc, 1, ATTRS(p, start), p->arena);
    }
    if (tok_eq(cur(p), "+") || tok_eq(cur(p), "-")) {
        unaryop_ty op = tok_eq(cur(p), "+") ? UAdd : USub;
        expr_ty operand;
        p->pos++;
        operand = p_unary(p);
        if (!operand) {
            p->pos = start;
            return NULL;
        }
        return _Aster_UnaryOp(op, operand, ATTRS(p, start), p->arena);
    }
    if (tok_eq(cur(p), "&")) {
        expr_ty operand;
        p->pos++;
        operand = p_unary(p);
        if (!operand) {
            p->pos = start;
            return NULL;
        }
        return _Aster_AddressOf(operand, ATTRS(p, start), p->arena);
    }
    if (tok_eq(cur(p), "*")) {
        expr_ty operand;
        p->pos++;
        operand = p_unary(p);
        if (!operand) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Deref(operand, ATTRS(p, start), p->arena);
    }
    if (tok_eq(cur(p), "**")) {
        expr_ty operand;
        p->pos++;
        operand = p_unary(p);
        if (!operand) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Deref(_Aster_Deref(operand, ATTRS(p, start), p->arena),
                            ATTRS(p, start), p->arena);
    }
    {
        expr_ty c = p_cast(p);
        if (c)
            return c;
        return p_power(p);
    }
}

static expr_ty p_term(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_unary(p);
    if (!left) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        operator_ty op;
        expr_ty right;
        size_t mark = p->pos;
        if (tok_eq(cur(p), "*"))
            op = Mult;
        else if (tok_eq(cur(p), "/"))
            op = Div;
        else if (tok_eq(cur(p), "//"))
            op = FloorDiv;
        else if (tok_eq(cur(p), "%"))
            op = Mod;
        else
            return left;
        p->pos++;
        right = p_unary(p);
        if (!right) {
            p->pos = mark;
            return left;
        }
        left = _Aster_BinOp(left, op, right, ATTRS(p, start), p->arena);
    }
}

static expr_ty p_arith(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_term(p);
    if (!left) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        operator_ty op;
        expr_ty right;
        size_t mark = p->pos;
        if (tok_eq(cur(p), "+"))
            op = Add;
        else if (tok_eq(cur(p), "-"))
            op = Sub;
        else
            return left;
        p->pos++;
        right = p_term(p);
        if (!right) {
            p->pos = mark;
            return left;
        }
        left = _Aster_BinOp(left, op, right, ATTRS(p, start), p->arena);
    }
}

static int p_comp_op(Parser *p) {
    static const struct { const char *text; cmpop_ty op; } ops[] = {
        {"==", Eq}, {"!=", NotEq}, {"<=", LtE}, {">=", GtE}, {"<", Lt}, {">", Gt},
    };
    size_t i;
    for (i = 0; i < sizeof ops / sizeof *ops; i++) {
        if (accept(p, ops[i].text))
            return ops[i].op;
    }
    return 0;
}

/* 일반 식 수준의 포함 범위: 1..5, 1..n+1, 1..5..2 */
static expr_ty p_range_value(Parser *p) {
    size_t start = p->pos;
    expr_ty lo = p_arith(p);
    expr_ty hi, step = NULL;
    if (!lo) {
        p->pos = start;
        return NULL;
    }
    if (!accept(p, ".."))
        return lo;
    hi = p_arith(p);
    if (!hi)
        syntax_error_at(p, p->pos, "range end expression is required after '..'");
    {
        size_t mark = p->pos;
        if (accept(p, "..")) {
            step = p_arith(p);
            if (!step)
                p->pos = mark;
        }
    }
    return _Aster_Range(lo, hi, step, ATTRS(p, start), p->arena);
}

static expr_ty p_comparison(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_range_value(p);
    size_t mark;
    if (!left) {
        p->pos = start;
        return NULL;
    }
    mark = p->pos;
    if (tok_eq(cur(p), "not") && tok_eq(tok_at(p, p->pos + 1), "in")) {
        expr_ty right;
        p->pos += 2;
        right = p_range_value(p);
        if (right)
            return _Aster_UnaryOp(Not, _Aster_In(left, right, ATTRS(p, start), p->arena),
                                  ATTRS(p, start), p->arena);
    }
    else if (accept(p, "is")) {
        int negate = 0;
        typeref_ty ty;
        if (accept(p, "not"))
            negate = 1;
        ty = p_type(p);
        if (ty) {
            expr_ty e = _Aster_Is(left, ty, ATTRS(p, start), p->arena);
            if (negate)
                e = _Aster_UnaryOp(Not, e, ATTRS(p, start), p->arena);
            return e;
        }
    }
    else if (accept(p, "in")) {
        expr_ty right = p_range_value(p);
        if (right)
            return _Aster_In(left, right, ATTRS(p, start), p->arena);
    }
    else {
        int op = p_comp_op(p);
        if (op) {
            expr_ty right = p_range_value(p);
            if (right)
                return _Aster_Compare(left, (cmpop_ty)op, right, ATTRS(p, start), p->arena);
        }
    }
    p->pos = mark;
    return left;
}

static expr_ty p_not_expr(Parser *p) {
    size_t start = p->pos;
    if (accept(p, "not")) {
        expr_ty operand = p_not_expr(p);
        if (!operand) {
            p->pos = start;
            return NULL;
        }
        return _Aster_UnaryOp(Not, operand, ATTRS(p, start), p->arena);
    }
    {
        expr_ty e = p_comparison(p);
        if (!e)
            p->pos = start;
        return e;
    }
}

static expr_ty p_and_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_not_expr(p);
    if (!left) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        expr_ty right;
        size_t mark = p->pos;
        if (!accept(p, "and"))
            return left;
        right = p_not_expr(p);
        if (!right) {
            p->pos = mark;
            return left;
        }
        left = _Aster_BoolOp(And, left, right, ATTRS(p, start), p->arena);
    }
}

static expr_ty p_or_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_and_expr(p);
    if (!left) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        expr_ty right;
        size_t mark = p->pos;
        if (!accept(p, "or"))
            return left;
        right = p_and_expr(p);
        if (!right) {
            p->pos = mark;
            return left;
        }
        left = _Aster_BoolOp(Or, left, right, ATTRS(p, start), p->arena);
    }
}

static expr_ty p_coalesce_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_or_expr(p);
    size_t mark;
    if (!left) {
        p->pos = start;
        return NULL;
    }
    mark = p->pos;
    if (accept(p, "??")) {
        expr_ty right = p_coalesce_expr(p);
        if (right)
            return _Aster_Coalesce(left, right, ATTRS(p, start), p->arena);
        p->pos = mark;
    }
    return left;
}

static expr_ty p_pipe_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty left = p_coalesce_expr(p);
    if (!left) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        expr_ty right;
        size_t mark = p->pos;
        if (!accept(p, "|>"))
            return left;
        right = p_coalesce_expr(p);
        if (!right) {
            p->pos = mark;
            return left;
        }
        left = _Aster_Pipe(left, right, ATTRS(p, start), p->arena);
    }
}

static expr_ty p_lambda_body(Parser *p) {
    int saved = p->no_arrow;
    expr_ty e;
    p->no_arrow = 1;
    e = p_pipe_expr(p);
    p->no_arrow = saved;
    return e;
}

/* 람다 매개변수: '타입 이름' 또는 '이름' 둘 다 허용한다. */
static asdl_seq *p_lambda_params(Parser *p) {
    size_t start = p->pos;
    Seq out = {0};
    for (;;) {
        size_t mark = p->pos;
        typeref_ty ty = p_type(p);
        identifier name = ty ? p_name(p) : NULL;
        if (!ty || !name) {
            p->pos = mark;
            ty = NULL;
            name = p_name(p);
        }
        if (!name) {
            seq_clear(&out);
            p->pos = start;
            return NULL;
        }
        seq_push(&out, _Aster_lparam(ty, name, p->arena));
        if (!accept(p, ","))
            break;
    }
    return seq_make(&out, p->arena);
}

static expr_ty p_lambda_expr(Parser *p) {
    size_t start = p->pos;
    AsterToken *t = cur(p);
    Seq params = {0};
    expr_ty body;
    ret_ty returns;
    int new_style = 0;

    /* 새 문법: '매개변수 -> 반환타입 => 본문', 옛 문법: 'x => 본문 -> 타입' */
    if (t->type == TOK_NAME && !is_hard_token(t) && p->pos + 1 < p->n &&
        (tok_eq(tok_at(p, p->pos + 1), "->") ||
         tok_eq(tok_at(p, p->pos + 1), "=>"))) {
        identifier name = intern_token(p, t);
        new_style = tok_eq(tok_at(p, p->pos + 1), "->");
        p->pos += 1;
        seq_push(&params, _Aster_lparam(NULL, name, p->arena));
    }
    else if (at(p, "(")) {
        asdl_seq *ps;
        size_t i;
        p->pos++;
        ps = at(p, ")") ? empty_seq(p) : p_lambda_params(p);
        if (!ps || !accept(p, ")")) {
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
        for (i = 0; i < asdl_seq_LEN(ps); i++) {
            lparam_ty pr = (lparam_ty)asdl_seq_GET(ps, i);
            seq_push(&params, _Aster_lparam(pr->ty, pr->name, p->arena));
        }
        if (at(p, "->"))
            new_style = 1;
        else if (!at(p, "=>")) {
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
        else {
            /* 옛 문법의 괄호 매개변수는 타입이 있어야 한다. */
            for (i = 0; i < asdl_seq_LEN(ps); i++) {
                lparam_ty pr = (lparam_ty)asdl_seq_GET(ps, i);
                if (!pr->ty) {
                    seq_clear(&params);
                    p->pos = start;
                    return NULL;
                }
            }
        }
    }
    else {
        return NULL;
    }

    if (new_style) {
        returns = accept(p, "->") ? p_return_type(p) : NULL;
        if (!returns || !accept(p, "=>")) {
            /* 'p->x' 같은 포인터 접근일 수 있으므로 조용히 되돌린다. */
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
        body = p_pipe_expr(p);
        if (!body) {
            seq_clear(&params);
            p->pos = start;
            return NULL;
        }
        return _Aster_Lambda(seq_make(&params, p->arena), returns, body,
                             ATTRS(p, start), p->arena);
    }

    /* 옛 문법: '매개변수 => 본문 -> 반환타입' */
    if (!accept(p, "=>")) {
        seq_clear(&params);
        p->pos = start;
        return NULL;
    }
    body = p_lambda_body(p);
    if (!body)
        syntax_error_at(p, p->pos, "lambda body expression is required");
    if (!accept(p, "->"))
        syntax_error_at(p, p->pos,
                        "lambda needs a return type: use 'x => expr -> type'");
    returns = p_return_type(p);
    if (!returns)
        syntax_error_at(p, p->pos, "lambda return type is required after '->'");
    if (at(p, "->"))
        syntax_error_at(p, p->pos,
                        "pointer access (->) in a lambda body must be parenthesized: (p->x)");
    return _Aster_Lambda(seq_make(&params, p->arena), returns, body,
                         ATTRS(p, start), p->arena);
}

static expr_ty p_expression(Parser *p) {
    size_t start = p->pos;
    expr_ty e = p_lambda_expr(p);
    if (e)
        return e;
    p->pos = start;
    return p_pipe_expr(p);
}

static asdl_seq *p_arguments(Parser *p) {
    size_t start = p->pos;
    Seq out = {0};
    for (;;) {
        expr_ty e = p_expression(p);
        if (!e) {
            seq_clear(&out);
            p->pos = start;
            return NULL;
        }
        seq_push(&out, e);
        if (!accept(p, ","))
            break;
    }
    return seq_make(&out, p->arena);
}

/* -------------------------------------------------------------- 매개변수 */

static param_ty p_parameter(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty = p_type(p);
    identifier name;
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    return _Aster_param(ty, name, p->arena);
}

static asdl_seq *p_parameters(Parser *p) {
    size_t start = p->pos;
    Seq out = {0};
    for (;;) {
        param_ty pr = p_parameter(p);
        if (!pr) {
            seq_clear(&out);
            p->pos = start;
            return NULL;
        }
        seq_push(&out, pr);
        if (!accept(p, ","))
            break;
    }
    return seq_make(&out, p->arena);
}

static asdl_seq *p_type_params(Parser *p) {
    size_t start = p->pos;
    Seq out = {0};
    if (!accept(p, "<")) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        identifier name = p_name(p);
        if (!name) {
            seq_clear(&out);
            p->pos = start;
            return NULL;
        }
        seq_push(&out, name);
        if (!accept(p, ","))
            break;
    }
    if (!accept(p, ">")) {
        seq_clear(&out);
        p->pos = start;
        return NULL;
    }
    return seq_make(&out, p->arena);
}

static int p_function_signature(Parser *p, asdl_seq **params_out, ret_ty *ret_out) {
    size_t start = p->pos;
    asdl_seq *params;
    ret_ty returns;
    if (!accept(p, "(")) {
        p->pos = start;
        return 0;
    }
    params = at(p, ")") ? empty_seq(p) : p_parameters(p);
    if (!params || !(accept(p, ")") && accept(p, "->"))) {
        p->pos = start;
        return 0;
    }
    returns = p_return_type(p);
    if (!returns) {
        p->pos = start;
        return 0;
    }
    *params_out = params;
    *ret_out = returns;
    return 1;
}

/* ---------------------------------------------------------------- 문장 */

static stmt_ty p_statement(Parser *p);
static stmt_ty p_var_decl_core(Parser *p, int is_const);
static stmt_ty p_assign_core(Parser *p);

static asdl_seq *p_block(Parser *p) {
    size_t start = p->pos;
    Seq body = {0};
    stmt_ty s;
    if (!accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    while ((s = p_statement(p)) != NULL)
        seq_push(&body, s);
    if (!accept(p, "}")) {
        seq_clear(&body);
        p->pos = start;
        return NULL;
    }
    return seq_make(&body, p->arena);
}

static asdl_seq *p_package_stmt(Parser *p) {
    size_t start = p->pos;
    asdl_seq *path;
    if (!accept(p, "package")) {
        p->pos = start;
        return NULL;
    }
    /* dotted_name은 아래에 정의되어 있으므로 여기서는 직접 읽는다. */
    {
        Seq parts = {0};
        identifier first = p_name(p);
        if (!first)
            goto fail;
        seq_push(&parts, first);
        for (;;) {
            size_t mark = p->pos;
            identifier part;
            if (!accept(p, "."))
                break;
            part = p_name(p);
            if (!part) {
                p->pos = mark;
                break;
            }
            seq_push(&parts, part);
        }
        if (!accept(p, ";")) {
            seq_clear(&parts);
            goto fail;
        }
        path = seq_make(&parts, p->arena);
    }
    return path;
fail:
    p->pos = start;
    return NULL;
}

static asdl_seq *p_dotted_name(Parser *p) {
    size_t start = p->pos;
    Seq parts = {0};
    identifier first = p_name(p);
    if (!first) {
        p->pos = start;
        return NULL;
    }
    seq_push(&parts, first);
    for (;;) {
        size_t mark = p->pos;
        identifier part;
        if (!accept(p, "."))
            break;
        part = p_name(p);
        if (!part) {
            p->pos = mark;
            break;
        }
        seq_push(&parts, part);
    }
    return seq_make(&parts, p->arena);
}

static stmt_ty p_import_stmt(Parser *p) {
    size_t start = p->pos;
    if (accept(p, "import")) {
        asdl_seq *path = p_dotted_name(p);
        identifier alias = NULL;
        if (!path) {
            p->pos = start;
            return NULL;
        }
        if (accept(p, ".")) {
            if (!(accept(p, "*") && accept(p, ";"))) {
                p->pos = start;
                return NULL;
            }
            return _Aster_Import(path, 1, NULL, ATTRS(p, start), p->arena);
        }
        if (accept(p, "as")) {
            alias = p_name(p);
            if (!alias) {
                p->pos = start;
                return NULL;
            }
        }
        if (!accept(p, ";")) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Import(path, 0, alias, ATTRS(p, start), p->arena);
    }
    if (accept(p, "from")) {
        asdl_seq *module = p_dotted_name(p);
        identifier name = NULL, alias = NULL;
        if (!module || !accept(p, "import")) {
            p->pos = start;
            return NULL;
        }
        if (!accept(p, "*")) {
            name = p_name(p);
            if (!name) {
                p->pos = start;
                return NULL;
            }
            if (accept(p, "as")) {
                alias = p_name(p);
                if (!alias) {
                    p->pos = start;
                    return NULL;
                }
            }
        }
        if (!accept(p, ";")) {
            p->pos = start;
            return NULL;
        }
        return _Aster_ImportFrom(module, name, alias, ATTRS(p, start), p->arena);
    }
    p->pos = start;
    return NULL;
}

static stmt_ty p_return_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty value = NULL;
    if (!accept(p, "return")) {
        p->pos = start;
        return NULL;
    }
    if (!at(p, ";")) {
        value = p_expression(p);
        if (!value) {
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Return(value, ATTRS(p, start), p->arena);
}

static stmt_ty p_yield_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty value = NULL;
    if (!accept(p, "yield")) {
        p->pos = start;
        return NULL;
    }
    if (!at(p, ";")) {
        value = p_expression(p);
        if (!value) {
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Yield(value, ATTRS(p, start), p->arena);
}

static stmt_ty p_throw_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty value;
    identifier exc_type = NULL;
    if (!accept(p, "throw")) {
        p->pos = start;
        return NULL;
    }
    value = p_expression(p);
    if (!value) {
        p->pos = start;
        return NULL;
    }
    if (accept(p, "as")) {
        exc_type = p_name(p);
        if (!exc_type) {
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Throw(value, exc_type, ATTRS(p, start), p->arena);
}

static stmt_ty p_break_stmt(Parser *p) {
    size_t start = p->pos;
    if (!(accept(p, "break") && accept(p, ";"))) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Break(ATTRS(p, start), p->arena);
}

static stmt_ty p_continue_stmt(Parser *p) {
    size_t start = p->pos;
    if (!(accept(p, "continue") && accept(p, ";"))) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Continue(ATTRS(p, start), p->arena);
}

static stmt_ty p_expr_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty value = p_expression(p);
    if (!value || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ExprStmt(value, ATTRS(p, start), p->arena);
}

/* ------------------------------------------------------ 선언, 초기화, 대입 */

static initializer_ty p_initializer(Parser *p, int allow_brace);
static designation_ty p_designated_init(Parser *p);

static initializer_ty p_brace_init(Parser *p) {
    size_t start = p->pos;
    if (!accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    if (at(p, "*") || at(p, "**")) {
        p->pos = start;
        return NULL;
    }
    if (accept(p, "}"))
        return _Aster_PositionalInit(empty_seq(p), p->arena);

    {
        designation_ty first = p_designated_init(p);
        if (first) {
            Seq items = {0};
            seq_push(&items, first);
            while (accept(p, ",")) {
                designation_ty item = p_designated_init(p);
                if (!item) {
                    seq_clear(&items);
                    p->pos = start;
                    return NULL;
                }
                seq_push(&items, item);
            }
            if (!accept(p, "}")) {
                seq_clear(&items);
                p->pos = start;
                return NULL;
            }
            return _Aster_DesignatedInit(seq_make(&items, p->arena), p->arena);
        }
    }

    {
        Seq values = {0};
        for (;;) {
            expr_ty value = p_expression(p);
            if (!value) {
                seq_clear(&values);
                p->pos = start;
                return NULL;
            }
            seq_push(&values, value);
            if (!accept(p, ","))
                break;
        }
        if (!accept(p, "}")) {
            seq_clear(&values);
            p->pos = start;
            return NULL;
        }
        return _Aster_PositionalInit(seq_make(&values, p->arena), p->arena);
    }
}

static designation_ty p_designated_init(Parser *p) {
    size_t start = p->pos;
    identifier field;
    expr_ty value;
    if (!accept(p, ".")) {
        p->pos = start;
        return NULL;
    }
    field = p_name(p);
    if (!field || !accept(p, "=")) {
        p->pos = start;
        return NULL;
    }
    value = p_expression(p);
    if (!value) {
        p->pos = start;
        return NULL;
    }
    return _Aster_designation(field, value, p->arena);
}

static initializer_ty p_initializer(Parser *p, int allow_brace) {
    size_t start = p->pos;
    expr_ty value;
    if (allow_brace) {
        initializer_ty init = p_brace_init(p);
        if (init)
            return init;
        p->pos = start;
    }
    value = p_expression(p);
    if (!value) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ExprInit(value, p->arena);
}

static stmt_ty p_var_decl_core(Parser *p, int is_const) {
    size_t start = p->pos;
    typeref_ty ty = p_type(p);
    identifier name;
    size_t after_name;
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    if (at(p, "(")) {
        Seq names = {0};
        expr_ty value;
        identifier n;
        p->pos++;
        for (;;) {
            n = p_name(p);
            if (!n) {
                seq_clear(&names);
                p->pos = start;
                return NULL;
            }
            seq_push(&names, n);
            if (!accept(p, ","))
                break;
        }
        /* 이름 하나짜리 T(x)는 호출문 f(x) = ...와 구분되지 않는다. */
        if (names.count < 2 || !accept(p, ")") || !accept(p, "=")) {
            seq_clear(&names);
            p->pos = start;
            return NULL;
        }
        value = p_expression(p);
        if (!value) {
            seq_clear(&names);
            p->pos = start;
            return NULL;
        }
        return _Aster_Destructure(seq_make(&names, p->arena), value,
                                  ATTRS(p, start), p->arena);
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    after_name = p->pos;
    if (accept(p, "=")) {
        initializer_ty init = p_initializer(p, type_is_struct(p, ty));
        if (init)
            return _Aster_VarDecl(ty, name, init, is_const, ATTRS(p, start), p->arena);
        p->pos = after_name;
    }
    return _Aster_VarDecl(ty, name, _Aster_NoInit(p->arena), is_const,
                          ATTRS(p, start), p->arena);
}

static stmt_ty p_var_decl(Parser *p) {
    size_t start = p->pos;
    int is_const = 0;
    stmt_ty core;
    if (accept(p, "const"))
        is_const = 1;
    core = p_var_decl_core(p, is_const);
    if (!core || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    if (core->kind == Destructure_kind)
        return core;
    return _Aster_VarDecl(core->v.VarDecl.ty, core->v.VarDecl.name,
                          core->v.VarDecl.init, is_const, ATTRS(p, start), p->arena);
}

static int p_assign_op(Parser *p) {
    static const char *const ops[] = {
        "=", "+=", "-=", "*=", "/=", "//=", "%=", "**=",
    };
    size_t i;
    for (i = 0; i < sizeof ops / sizeof *ops; i++) {
        if (accept(p, ops[i]))
            return (int)i + 1;
    }
    return 0;
}

static expr_ty p_assign_target(Parser *p) {
    size_t start = p->pos;
    if (tok_eq(cur(p), "*")) {
        expr_ty inner;
        p->pos++;
        inner = p_assign_target(p);
        if (!inner) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Deref(inner, ATTRS(p, start), p->arena);
    }
    if (tok_eq(cur(p), "**")) {
        expr_ty inner;
        p->pos++;
        inner = p_assign_target(p);
        if (!inner) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Deref(_Aster_Deref(inner, ATTRS(p, start), p->arena),
                            ATTRS(p, start), p->arena);
    }
    {
        expr_ty target = p_postfix_expr(p);
        if (!target) {
            p->pos = start;
            return NULL;
        }
        if (target->kind == Name_kind || target->kind == Attribute_kind ||
            target->kind == PtrAttribute_kind || target->kind == Subscript_kind ||
            target->kind == Tuple_kind)
            return target;
        expect_text(p, "an assignable target");
        p->pos = start;
        return NULL;
    }
}

static stmt_ty p_assign_core(Parser *p) {
    size_t start = p->pos;
    expr_ty target = p_assign_target(p);
    int op;
    expr_ty value;
    if (!target) {
        p->pos = start;
        return NULL;
    }
    op = p_assign_op(p);
    if (!op) {
        p->pos = start;
        return NULL;
    }
    value = p_expression(p);
    if (!value) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Assign(target, (assign_op_ty)op, value, ATTRS(p, start), p->arena);
}

static stmt_ty p_assign_stmt(Parser *p) {
    size_t start = p->pos;
    stmt_ty core = p_assign_core(p);
    if (!core || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Assign(core->v.Assign.target, core->v.Assign.op,
                         core->v.Assign.value, ATTRS(p, start), p->arena);
}

/* --------------------------------------------------------------- enum */

static stmt_ty p_enum_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    Seq members = {0};
    if (!accept(p, "enum")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    while (!at(p, "}")) {
        identifier mname = p_name(p);
        asdl_seq *params = empty_seq(p);
        expr_ty value = NULL;
        if (!mname) {
            seq_clear(&members);
            p->pos = start;
            return NULL;
        }
        if (accept(p, "(")) {
            Seq ps = {0};
            if (!accept(p, ")")) {
                for (;;) {
                    param_ty pr = p_parameter(p);
                    if (!pr) {
                        seq_clear(&ps);
                        seq_clear(&members);
                        p->pos = start;
                        return NULL;
                    }
                    seq_push(&ps, pr);
                    if (!accept(p, ","))
                        break;
                }
                if (!accept(p, ")")) {
                    seq_clear(&ps);
                    seq_clear(&members);
                    p->pos = start;
                    return NULL;
                }
            }
            params = seq_make(&ps, p->arena);
        }
        if (accept(p, "=")) {
            value = p_expression(p);
            if (!value) {
                seq_clear(&members);
                p->pos = start;
                return NULL;
            }
        }
        seq_push(&members, _Aster_enum_member(mname, params, value, p->arena));
        if (!accept(p, ","))
            break;
    }
    if (!accept(p, "}")) {
        seq_clear(&members);
        p->pos = start;
        return NULL;
    }
    (void)accept(p, ";");
    return _Aster_EnumDef(name, seq_make(&members, p->arena), ATTRS(p, start), p->arena);
}

static stmt_ty p_assert_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty test, message = NULL;
    if (!accept(p, "assert")) {
        p->pos = start;
        return NULL;
    }
    test = p_expression(p);
    if (!test) {
        p->pos = start;
        return NULL;
    }
    if (accept(p, ",")) {
        message = p_expression(p);
        if (!message) {
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Assert(test, message, ATTRS(p, start), p->arena);
}

/* ---------------------------------------------------------------- 함수 */

static stmt_ty p_function_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    asdl_seq *type_params, *params;
    ret_ty returns;
    asdl_seq *body;
    if (!accept(p, "fn")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    type_params = p_type_params(p);
    if (!type_params)
        type_params = empty_seq(p);
    push_type_params(p, type_params);
    if (!p_function_signature(p, &params, &returns)) {
        pop_type_params(p);
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    pop_type_params(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_FunctionDef(name, type_params, params, returns, body, 0,
                              ATTRS(p, start), p->arena);
}

/* extern fn name(params) -> T;  — 본문 없이 선언만 한다. */
static stmt_ty p_extern_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    asdl_seq *type_params, *params;
    ret_ty returns;
    if (!accept(p, "extern")) {
        p->pos = start;
        return NULL;
    }
    if (!accept(p, "fn")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    type_params = p_type_params(p);
    if (!type_params)
        type_params = empty_seq(p);
    push_type_params(p, type_params);
    if (!p_function_signature(p, &params, &returns) || !accept(p, ";")) {
        pop_type_params(p);
        p->pos = start;
        return NULL;
    }
    pop_type_params(p);
    return _Aster_FunctionDef(name, type_params, params, returns, empty_seq(p), 1,
                              ATTRS(p, start), p->arena);
}

/* --------------------------------------------------------------- 클래스 */

static access_ty p_access_modifier(Parser *p) {
    if (accept(p, "public"))
        return Public;
    if (accept(p, "protected"))
        return Protected;
    if (accept(p, "private"))
        return Private;
    if (accept(p, "internal"))
        return Internal;
    return (access_ty)0;
}

static member_ty p_field_decl(Parser *p) {
    size_t start = p->pos;
    access_ty access = p_access_modifier(p);
    int is_static = accept(p, "static");
    typeref_ty ty = p_type(p);
    identifier name;
    expr_ty value = NULL;
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    {
        size_t mark = p->pos;
        if (accept(p, "=")) {
            value = p_expression(p);
            if (!value)
                p->pos = mark;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Field(access, ty, name, value, is_static, ATTRS(p, start), p->arena);
}

static member_ty p_method_def(Parser *p) {
    size_t start = p->pos;
    access_ty access = p_access_modifier(p);
    int is_static = accept(p, "static");
    identifier name;
    asdl_seq *type_params, *params, *body;
    ret_ty returns;
    if (!accept(p, "fn")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    type_params = p_type_params(p);
    if (!type_params)
        type_params = empty_seq(p);
    push_type_params(p, type_params);
    if (!p_function_signature(p, &params, &returns)) {
        pop_type_params(p);
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    pop_type_params(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Method(access, name, type_params, params, returns, body, is_static,
                         ATTRS(p, start), p->arena);
}

static member_ty p_constructor_def(Parser *p) {
    size_t start = p->pos;
    asdl_seq *params, *body;
    if (!(accept(p, "constructor") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    params = at(p, ")") ? empty_seq(p) : p_parameters(p);
    if (!params || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Constructor(params, body, ATTRS(p, start), p->arena);
}

static member_ty p_property_decl(Parser *p) {
    size_t start = p->pos;
    access_ty access = p_access_modifier(p);
    typeref_ty ty;
    identifier name;
    if (!(accept(p, "@") && accept(p, "property"))) {
        p->pos = start;
        return NULL;
    }
    ty = p_type(p);
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Property(access, ty, name, ATTRS(p, start), p->arena);
}

static member_ty p_getter_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    asdl_seq *body;
    if (!accept(p, "getter")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Getter(name, body, ATTRS(p, start), p->arena);
}

static member_ty p_setter_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    typeref_ty ty;
    size_t param_index;
    identifier param_name;
    asdl_seq *body;
    if (!accept(p, "setter")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    ty = p_type(p);
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    param_index = p->pos;
    param_name = p_name(p);
    if (!param_name || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    if (strcmp(param_name, "value") != 0)
        syntax_error_at(p, param_index,
                        "setter parameter must be named 'value' (got '%s')", param_name);
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Setter(name, _Aster_param(ty, param_name, p->arena), body,
                         ATTRS(p, start), p->arena);
}

static member_ty p_class_member(Parser *p) {
    member_ty m = p_constructor_def(p);
    if (m)
        return m;
    m = p_property_decl(p);
    if (m)
        return m;
    m = p_getter_def(p);
    if (m)
        return m;
    m = p_setter_def(p);
    if (m)
        return m;
    m = p_method_def(p);
    if (m)
        return m;
    return p_field_decl(p);
}

static stmt_ty p_class_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    asdl_seq *type_params, *interfaces;
    typeref_ty base = NULL;
    Seq ifaces = {0};
    Seq members = {0};
    member_ty m;
    if (!accept(p, "class")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    type_params = p_type_params(p);
    if (!type_params)
        type_params = empty_seq(p);
    if (accept(p, "extends")) {
        base = p_named_type(p);
        if (!base) {
            p->pos = start;
            return NULL;
        }
    }
    if (accept(p, "implements")) {
        for (;;) {
            typeref_ty iface = p_named_type(p);
            if (!iface) {
                seq_clear(&ifaces);
                p->pos = start;
                return NULL;
            }
            seq_push(&ifaces, iface);
            if (!accept(p, ","))
                break;
        }
    }
    interfaces = seq_make(&ifaces, p->arena);
    if (!accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    push_type_params(p, type_params);
    while ((m = p_class_member(p)) != NULL)
        seq_push(&members, m);
    if (!accept(p, "}")) {
        pop_type_params(p);
        seq_clear(&members);
        p->pos = start;
        return NULL;
    }
    pop_type_params(p);
    return _Aster_ClassDef(name, type_params, base, interfaces,
                           seq_make(&members, p->arena), ATTRS(p, start), p->arena);
}

/* ------------------------------------------------------------- 인터페이스 */

static signature_ty p_interface_method(Parser *p) {
    size_t start = p->pos;
    access_ty access = p_access_modifier(p);
    identifier name;
    asdl_seq *params;
    ret_ty returns;
    if (!accept(p, "fn")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    if (!p_function_signature(p, &params, &returns) || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_signature(access, name, params, returns, p->arena);
}

static stmt_ty p_interface_def(Parser *p) {
    size_t start = p->pos;
    identifier name;
    Seq methods = {0};
    signature_ty m;
    if (!accept(p, "interface")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    while ((m = p_interface_method(p)) != NULL)
        seq_push(&methods, m);
    if (!accept(p, "}")) {
        seq_clear(&methods);
        p->pos = start;
        return NULL;
    }
    return _Aster_InterfaceDef(name, seq_make(&methods, p->arena),
                               ATTRS(p, start), p->arena);
}

/* --------------------------------------------------------------- 구조체 */

static struct_member_ty p_struct_field(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty = p_type(p);
    identifier name;
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_StructField(ty, name, p->arena);
}

static member_init_ty p_member_init(Parser *p) {
    size_t start = p->pos;
    identifier field;
    expr_ty value;
    field = p_name(p);
    if (!field || !accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    value = p_expression(p);
    if (!value || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_member_init(field, value, p->arena);
}

static struct_member_ty p_struct_constructor(Parser *p) {
    size_t start = p->pos;
    size_t name_index = p->pos;
    identifier name;
    asdl_seq *params;
    Seq inits = {0};
    asdl_seq *body;
    int allowed = 0;
    size_t i;

    name = p_name(p);
    if (!name || !accept(p, "(")) {
        p->pos = start;
        return NULL;
    }
    if (p->n_structs > 0) {
        NameSet *ns = &p->struct_stack[p->n_structs - 1];
        for (i = 0; i < ns->count; i++)
            if (strcmp(ns->items[i], name) == 0)
                allowed = 1;
    }
    if (!allowed)
        syntax_error_at(p, name_index,
                        "struct constructor name must match the struct name: '%s'", name);
    params = at(p, ")") ? empty_seq(p) : p_parameters(p);
    if (!params || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    if (accept(p, ":")) {
        for (;;) {
            member_init_ty init = p_member_init(p);
            if (!init) {
                seq_clear(&inits);
                p->pos = start;
                return NULL;
            }
            seq_push(&inits, init);
            if (!accept(p, ","))
                break;
        }
    }
    body = p_block(p);
    if (!body) {
        seq_clear(&inits);
        p->pos = start;
        return NULL;
    }
    return _Aster_StructCtor(params, seq_make(&inits, p->arena), body, p->arena);
}

static stmt_ty p_exception_def(Parser *p) {
    size_t start = p->pos;
    identifier name, base = NULL;
    if (!accept(p, "exception")) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name) {
        p->pos = start;
        return NULL;
    }
    if (accept(p, "extends")) {
        base = p_name(p);
        if (!base) {
            p->pos = start;
            return NULL;
        }
    }
    if (!accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ExceptionDef(name, base, ATTRS(p, start), p->arena);
}

static struct_member_ty p_struct_member(Parser *p) {
    struct_member_ty m = p_struct_constructor(p);
    if (m)
        return m;
    return p_struct_field(p);
}

static asdl_seq *p_struct_body(Parser *p) {
    size_t start = p->pos;
    Seq members = {0};
    struct_member_ty m;
    if (!accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    while ((m = p_struct_member(p)) != NULL)
        seq_push(&members, m);
    if (!accept(p, "}")) {
        seq_clear(&members);
        p->pos = start;
        return NULL;
    }
    return seq_make(&members, p->arena);
}

static stmt_ty p_struct_def(Parser *p) {
    size_t start = p->pos;
    identifier tag;
    NameSet ns = {0};
    asdl_seq *members;
    if (!accept(p, "struct")) {
        p->pos = start;
        return NULL;
    }
    tag = p_name(p);
    if (!tag) {
        p->pos = start;
        return NULL;
    }
    ns_add(&ns, tag);
    push_struct_names(p, ns);
    members = p_struct_body(p);
    pop_struct_names(p);
    if (!members || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_StructDef(tag, NULL, members, ATTRS(p, start), p->arena);
}

static stmt_ty p_typedef_struct(Parser *p) {
    size_t start = p->pos;
    size_t tag_index;
    identifier tag, alias = NULL;
    NameSet ns = {0};
    asdl_seq *members;
    size_t i;

    if (!(accept(p, "typedef") && accept(p, "struct"))) {
        p->pos = start;
        return NULL;
    }
    tag_index = p->pos;
    tag = p_name(p);
    if (!tag) {
        p->pos = start;
        return NULL;
    }
    ns_add(&ns, tag);
    for (i = 0; i < p->naliases; i++) {
        if (p->aliases[i].tag_index == tag_index) {
            alias = p->aliases[i].alias;
            ns_add(&ns, alias);
        }
    }
    push_struct_names(p, ns);
    members = p_struct_body(p);
    pop_struct_names(p);
    if (!members) {
        p->pos = start;
        return NULL;
    }
    alias = p_name(p);
    if (!alias || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    return _Aster_StructDef(tag, alias, members, ATTRS(p, start), p->arena);
}

/* --------------------------------------------------------------- 제어문 */

static stmt_ty p_if_stmt(Parser *p);
static asdl_seq *p_else_body(Parser *p);

static stmt_ty p_if_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty test;
    asdl_seq *body, *orelse;
    if (!(accept(p, "if") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    test = p_expression(p);
    if (!test || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    orelse = empty_seq(p);
    {
        size_t mark = p->pos;
        if (accept(p, "else")) {
            asdl_seq *eb = p_else_body(p);
            if (!eb)
                p->pos = mark;
            else
                orelse = eb;
        }
    }
    return _Aster_If(test, body, orelse, ATTRS(p, start), p->arena);
}

static asdl_seq *p_else_body(Parser *p) {
    size_t start = p->pos;
    asdl_seq *body = p_block(p);
    if (body)
        return body;
    p->pos = start;
    {
        stmt_ty nested = p_if_stmt(p);
        Seq s = {0};
        if (!nested) {
            p->pos = start;
            return NULL;
        }
        seq_push(&s, nested);
        return seq_make(&s, p->arena);
    }
}

static stmt_ty p_while_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty test;
    asdl_seq *body;
    if (!(accept(p, "while") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    test = p_expression(p);
    if (!test || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_While(test, body, ATTRS(p, start), p->arena);
}

static range_expr_ty p_range_expr(Parser *p) {
    size_t start = p->pos;
    expr_ty lo = p_arith(p);
    expr_ty hi, step = NULL;
    if (!lo || !accept(p, "..")) {
        p->pos = start;
        return NULL;
    }
    hi = p_arith(p);
    if (!hi) {
        p->pos = start;
        return NULL;
    }
    {
        size_t mark = p->pos;
        if (accept(p, "..")) {
            step = p_arith(p);
            if (!step)
                p->pos = mark;
        }
    }
    return _Aster_range_expr(lo, hi, step, p->arena);
}

static stmt_ty p_range_for_stmt(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty;
    identifier name, target;
    size_t target_index;
    range_expr_ty iter;
    asdl_seq *body;
    if (!(accept(p, "for") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    ty = p_type(p);
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    target_index = p->pos;
    target = p_name(p);
    if (!target || !accept(p, "=")) {
        p->pos = start;
        return NULL;
    }
    iter = p_range_expr(p);
    if (!iter || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    if (strcmp(name, target) != 0)
        syntax_error_at(p, target_index,
                        "range for variable names must match: declared '%s' but assigned '%s'",
                        name, target);
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ForRange(ty, name, iter, body, ATTRS(p, start), p->arena);
}

static stmt_ty p_for_init(Parser *p) {
    stmt_ty s = p_var_decl_core(p, 0);
    if (s)
        return s;
    return p_assign_core(p);
}

static stmt_ty p_for_step(Parser *p) {
    size_t start = p->pos;
    stmt_ty s = p_assign_core(p);
    expr_ty e;
    if (s)
        return s;
    e = p_expression(p);
    if (!e) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ExprStmt(e, ATTRS(p, start), p->arena);
}

static stmt_ty p_for_in_stmt(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty;
    identifier name;
    expr_ty iter;
    asdl_seq *body;
    if (!(accept(p, "for") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    ty = p_type(p);
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, "in")) {
        p->pos = start;
        return NULL;
    }
    iter = p_expression(p);
    if (!iter || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_ForIn(ty, name, iter, body, ATTRS(p, start), p->arena);
}

static stmt_ty p_c_for_stmt(Parser *p) {
    size_t start = p->pos;
    stmt_ty init, step;
    expr_ty test;
    asdl_seq *body;
    if (!(accept(p, "for") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    init = p_for_init(p);
    if (!init || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    test = p_expression(p);
    if (!test || !accept(p, ";")) {
        p->pos = start;
        return NULL;
    }
    step = p_for_step(p);
    if (!step || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_For(init, test, step, body, ATTRS(p, start), p->arena);
}

static stmt_ty p_for_stmt(Parser *p) {
    stmt_ty s = p_range_for_stmt(p);
    if (s)
        return s;
    s = p_for_in_stmt(p);
    if (s)
        return s;
    return p_c_for_stmt(p);
}

static match_pattern_ty p_match_pattern(Parser *p) {
    size_t start = p->pos;
    AsterToken *t = cur(p);

    if (t->type == TOK_INT) {
        int64_t v = token_int(p, start);
        p->pos++;
        return _Aster_PLiteral(_Aster_Constant(Value_Int(v), ATTRS(p, start), p->arena),
                               p->arena);
    }
    if (t->type == TOK_FLOAT) {
        double v = token_float(p, start);
        p->pos++;
        return _Aster_PLiteral(_Aster_Constant(Value_Float(v), ATTRS(p, start), p->arena),
                               p->arena);
    }
    if (t->type == TOK_STRING) {
        int len = 0;
        AsterError e;
        char *decoded = Aster_DecodeString(t->start, t->length, &len, &e);
        Value v;
        if (!decoded)
            syntax_error_at(p, start, "invalid string literal: %s", e.message);
        v = Value_Obj((Object *)AsterString_FromUTF8(decoded, (size_t)len));
        free(decoded);
        p->pos++;
        return _Aster_PLiteral(_Aster_Constant(v, ATTRS(p, start), p->arena), p->arena);
    }
    if (tok_eq(t, "true") || tok_eq(t, "false") || tok_eq(t, "null")) {
        Value v = tok_eq(t, "true") ? Value_Bool(1)
                : tok_eq(t, "false") ? Value_Bool(0) : Value_Null();
        p->pos++;
        return _Aster_PLiteral(_Aster_Constant(v, ATTRS(p, start), p->arena), p->arena);
    }
    if (tok_eq(t, "-") && tok_at(p, start + 1)->type == TOK_INT) {
        int64_t v;
        p->pos++;
        v = token_int(p, p->pos);
        p->pos++;
        return _Aster_PLiteral(_Aster_Constant(Value_Int(-v), ATTRS(p, start), p->arena),
                               p->arena);
    }
    if (t->type == TOK_NAME && !is_hard_token(t)) {
        identifier first = intern_token(p, t);
        expr_ty path;
        int dotted = 0;
        p->pos++;
        path = _Aster_Name(first, ATTRS(p, start), p->arena);
        while (accept(p, ".")) {
            identifier part = p_name(p);
            if (!part) {
                p->pos = start;
                return NULL;
            }
            path = _Aster_Attribute(path, part, ATTRS(p, start), p->arena);
            dotted = 1;
        }
        if (accept(p, "(")) {
            Seq items = {0};
            if (!at(p, ")")) {
                for (;;) {
                    match_pattern_ty item = p_match_pattern(p);
                    if (!item) {
                        seq_clear(&items);
                        p->pos = start;
                        return NULL;
                    }
                    seq_push(&items, item);
                    if (!accept(p, ","))
                        break;
                }
            }
            if (!accept(p, ")")) {
                seq_clear(&items);
                p->pos = start;
                return NULL;
            }
            return _Aster_PVariant(path, seq_make(&items, p->arena), p->arena);
        }
        if (!dotted)
            return _Aster_PBind(first, p->arena);
        return _Aster_PVariant(path, empty_seq(p), p->arena);
    }
    expect_text(p, "a literal, name, or Type.Variant(...) pattern");
    p->pos = start;
    return NULL;
}

static stmt_ty p_switch_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty subject;
    Seq cases = {0};
    int have_default = 0;

    if (!(accept(p, "switch") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    subject = p_expression(p);
    if (!subject || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    if (!accept(p, "{")) {
        p->pos = start;
        return NULL;
    }
    for (;;) {
        size_t mark = p->pos;
        if (accept(p, "case")) {
            match_pattern_ty pat;
            asdl_seq *body;
            if (have_default)
                syntax_error_at(p, mark, "case cannot follow default");
            pat = p_match_pattern(p);
            if (!pat) {
                p->pos = mark;
                goto fail;
            }
            body = p_block(p);
            if (!body) {
                p->pos = mark;
                goto fail;
            }
            seq_push(&cases, _Aster_match_case(pat, body, p->arena));
            continue;
        }
        if (accept(p, "default")) {
            asdl_seq *body;
            if (have_default)
                syntax_error_at(p, mark, "duplicate default case");
            have_default = 1;
            body = p_block(p);
            if (!body) {
                p->pos = mark;
                goto fail;
            }
            seq_push(&cases, _Aster_match_case(_Aster_PBind("_", p->arena), body,
                                               p->arena));
            continue;
        }
        break;
    }
    if (!accept(p, "}"))
        goto fail;
    return _Aster_Switch(subject, seq_make(&cases, p->arena), ATTRS(p, start), p->arena);
fail:
    seq_clear(&cases);
    p->pos = start;
    return NULL;
}

static handler_ty p_catch_clause(Parser *p) {
    size_t start = p->pos;
    typeref_ty ty;
    identifier name;
    asdl_seq *body;
    if (!(accept(p, "catch") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    ty = p_type(p);
    if (!ty) {
        p->pos = start;
        return NULL;
    }
    name = p_name(p);
    if (!name || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_handler(ty, name, body, p->arena);
}

static stmt_ty p_try_stmt(Parser *p) {
    size_t start = p->pos;
    asdl_seq *body;
    Seq handlers = {0};
    asdl_seq *finalbody;
    handler_ty h;
    if (!accept(p, "try")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    while ((h = p_catch_clause(p)) != NULL)
        seq_push(&handlers, h);

    if (handlers.count > 0) {
        finalbody = empty_seq(p);
        {
            size_t mark = p->pos;
            if (accept(p, "finally")) {
                asdl_seq *final = p_block(p);
                if (!final)
                    p->pos = mark;
                else
                    finalbody = final;
            }
        }
        return _Aster_Try(body, seq_make(&handlers, p->arena), finalbody,
                          ATTRS(p, start), p->arena);
    }

    if (!accept(p, "finally")) {
        seq_clear(&handlers);
        p->pos = start;
        return NULL;
    }
    {
        asdl_seq *final = p_block(p);
        seq_clear(&handlers);
        if (!final) {
            p->pos = start;
            return NULL;
        }
        return _Aster_Try(body, empty_seq(p), final, ATTRS(p, start), p->arena);
    }
}

/* ---------------------------------------------------------------- 문장 */

static stmt_ty p_simple_stmt(Parser *p) {
    stmt_ty s = p_import_stmt(p);
    if (s)
        return s;
    s = p_assert_stmt(p);
    if (s)
        return s;
    s = p_return_stmt(p);
    if (s)
        return s;
    s = p_yield_stmt(p);
    if (s)
        return s;
    s = p_throw_stmt(p);
    if (s)
        return s;
    s = p_break_stmt(p);
    if (s)
        return s;
    s = p_continue_stmt(p);
    if (s)
        return s;
    s = p_var_decl(p);
    if (s)
        return s;
    s = p_assign_stmt(p);
    if (s)
        return s;
    return p_expr_stmt(p);
}

static stmt_ty p_export_decl(Parser *p) {
    size_t start = p->pos;
    stmt_ty inner;
    if (!accept(p, "export")) {
        p->pos = start;
        return NULL;
    }
    inner = p_extern_def(p);
    if (!inner)
        inner = p_function_def(p);
    if (!inner)
        inner = p_class_def(p);
    if (!inner)
        inner = p_interface_def(p);
    if (!inner)
        inner = p_exception_def(p);
    if (!inner)
        inner = p_struct_def(p);
    if (!inner)
        inner = p_typedef_struct(p);
    if (!inner)
        inner = p_var_decl(p);
    if (!inner) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Export(inner, ATTRS(p, start), p->arena);
}

static stmt_ty p_compound_stmt(Parser *p) {
    stmt_ty s = p_export_decl(p);
    if (s)
        return s;
    s = p_extern_def(p);
    if (s)
        return s;
    s = p_enum_def(p);
    if (s)
        return s;
    s = p_function_def(p);
    if (s)
        return s;
    s = p_class_def(p);
    if (s)
        return s;
    s = p_interface_def(p);
    if (s)
        return s;
    s = p_exception_def(p);
    if (s)
        return s;
    s = p_struct_def(p);
    if (s)
        return s;
    s = p_typedef_struct(p);
    if (s)
        return s;
    s = p_if_stmt(p);
    if (s)
        return s;
    s = p_while_stmt(p);
    if (s)
        return s;
    s = p_for_stmt(p);
    if (s)
        return s;
    s = p_switch_stmt(p);
    if (s)
        return s;
    return p_try_stmt(p);
}

static stmt_ty p_statement(Parser *p) {
    stmt_ty s = p_compound_stmt(p);
    if (s)
        return s;
    return p_simple_stmt(p);
}

/* ------------------------------------------------------------ 파일과 오류 */

static mod_ty p_file(Parser *p) {
    size_t start = p->pos;
    asdl_seq *package = p_package_stmt(p);
    Seq body = {0};
    stmt_ty s;
    while ((s = p_statement(p)) != NULL)
        seq_push(&body, s);
    if (cur(p)->type != TOK_ENDMARKER) {
        expect_text(p, "'end of file'");
        seq_clear(&body);
        p->pos = start;
        return NULL;
    }
    if (!package)
        package = empty_seq(p);
    return _Aster_Module(package, seq_make(&body, p->arena), p->arena);
}

static int expected_has(Parser *p, const char *text) {
    int i;
    for (i = 0; i < p->nexp; i++)
        if (strcmp(p->expected[i], text) == 0)
            return 1;
    return 0;
}

static void generic_error(Parser *p) {
    size_t idx = p->farthest < p->n ? p->farthest : p->n - 1;
    AsterToken *t = tok_at(p, idx);

    if (idx > 0 && expected_has(p, "';'") && t->line > tok_at(p, idx - 1)->end_line) {
        AsterToken *prev = tok_at(p, idx - 1);
        p->err->line = prev->end_line;
        p->err->col = prev->end_col;
        p->err->length = 1;
        snprintf(p->err->message, sizeof p->err->message,
                 "syntax error: missing ';' at end of line");
        return;
    }

    {
        static const char *const prio[] = {"')'", "'}'", "';'", "','"};
        char shown[512] = "";
        int count = 0;
        int i;
        int width = t->end_line == t->line ? t->end_col - t->col : 1;

        for (i = 0; i < (int)(sizeof prio / sizeof *prio) && count < 8; i++) {
            if (expected_has(p, prio[i])) {
                if (count)
                    strncat(shown, ", ", sizeof shown - strlen(shown) - 1);
                strncat(shown, prio[i], sizeof shown - strlen(shown) - 1);
                count++;
            }
        }
        for (i = 0; i < p->nexp && count < 8; i++) {
            int j, dup = 0;
            for (j = 0; j < (int)(sizeof prio / sizeof *prio); j++)
                if (strcmp(p->expected[i], prio[j]) == 0)
                    dup = 1;
            if (dup)
                continue;
            if (count)
                strncat(shown, ", ", sizeof shown - strlen(shown) - 1);
            strncat(shown, p->expected[i], sizeof shown - strlen(shown) - 1);
            count++;
        }

        p->err->line = t->line;
        p->err->col = t->col;
        p->err->length = width > 0 ? width : 1;
        if (t->type == TOK_ENDMARKER) {
            snprintf(p->err->message, sizeof p->err->message,
                     "syntax error: unexpected end of file");
        }
        else {
            snprintf(p->err->message, sizeof p->err->message,
                     "syntax error: unexpected '%.*s'", t->length, t->start);
        }
        if (count > 0) {
            size_t used = strlen(p->err->message);
            snprintf(p->err->message + used, sizeof p->err->message - used,
                     " (%s expected)", shown);
        }
    }
}

static void parser_free(Parser *p) {
    size_t i;
    Aster_TokenList_Clear(&p->list);
    free(p->type_names.items);
    free(p->struct_type_names.items);
    for (i = 0; i < p->n_type_params; i++)
        free(p->type_param_stack[i].items);
    free(p->type_param_stack);
    for (i = 0; i < p->n_structs; i++)
        free(p->struct_stack[i].items);
    free(p->struct_stack);
    free(p);
}

mod_ty Aster_ParseFile(const char *source, size_t length, Arena *arena, AsterError *err) {
    Parser *p = xrealloc(NULL, sizeof *p);
    mod_ty mod;

    memset(p, 0, sizeof *p);
    memset(err, 0, sizeof *err);
    p->arena = arena;
    p->err = err;

    if (!Aster_Tokenize(source, length, &p->list, err)) {
        parser_free(p);
        return NULL;
    }
    p->n = p->list.count;
    prescan(p);

    if (setjmp(p->jmp)) {
        parser_free(p);
        return NULL;
    }

    mod = p_file(p);
    if (!mod) {
        generic_error(p);
        parser_free(p);
        return NULL;
    }
    parser_free(p);
    return mod;
}

expr_ty Aster_ParseExpression(const char *source, size_t length, Arena *arena,
                              AsterError *err) {
    Parser *p = xrealloc(NULL, sizeof *p);
    expr_ty e;

    memset(p, 0, sizeof *p);
    memset(err, 0, sizeof *err);
    p->arena = arena;
    p->err = err;

    if (!Aster_Tokenize(source, length, &p->list, err)) {
        parser_free(p);
        return NULL;
    }
    p->n = p->list.count;
    prescan(p);

    if (setjmp(p->jmp)) {
        parser_free(p);
        return NULL;
    }

    e = p_expression(p);
    if (!e || cur(p)->type != TOK_ENDMARKER) {
        if (!e) {
            generic_error(p);
        }
        else {
            AsterToken *t = cur(p);
            err->line = t->line;
            err->col = t->col;
            err->length = t->end_line == t->line ? t->end_col - t->col : 1;
            snprintf(err->message, sizeof err->message, "syntax error: unexpected '%.*s'",
                     t->length, t->start);
        }
        parser_free(p);
        return NULL;
    }
    parser_free(p);
    return e;
}
