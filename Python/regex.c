/*
 * Python 호환 정규식 엔진.
 *
 * 패턴을 바이트코드로 컴파일한 뒤 백트래킹 VM으로 실행한다.
 * 문자열은 코드 포인트 단위로 다루고, 캡처 슬롯은 2*그룹+1 배열에 기록한다.
 * lookahead/lookbehind/atomic 그룹은 같은 프로그램을 중첩 실행해 구현한다.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/regex.h"
#include "internal/unicode_ranges.h"

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* ---- 플래그 (파이썬 re 상수와 같은 값) ---- */

enum {
    F_IGNORECASE = 2,
    F_MULTILINE = 8,
    F_DOTALL = 16,
    F_UNICODE = 32,
    F_VERBOSE = 64,
    F_ASCII = 256
};

#define F_ALL (F_IGNORECASE | F_MULTILINE | F_DOTALL | F_UNICODE | F_VERBOSE | F_ASCII)

/* ---- 명령어 ---- */

enum {
    OP_CHAR = 1,
    OP_ANY,
    OP_CLASS,
    OP_AT,
    OP_SPLIT,
    OP_JMP,
    OP_FAIL,
    OP_SAVE,
    OP_MATCH,
    OP_ASSERT,     /* a=본문 시작, b=계속, c=lookbehind 폭(-1이면 ahead), d=부정 */
    OP_ATOMIC,     /* a=본문 시작, b=계속 */
    OP_BACKREF,    /* a=그룹 번호 */
    OP_COND,       /* a=그룹, b=참일 때, c=거짓일 때 */
    OP_PSET,       /* a=진행 슬롯: 현재 위치 저장 */
    OP_PCHK        /* a=진행 슬롯, b=위치가 그대로면 이동 (빈 반복 탈출) */
};

enum { AT_BOL = 1, AT_EOL, AT_BOS, AT_EOS, AT_BOUND, AT_NBOUND };

typedef enum {
    CIT_RANGE,
    CIT_DIGIT,
    CIT_WORD,
    CIT_SPACE
} CItemKind;

typedef struct {
    int kind;
    int negate;
    uint32_t lo, hi;
} RClassItem;

typedef struct {
    RClassItem *items;
    size_t n, cap;
    int negate;
} RClass;

typedef struct {
    int op;
    int a, b, c, d;
    uint32_t cp;
    int flags;
    RClass *cls;
} Inst;

typedef struct {
    char name[64];
    int index;
} GroupName;

struct AsterRegex {
    Inst *prog;
    size_t nprog;
    int ngroups;
    int flags;
    char *pattern;
    size_t plen;
    GroupName *names;
    int nnames;
    int nscratch;
    size_t nslots;
};

struct RMatch {
    AsterRegex *re;
    AsterString *subject;
    size_t *slots;
};

/* ---- 컴파일러 상태 ---- */

typedef struct {
    long min, max;   /* max < 0 이면 무한/가변 */
} Width;

typedef struct {
    int has_name;
    int group;
    char name[64];
    size_t pos;
    size_t inst;     /* OP_COND 명령 위치 (이름 조건은 나중에 인덱스를 채운다) */
} CondRef;

typedef struct {
    Interp *in;
    uint32_t *pat;
    size_t n, i;
    Inst *prog;
    size_t len, cap;
    int flags;
    int ngroups;
    int nscratch;
    GroupName names[128];
    int nnames;
    size_t global_prefix_end;
    char err[512];
    int failed;
    int depth;
    CondRef conds[64];
    int nconds;
} P;

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("out of memory\n", stderr);
        abort();
    }
    return q;
}

static void *xmalloc(size_t n) {
    return xrealloc(NULL, n);
}

static char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    if (n)
        memcpy(p, s, n);
    p[n] = 0;
    return p;
}

static uint32_t *decode_cps(const char *s, size_t n, size_t *out_n) {
    uint32_t *cps = xmalloc((n + 1) * sizeof(uint32_t));
    size_t i = 0, k = 0;
    while (i < n) {
        size_t adv;
        cps[k++] = Aster_UTF8_Decode(s + i, n - i, &adv);
        i += adv;
    }
    *out_n = k;
    return cps;
}

/* ---- 유니코드 분류 (파이썬 str.isalnum/isdecimal과 같은 표) ---- */

static int cp_in_ranges(const AsterCPRange *r, size_t n, uint32_t cp) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < r[mid].lo)
            hi = mid;
        else if (cp > r[mid].hi)
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

static int is_digit_cp(uint32_t cp, int ascii) {
    if (cp >= '0' && cp <= '9')
        return 1;
    if (ascii || cp < 0x80 || cp > 0x10FFFF)
        return 0;
    return cp_in_ranges(_AsterDigitRanges,
                        sizeof _AsterDigitRanges / sizeof _AsterDigitRanges[0], cp);
}

static int is_word_cp(uint32_t cp, int ascii) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_')
        return 1;
    if (is_digit_cp(cp, ascii))
        return 1;
    if (ascii || cp < 0x80 || cp > 0x10FFFF)
        return 0;
    return cp_in_ranges(_AsterWordRanges,
                        sizeof _AsterWordRanges / sizeof _AsterWordRanges[0], cp);
}

static int is_space_cp(uint32_t cp, int ascii) {
    if (ascii)
        return cp == ' ' || (cp >= 0x09 && cp <= 0x0D);
    return Aster_IsSpaceCP(cp);
}

static int cp_eq(uint32_t a, uint32_t b, int flags) {
    if (a == b)
        return 1;
    if (!(flags & F_IGNORECASE))
        return 0;
    if ((flags & F_ASCII) && (a >= 0x80 || b >= 0x80))
        return 0;
    return Aster_ToLowerCP(a) == Aster_ToLowerCP(b) ||
           Aster_ToUpperCP(a) == Aster_ToUpperCP(b);
}

/* ---- 클래스 ---- */

static RClass *class_new(void) {
    RClass *c = xmalloc(sizeof(RClass));
    memset(c, 0, sizeof(*c));
    return c;
}

static void class_add(RClass *c, int kind, int negate, uint32_t lo, uint32_t hi) {
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->items = xrealloc(c->items, c->cap * sizeof(RClassItem));
    }
    c->items[c->n].kind = kind;
    c->items[c->n].negate = negate;
    c->items[c->n].lo = lo;
    c->items[c->n].hi = hi;
    c->n++;
}

static int class_item_hit(const RClassItem *it, uint32_t cp, int flags) {
    int ascii = (flags & F_ASCII) != 0;
    int hit = 0;
    switch (it->kind) {
    case CIT_RANGE:
        hit = cp >= it->lo && cp <= it->hi;
        if (!hit && (flags & F_IGNORECASE)) {
            uint32_t lo = Aster_ToLowerCP(cp), up = Aster_ToUpperCP(cp);
            if ((flags & F_ASCII) && (cp >= 0x80))
                return 0;
            hit = (lo >= it->lo && lo <= it->hi) || (up >= it->lo && up <= it->hi);
        }
        break;
    case CIT_DIGIT:
        hit = is_digit_cp(cp, ascii);
        break;
    case CIT_WORD:
        hit = is_word_cp(cp, ascii);
        break;
    case CIT_SPACE:
        hit = is_space_cp(cp, ascii);
        break;
    }
    return it->negate ? !hit : hit;
}

static int class_match(const RClass *c, uint32_t cp, int flags) {
    size_t i;
    int hit = 0;
    for (i = 0; i < c->n; i++) {
        if (class_item_hit(&c->items[i], cp, flags)) {
            hit = 1;
            break;
        }
    }
    return c->negate ? !hit : hit;
}

/* ---- 컴파일 보조 ---- */

static Inst inst_new(int op) {
    Inst x;
    memset(&x, 0, sizeof(x));
    x.op = op;
    x.a = x.b = x.c = x.d = -1;
    return x;
}

static size_t emit(P *p, Inst x) {
    if (p->len == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->prog = xrealloc(p->prog, p->cap * sizeof(Inst));
    }
    p->prog[p->len] = x;
    return p->len++;
}

static void perr_nopos(P *p, const char *fmt, ...) {
    va_list ap;
    if (p->failed)
        return;
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    p->failed = 1;
}

static void perr(P *p, size_t pos, const char *fmt, ...) {
    char msg[400];
    va_list ap;
    if (p->failed)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    snprintf(p->err, sizeof p->err, "%s at position %zu", msg, pos);
    p->failed = 1;
}

/* 원본 패턴의 [from, to) 구간을 오류 메시지용 UTF-8 문자열로 만든다. */
static void pattern_slice(P *p, size_t from, size_t to, char *out, size_t cap) {
    size_t i, o = 0;
    if (from > p->n)
        from = p->n;
    if (to > p->n)
        to = p->n;
    for (i = from; i < to && o + 4 < cap; i++) {
        char buf[4];
        size_t k = Aster_UTF8_Encode(p->pat[i], buf);
        memcpy(out + o, buf, k);
        o += k;
    }
    out[o] = 0;
}

static void skip_ignored(P *p) {
    if (!(p->flags & F_VERBOSE))
        return;
    for (;;) {
        if (p->i >= p->n)
            return;
        if (is_space_cp(p->pat[p->i], 0)) {
            p->i++;
            continue;
        }
        if (p->pat[p->i] == '#') {
            while (p->i < p->n && p->pat[p->i] != '\n')
                p->i++;
            continue;
        }
        return;
    }
}

/* '{m}', '{m,}', '{m,n}' 형태인지 미리 본다. */
static int brace_looks_quant(P *p, size_t at) {
    size_t i = at + 1;
    if (i >= p->n || p->pat[i] < '0' || p->pat[i] > '9')
        return 0;
    while (i < p->n && p->pat[i] >= '0' && p->pat[i] <= '9')
        i++;
    if (i >= p->n)
        return 0;
    if (p->pat[i] == '}')
        return 1;
    if (p->pat[i] != ',')
        return 0;
    i++;
    if (i < p->n && p->pat[i] == '}')
        return 1;
    while (i < p->n && p->pat[i] >= '0' && p->pat[i] <= '9')
        i++;
    return i < p->n && p->pat[i] == '}';
}

static void w_add(Width *a, Width b) {
    a->min += b.min;
    if (a->max < 0 || b.max < 0)
        a->max = -1;
    else
        a->max += b.max;
}

static void w_merge(Width *a, Width b) {
    if (b.min < a->min)
        a->min = b.min;
    if (a->max < 0 || b.max < 0)
        a->max = -1;
    else if (b.max > a->max)
        a->max = b.max;
}

static void append_body(P *p, const Inst *body, size_t blen, int delta) {
    size_t k;
    for (k = 0; k < blen; k++) {
        Inst x = body[k];
        switch (x.op) {
        case OP_SPLIT:
            if (x.a >= 0) x.a += delta;
            if (x.b >= 0) x.b += delta;
            break;
        case OP_JMP:
        case OP_FAIL:
            if (x.op == OP_JMP && x.a >= 0) x.a += delta;
            break;
        case OP_ASSERT:
        case OP_ATOMIC:
            if (x.a >= 0) x.a += delta;
            if (x.b >= 0) x.b += delta;
            break;
        case OP_COND:
            if (x.b >= 0) x.b += delta;
            if (x.c >= 0) x.c += delta;
            break;
        case OP_PCHK:
            if (x.b >= 0) x.b += delta;
            break;
        default:
            break;
        }
        emit(p, x);
    }
}

static Width parse_alt(P *p);
static Width parse_seq(P *p);
static Width parse_quant(P *p);
static Width parse_atom(P *p, int *repeatable);
static Width parse_escape_atom(P *p, int *repeatable);

static void emit_quant(P *p, Inst *body, size_t blen, long min, long max,
                       int greedy, int possessive) {
    long k;

    if (max == 0)
        return;
    if (possessive) {
        Inst *saved_prog = p->prog;
        size_t saved_len = p->len, saved_cap = p->cap;
        Inst *tmp;
        size_t tlen;
        Inst x;
        size_t at;

        p->prog = NULL;
        p->len = 0;
        p->cap = 0;
        emit_quant(p, body, blen, min, max, greedy, 0);
        tmp = p->prog;
        tlen = p->len;
        p->prog = saved_prog;
        p->len = saved_len;
        p->cap = saved_cap;

        x = inst_new(OP_ATOMIC);
        x.a = (int)(p->len + 1);
        at = emit(p, x);
        append_body(p, tmp, tlen, (int)p->len);
        p->prog[at].b = (int)p->len;
        free(tmp);
        return;
    }

    for (k = 0; k < min; k++)
        append_body(p, body, blen, (int)p->len);

    if (max < 0) {
        int slot = -(++p->nscratch);
        size_t l1 = p->len;
        Inst sp = inst_new(OP_SPLIT);
        size_t spi, body_start, pci, out;
        Inst ps, pc, j;

        sp.a = (int)(l1 + 1);
        spi = emit(p, sp);
        body_start = p->len;

        ps = inst_new(OP_PSET);
        ps.a = slot;
        emit(p, ps);

        append_body(p, body, blen, (int)p->len);

        pc = inst_new(OP_PCHK);
        pc.a = slot;
        pci = emit(p, pc);

        j = inst_new(OP_JMP);
        j.a = (int)l1;
        emit(p, j);

        out = p->len;
        p->prog[spi].a = greedy ? (int)body_start : (int)out;
        p->prog[spi].b = greedy ? (int)out : (int)body_start;
        p->prog[pci].b = (int)out;
    }
    else {
        size_t *exits = NULL;
        size_t ne = 0, cap = 0, out;
        size_t kk;

        for (k = min; k < max; k++) {
            Inst sp = inst_new(OP_SPLIT);
            size_t spi = emit(p, sp);
            size_t body_start = p->len;

            append_body(p, body, blen, (int)p->len);
            if (ne == cap) {
                cap = cap ? cap * 2 : 8;
                exits = xrealloc(exits, cap * sizeof(size_t));
            }
            exits[ne++] = spi;
            p->prog[spi].a = greedy ? (int)body_start : -1;
            p->prog[spi].b = greedy ? -1 : (int)body_start;
        }
        out = p->len;
        for (kk = 0; kk < ne; kk++) {
            if (greedy)
                p->prog[exits[kk]].b = (int)out;
            else
                p->prog[exits[kk]].a = (int)out;
        }
        free(exits);
    }
}

/* 한 분기를 임시 버퍼에 컴파일한 뒤, 분기 수에 따라 SPLIT/FAIL을 붙여 합친다. */
static Width parse_alt(P *p) {
    Inst **branches = NULL;
    size_t *blens = NULL;
    Width *bws = NULL;
    size_t nb = 0, cap = 0;
    Inst *saved_prog = p->prog;
    size_t saved_len = p->len, saved_cap = p->cap;
    Width total = {0, 0};
    int first = 1;
    size_t i, k;

    for (;;) {
        Width w;
        p->prog = NULL;
        p->len = 0;
        p->cap = 0;
        w = parse_seq(p);
        if (nb == cap) {
            cap = cap ? cap * 2 : 4;
            branches = xrealloc(branches, cap * sizeof(Inst *));
            blens = xrealloc(blens, cap * sizeof(size_t));
            bws = xrealloc(bws, cap * sizeof(Width));
        }
        branches[nb] = p->prog;
        blens[nb] = p->len;
        bws[nb] = w;
        nb++;
        if (first) {
            total = w;
            first = 0;
        }
        else {
            w_merge(&total, w);
        }
        if (p->failed)
            break;
        if (p->i < p->n && p->pat[p->i] == '|') {
            p->i++;
            continue;
        }
        break;
    }

    p->prog = saved_prog;
    p->len = saved_len;
    p->cap = saved_cap;

    if (!p->failed) {
        if (nb == 1) {
            append_body(p, branches[0], blens[0], (int)p->len);
        }
        else {
            size_t *splits = xmalloc(nb * sizeof(size_t));
            size_t *jmps = xmalloc(nb * sizeof(size_t));
            size_t failpc, end;

            for (i = 0; i < nb; i++) {
                Inst sp = inst_new(OP_SPLIT);
                sp.a = (int)(p->len + 1);
                splits[i] = emit(p, sp);
                append_body(p, branches[i], blens[i], (int)p->len);
                jmps[i] = emit(p, inst_new(OP_JMP));
            }
            failpc = emit(p, inst_new(OP_FAIL));
            end = p->len;
            for (i = 0; i < nb; i++)
                p->prog[splits[i]].b = (i + 1 < nb) ? (int)splits[i + 1] : (int)failpc;
            for (k = 0; k < nb; k++)
                p->prog[jmps[k]].a = (int)end;
            free(splits);
            free(jmps);
        }
    }

    for (i = 0; i < nb; i++)
        free(branches[i]);
    free(branches);
    free(blens);
    free(bws);
    return total;
}

static Width parse_seq(P *p) {
    Width total = {0, 0};
    for (;;) {
        Width w;
        skip_ignored(p);
        if (p->failed || p->i >= p->n)
            break;
        if (p->pat[p->i] == ')' || p->pat[p->i] == '|')
            break;
        w = parse_quant(p);
        w_add(&total, w);
    }
    return total;
}

/* ---- 이스케이프 ---- */

static int hex_digit(uint32_t c) {
    if (c >= '0' && c <= '9')
        return (int)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A' + 10);
    return -1;
}

static int read_hex(P *p, int count, uint32_t *out) {
    uint32_t v = 0;
    int k;
    for (k = 0; k < count; k++) {
        int h;
        if (p->i >= p->n)
            return 0;
        h = hex_digit(p->pat[p->i]);
        if (h < 0)
            return 0;
        v = v * 16 + (uint32_t)h;
        p->i++;
    }
    *out = v;
    return 1;
}

static void emit_literal(P *p, uint32_t cp) {
    Inst x = inst_new(OP_CHAR);
    x.cp = cp;
    x.flags = p->flags;
    emit(p, x);
}

static void emit_class_atom(P *p, int kind, int negate) {
    RClass *c = class_new();
    Inst x = inst_new(OP_CLASS);
    class_add(c, kind, negate, 0, 0);
    x.cls = c;
    x.flags = p->flags;
    emit(p, x);
}

static int ascii_letter(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static Width parse_escape_atom(P *p, int *repeatable) {
    size_t start = p->i;
    uint32_t c;
    Width w = {1, 1};

    p->i++;
    if (p->i >= p->n) {
        perr(p, start, "bad escape (end of pattern)");
        return w;
    }
    c = p->pat[p->i++];
    switch (c) {
    case 'A':
        emit(p, inst_new(OP_AT));
        p->prog[p->len - 1].a = AT_BOS;
        *repeatable = 0;
        return (Width){0, 0};
    case 'Z':
    case 'z':
        emit(p, inst_new(OP_AT));
        p->prog[p->len - 1].a = AT_EOS;
        *repeatable = 0;
        return (Width){0, 0};
    case 'b':
        emit(p, inst_new(OP_AT));
        p->prog[p->len - 1].a = AT_BOUND;
        p->prog[p->len - 1].flags = p->flags;
        *repeatable = 0;
        return (Width){0, 0};
    case 'B':
        emit(p, inst_new(OP_AT));
        p->prog[p->len - 1].a = AT_NBOUND;
        p->prog[p->len - 1].flags = p->flags;
        *repeatable = 0;
        return (Width){0, 0};
    case 'd': emit_class_atom(p, CIT_DIGIT, 0); return w;
    case 'D': emit_class_atom(p, CIT_DIGIT, 1); return w;
    case 'w': emit_class_atom(p, CIT_WORD, 0); return w;
    case 'W': emit_class_atom(p, CIT_WORD, 1); return w;
    case 's': emit_class_atom(p, CIT_SPACE, 0); return w;
    case 'S': emit_class_atom(p, CIT_SPACE, 1); return w;
    case 'n': emit_literal(p, '\n'); return w;
    case 'r': emit_literal(p, '\r'); return w;
    case 't': emit_literal(p, '\t'); return w;
    case 'f': emit_literal(p, '\f'); return w;
    case 'v': emit_literal(p, '\v'); return w;
    case 'a': emit_literal(p, 0x07); return w;
    case 'x':
        {
            uint32_t v = 0;
            if (!read_hex(p, 2, &v)) {
                perr(p, start, "incomplete escape \\x");
                return w;
            }
            emit_literal(p, v);
            return w;
        }
    case 'u':
        {
            uint32_t v = 0;
            if (!read_hex(p, 4, &v)) {
                perr(p, start, "incomplete escape \\u");
                return w;
            }
            emit_literal(p, v);
            return w;
        }
    case 'U':
        {
            uint32_t v = 0;
            if (!read_hex(p, 8, &v)) {
                perr(p, start, "incomplete escape \\U");
                return w;
            }
            if (v > 0x10FFFF) {
                perr(p, start, "bad escape \\U%08X", v);
                return w;
            }
            emit_literal(p, v);
            return w;
        }
    case 'N':
        {
            /* \N{이름}은 유니코드 이름 데이터베이스가 필요하므로 지원하지 않는다.
               파이썬과 같은 형식의 오류만 낸다. */
            size_t j, o = 0;
            char name[128];
            if (p->i >= p->n || p->pat[p->i] != '{') {
                perr(p, p->i, "missing {");
                return w;
            }
            j = p->i + 1;
            while (j < p->n && p->pat[j] != '}') {
                uint32_t ch = p->pat[j];
                if (o + 1 < sizeof name)
                    name[o++] = (char)(ch < 0x80 ? ch : '?');
                j++;
            }
            name[o] = 0;
            if (o == 0) {
                perr(p, j, "missing character name");
                return w;
            }
            if (j >= p->n) {
                perr(p, p->i + 1, "missing }, unterminated name");
                return w;
            }
            p->i = j + 1;
            perr(p, start, "undefined character name '%s'", name);
            return w;
        }
    case '0':
        {
            uint32_t v = 0;
            int cnt = 0;
            while (cnt < 2 && p->i < p->n && p->pat[p->i] >= '0' && p->pat[p->i] <= '7') {
                v = v * 8 + (uint32_t)(p->pat[p->i] - '0');
                p->i++;
                cnt++;
            }
            emit_literal(p, v);
            return w;
        }
    case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        {
            size_t dig_start = p->i - 1;
            uint32_t first = c;
            size_t digits = 1;
            uint64_t num = (uint64_t)(first - '0');
            /* \123 처럼 8진수 3자리면 8진 이스케이프다. */
            if (p->i + 1 < p->n &&
                p->pat[p->i] >= '0' && p->pat[p->i] <= '7' &&
                p->pat[p->i + 1] >= '0' && p->pat[p->i + 1] <= '7') {
                uint32_t v = (uint32_t)(first - '0');
                v = v * 8 + (uint32_t)(p->pat[p->i] - '0');
                v = v * 8 + (uint32_t)(p->pat[p->i + 1] - '0');
                p->i += 2;
                if (v > 0xFF) {
                    char text[16];
                    snprintf(text, sizeof text, "\\%c%c%c", (char)first,
                             p->pat[p->i - 2] ? (char)p->pat[p->i - 2] : '0',
                             p->pat[p->i - 1] ? (char)p->pat[p->i - 1] : '0');
                    (void)text;
                    perr(p, start, "octal escape value \\%c%c%c outside of range 0-0o377",
                         (char)first, (char)p->pat[p->i - 2], (char)p->pat[p->i - 1]);
                    return w;
                }
                emit_literal(p, v);
                return w;
            }
            while (p->i < p->n && p->pat[p->i] >= '0' && p->pat[p->i] <= '9') {
                num = num * 10 + (uint64_t)(p->pat[p->i] - '0');
                p->i++;
                digits++;
            }
            (void)digits;
            if (num == 0 || num > (uint64_t)p->ngroups) {
                perr(p, dig_start, "invalid group reference %llu", (unsigned long long)num);
                return w;
            }
            {
                Inst x = inst_new(OP_BACKREF);
                x.a = (int)num;
                x.flags = p->flags;
                emit(p, x);
            }
            return (Width){0, -1};
        }
    default:
        if (ascii_letter(c)) {
            perr(p, start, "bad escape \\%c", (char)c);
            return w;
        }
        emit_literal(p, c);
        return w;
    }
}

/* ---- 문자 클래스 ---- */

typedef struct {
    int is_class;
    int kind;
    int negate;
    uint32_t cp;
    size_t start;
} CItem;

static CItem parse_citem(P *p) {
    CItem it;
    memset(&it, 0, sizeof(it));
    it.start = p->i;
    if (p->i >= p->n) {
        perr(p, p->i, "unterminated character set");
        return it;
    }
    if (p->pat[p->i] != '\\') {
        it.cp = p->pat[p->i++];
        return it;
    }
    p->i++;
    if (p->i >= p->n) {
        perr(p, p->i - 1, "bad escape (end of pattern)");
        return it;
    }
    {
        uint32_t c = p->pat[p->i++];
        switch (c) {
        case 'd': it.is_class = 1; it.kind = CIT_DIGIT; return it;
        case 'D': it.is_class = 1; it.kind = CIT_DIGIT; it.negate = 1; return it;
        case 'w': it.is_class = 1; it.kind = CIT_WORD; return it;
        case 'W': it.is_class = 1; it.kind = CIT_WORD; it.negate = 1; return it;
        case 's': it.is_class = 1; it.kind = CIT_SPACE; return it;
        case 'S': it.is_class = 1; it.kind = CIT_SPACE; it.negate = 1; return it;
        case 'b': it.cp = 0x08; return it;
        case 'n': it.cp = '\n'; return it;
        case 'r': it.cp = '\r'; return it;
        case 't': it.cp = '\t'; return it;
        case 'f': it.cp = '\f'; return it;
        case 'v': it.cp = '\v'; return it;
        case 'a': it.cp = 0x07; return it;
        case 'x':
            {
                uint32_t v = 0;
                if (!read_hex(p, 2, &v))
                    perr(p, it.start, "incomplete escape \\x");
                it.cp = v;
                return it;
            }
        case 'u':
            {
                uint32_t v = 0;
                if (!read_hex(p, 4, &v))
                    perr(p, it.start, "incomplete escape \\u");
                it.cp = v;
                return it;
            }
        case 'U':
            {
                uint32_t v = 0;
                if (!read_hex(p, 8, &v))
                    perr(p, it.start, "incomplete escape \\U");
                it.cp = v;
                return it;
            }
        default:
            if (c >= '0' && c <= '7') {
                uint32_t v = c - '0';
                int cnt = 1;
                while (cnt < 3 && p->i < p->n &&
                       p->pat[p->i] >= '0' && p->pat[p->i] <= '7') {
                    v = v * 8 + (uint32_t)(p->pat[p->i] - '0');
                    p->i++;
                    cnt++;
                }
                if (v > 0xFF) {
                    perr(p, it.start, "octal escape value outside of range 0-0o377");
                    it.cp = 0;
                }
                else {
                    it.cp = v;
                }
                return it;
            }
            if (ascii_letter(c)) {
                perr(p, it.start, "bad escape \\%c", (char)c);
                return it;
            }
            it.cp = c;
            return it;
        }
    }
}

static Width parse_class(P *p) {
    size_t open = p->i;
    RClass *c;
    Inst x;
    int first = 1;

    p->i++;
    c = class_new();
    if (p->i < p->n && p->pat[p->i] == '^') {
        c->negate = 1;
        p->i++;
    }
    for (;;) {
        CItem lo, hi;
        int is_range = 0;

        if (p->i >= p->n) {
            perr(p, open, "unterminated character set");
            return (Width){1, 1};
        }
        if (p->pat[p->i] == ']' && !first) {
            p->i++;
            break;
        }
        if (first && p->pat[p->i] == '-' && p->i + 1 < p->n && p->pat[p->i + 1] == ']') {
            /* [-a] 의 '-'는 리터럴 */
        }
        lo = parse_citem(p);
        if (p->failed)
            return (Width){1, 1};
        first = 0;
        if (p->i < p->n && p->pat[p->i] == '-' &&
            p->i + 1 < p->n && p->pat[p->i + 1] != ']') {
            size_t dash = p->i;
            (void)dash;
            p->i++;
            hi = parse_citem(p);
            if (p->failed)
                return (Width){1, 1};
            if (lo.is_class || hi.is_class || lo.cp > hi.cp) {
                char text[96];
                pattern_slice(p, lo.start, p->i, text, sizeof text);
                perr(p, lo.start, "bad character range %s", text);
                return (Width){1, 1};
            }
            is_range = 1;
        }
        if (lo.is_class) {
            class_add(c, lo.kind, lo.negate, 0, 0);
        }
        else if (is_range) {
            class_add(c, CIT_RANGE, 0, lo.cp, hi.cp);
        }
        else {
            class_add(c, CIT_RANGE, 0, lo.cp, lo.cp);
        }
    }
    x = inst_new(OP_CLASS);
    x.cls = c;
    x.flags = p->flags;
    emit(p, x);
    return (Width){1, 1};
}

/* ---- 그룹 ---- */

static int valid_group_name_char(uint32_t c, int first) {
    if (c == '_')
        return 1;
    if (c >= 'a' && c <= 'z')
        return 1;
    if (c >= 'A' && c <= 'Z')
        return 1;
    if (!first && c >= '0' && c <= '9')
        return 1;
    return c >= 0x80;
}

static int find_group_name(P *p, const char *name) {
    int i;
    for (i = 0; i < p->nnames; i++) {
        if (!strcmp(p->names[i].name, name))
            return p->names[i].index;
    }
    return -1;
}

/* 이름을 [p->i, '>') 로 읽는다. */
static int read_group_name(P *p, char *out, size_t cap, size_t start_pos) {
    size_t o = 0;
    while (p->i < p->n && p->pat[p->i] != '>') {
        uint32_t c = p->pat[p->i];
        if (o + 1 >= cap)
            break;
        out[o++] = (char)(c < 0x80 ? c : '?');
        p->i++;
    }
    out[o] = 0;
    if (o == 0) {
        perr(p, p->i, "missing group name");
        return 0;
    }
    {
        size_t k;
        for (k = 0; k < o; k++) {
            if (!valid_group_name_char((unsigned char)out[k], k == 0)) {
                perr(p, start_pos, "bad character in group name '%s'", out);
                return 0;
            }
        }
    }
    if (p->i >= p->n) {
        perr(p, start_pos, "missing >, unterminated name");
        return 0;
    }
    p->i++;
    return 1;
}

static Width parse_group_body(P *p, size_t open) {
    Width w = parse_alt(p);
    if (!p->failed) {
        if (p->i < p->n && p->pat[p->i] == ')')
            p->i++;
        else
            perr(p, open, "missing ), unterminated subpattern");
    }
    return w;
}

static Width parse_assert(P *p, int behind, int negate, size_t open, int *repeatable) {
    Inst x = inst_new(OP_ASSERT);
    size_t at;
    Width w;

    x.a = (int)(p->len + 1);
    x.d = negate;
    at = emit(p, x);
    w = parse_group_body(p, open);
    if (behind && !p->failed) {
        if (w.max < 0 || w.min != w.max)
            perr_nopos(p, "look-behind requires fixed-width pattern");
        else
            p->prog[at].c = (int)w.min;
    }
    emit(p, inst_new(OP_MATCH));
    p->prog[at].b = (int)p->len;
    *repeatable = 1;
    return (Width){0, 0};
}

static Width parse_atomic(P *p, size_t open) {
    Inst x = inst_new(OP_ATOMIC);
    size_t at;
    Width w;

    x.a = (int)(p->len + 1);
    at = emit(p, x);
    w = parse_group_body(p, open);
    emit(p, inst_new(OP_MATCH));
    p->prog[at].b = (int)p->len;
    return w;
}

static int flag_letter(uint32_t c) {
    return c == 'a' || c == 'i' || c == 'L' || c == 'm' ||
           c == 's' || c == 'u' || c == 'x';
}

static int flag_bit(uint32_t c) {
    switch (c) {
    case 'a': return F_ASCII;
    case 'i': return F_IGNORECASE;
    case 'm': return F_MULTILINE;
    case 's': return F_DOTALL;
    case 'u': return F_UNICODE;
    case 'x': return F_VERBOSE;
    default: return 0;
    }
}

static Width parse_inline_flags(P *p, size_t open, int *repeatable) {
    int add = 0, del = 0, neg = 0;
    size_t flag_pos = p->i;

    for (;;) {
        uint32_t c;
        if (p->i >= p->n)
            break;
        c = p->pat[p->i];
        if (c == '-') {
            neg = 1;
            p->i++;
            continue;
        }
        if (!flag_letter(c))
            break;
        if (c == 'L') {
            perr(p, p->i, "bad inline flags: cannot use 'L' flag with a str pattern");
            return (Width){0, 0};
        }
        if (neg)
            del |= flag_bit(c);
        else
            add |= flag_bit(c);
        p->i++;
    }
    (void)flag_pos;
    if (p->i >= p->n) {
        perr(p, p->i, "missing -, : or )");
        return (Width){0, 0};
    }
    if (p->pat[p->i] == ')') {
        p->i++;
        if (!(open == 0 || open == p->global_prefix_end)) {
            perr(p, open, "global flags not at the start of the expression");
            return (Width){0, 0};
        }
        p->flags = (p->flags | add) & ~del;
        p->global_prefix_end = p->i;
        *repeatable = 0;
        return (Width){0, 0};
    }
    if (p->pat[p->i] == ':') {
        int saved = p->flags;
        Width w;
        p->i++;
        p->flags = (p->flags | add) & ~del;
        w = parse_group_body(p, open);
        p->flags = saved;
        *repeatable = 1;
        return w;
    }
    perr(p, p->i, "missing -, : or )");
    return (Width){0, 0};
}

static Width parse_conditional(P *p, size_t open, int *repeatable) {
    CondRef ref;
    Inst x = inst_new(OP_COND);
    size_t at, no_start, yes_jmp = 0;
    int has_bar = 0;
    Width w = {0, -1};

    memset(&ref, 0, sizeof(ref));
    ref.pos = p->i;
    if (p->i < p->n && p->pat[p->i] >= '0' && p->pat[p->i] <= '9') {
        long num = 0;
        while (p->i < p->n && p->pat[p->i] >= '0' && p->pat[p->i] <= '9') {
            num = num * 10 + (long)(p->pat[p->i] - '0');
            p->i++;
        }
        ref.group = (int)num;
    }
    else if (p->i < p->n && p->pat[p->i] != ')') {
        size_t o = 0;
        ref.has_name = 1;
        while (p->i < p->n && p->pat[p->i] != ')') {
            uint32_t c = p->pat[p->i];
            if (o + 1 >= sizeof ref.name)
                break;
            ref.name[o++] = (char)(c < 0x80 ? c : '?');
            p->i++;
        }
        ref.name[o] = 0;
        {
            size_t k;
            for (k = 0; k < o; k++) {
                if (!valid_group_name_char((unsigned char)ref.name[k], k == 0)) {
                    perr(p, ref.pos, "bad character in group name '%s'", ref.name);
                    return w;
                }
            }
        }
    }
    if (p->i == ref.pos || (p->i < p->n && p->pat[p->i] != ')')) {
        perr(p, ref.pos, "missing group name");
        return w;
    }
    if (p->i >= p->n) {
        perr(p, ref.pos, "missing group name");
        return w;
    }
    p->i++;  /* ')' */

    x.a = ref.has_name ? -2 : ref.group;
    at = emit(p, x);
    ref.inst = at;
    if (p->nconds < (int)(sizeof p->conds / sizeof p->conds[0]))
        p->conds[p->nconds++] = ref;

    parse_seq(p);
    if (!p->failed && p->i < p->n && p->pat[p->i] == '|') {
        Inst j = inst_new(OP_JMP);
        p->i++;
        has_bar = 1;
        yes_jmp = emit(p, j);
        no_start = p->len;
        parse_seq(p);
        if (!p->failed && p->i < p->n && p->pat[p->i] == '|')
            perr(p, p->i, "conditional backref with more than two branches");
    }
    else {
        no_start = p->len;
    }
    if (!p->failed) {
        if (p->i < p->n && p->pat[p->i] == ')')
            p->i++;
        else
            perr(p, open, "missing ), unterminated subpattern");
    }
    if (!p->failed) {
        p->prog[at].b = (int)(at + 1);
        p->prog[at].c = (int)no_start;
        if (has_bar)
            p->prog[yes_jmp].a = (int)p->len;
    }
    *repeatable = 1;
    return w;
}

static Width parse_group(P *p, int *repeatable) {
    size_t open = p->i;

    if (p->depth >= 200) {
        perr(p, open, "sorry, but this version only supports 200 nested groups");
        return (Width){0, 0};
    }
    p->depth++;
    p->i++;  /* '(' */

    if (p->i < p->n && p->pat[p->i] == '?') {
        uint32_t c;
        p->i++;
        if (p->i >= p->n) {
            perr_nopos(p, "unexpected end of pattern");
            p->depth--;
            return (Width){0, 0};
        }
        c = p->pat[p->i];
        if (c == ':') {
            Width w;
            p->i++;
            w = parse_group_body(p, open);
            *repeatable = 1;
            p->depth--;
            return w;
        }
        if (c == '=' || c == '!') {
            Width w;
            p->i++;
            w = parse_assert(p, 0, c == '!', open, repeatable);
            p->depth--;
            return w;
        }
        if (c == '<') {
            if (p->i + 1 < p->n && (p->pat[p->i + 1] == '=' || p->pat[p->i + 1] == '!')) {
                int neg = p->pat[p->i + 1] == '!';
                Width w;
                p->i += 2;
                w = parse_assert(p, 1, neg, open, repeatable);
                p->depth--;
                return w;
            }
            {
                char ext[48];
                size_t o = 0;
                ext[o++] = '?';
                {
                    size_t k = p->i;
                    while (k < p->n && p->pat[k] != ')' && p->pat[k] != ':' && o + 1 < sizeof ext) {
                        uint32_t ch = p->pat[k];
                        ext[o++] = (char)(ch < 0x80 ? ch : '?');
                        k++;
                    }
                }
                ext[o] = 0;
                perr(p, open + 1, "unknown extension %s", ext);
                p->depth--;
                return (Width){0, 0};
            }
        }
        if (c == 'P') {
            p->i++;
            if (p->i >= p->n) {
                perr_nopos(p, "unexpected end of pattern");
                p->depth--;
                return (Width){0, 0};
            }
            c = p->pat[p->i];
            if (c == '<') {
                char name[64];
                size_t name_pos;
                int idx;
                int g;
                Inst sv;
                Width w;

                p->i++;
                name_pos = p->i;
                if (!read_group_name(p, name, sizeof name, name_pos)) {
                    p->depth--;
                    return (Width){0, 0};
                }
                idx = find_group_name(p, name);
                if (idx >= 0) {
                    perr(p, name_pos, "redefinition of group name '%s' as group %d; was group %d",
                         name, p->ngroups + 1, idx);
                    p->depth--;
                    return (Width){0, 0};
                }
                if (p->nnames >= (int)(sizeof p->names / sizeof p->names[0])) {
                    perr(p, name_pos, "too many named groups");
                    p->depth--;
                    return (Width){0, 0};
                }
                g = ++p->ngroups;
                snprintf(p->names[p->nnames].name, sizeof p->names[0].name, "%s", name);
                p->names[p->nnames].index = g;
                p->nnames++;

                sv = inst_new(OP_SAVE);
                sv.a = 2 * g;
                emit(p, sv);
                w = parse_group_body(p, open);
                sv = inst_new(OP_SAVE);
                sv.a = 2 * g + 1;
                emit(p, sv);
                *repeatable = 1;
                p->depth--;
                return w;
            }
            if (c == '=') {
                char name[64];
                size_t name_pos, o = 0;
                int idx;

                p->i++;
                name_pos = p->i;
                while (p->i < p->n && p->pat[p->i] != ')') {
                    uint32_t ch = p->pat[p->i];
                    if (o + 1 < sizeof name)
                        name[o++] = (char)(ch < 0x80 ? ch : '?');
                    p->i++;
                }
                name[o] = 0;
                if (o == 0) {
                    perr(p, name_pos, "missing group name");
                    p->depth--;
                    return (Width){0, 0};
                }
                {
                    size_t k;
                    for (k = 0; k < o; k++) {
                        if (!valid_group_name_char((unsigned char)name[k], k == 0)) {
                            perr(p, name_pos, "bad character in group name '%s'", name);
                            p->depth--;
                            return (Width){0, 0};
                        }
                    }
                }
                if (p->i >= p->n) {
                    perr(p, open, "missing ), unterminated subpattern");
                    p->depth--;
                    return (Width){0, 0};
                }
                p->i++;  /* ')' */
                idx = find_group_name(p, name);
                if (idx < 0) {
                    perr(p, name_pos, "unknown group name '%s'", name);
                    p->depth--;
                    return (Width){0, 0};
                }
                {
                    Inst x = inst_new(OP_BACKREF);
                    x.a = idx;
                    x.flags = p->flags;
                    emit(p, x);
                }
                *repeatable = 1;
                p->depth--;
                return (Width){0, -1};
            }
            {
                char ext[48];
                size_t o = 0;
                size_t k = p->i - 1;
                ext[o++] = '?';
                while (k < p->n && p->pat[k] != ')' && o + 1 < sizeof ext) {
                    uint32_t ch = p->pat[k];
                    ext[o++] = (char)(ch < 0x80 ? ch : '?');
                    k++;
                }
                ext[o] = 0;
                perr(p, open + 1, "unknown extension %s", ext);
                p->depth--;
                return (Width){0, 0};
            }
        }
        if (c == '>') {
            Width w;
            p->i++;
            w = parse_atomic(p, open);
            *repeatable = 1;
            p->depth--;
            return w;
        }
        if (c == '#') {
            p->i++;
            while (p->i < p->n && p->pat[p->i] != ')')
                p->i++;
            if (p->i >= p->n) {
                perr(p, open, "missing ), unterminated comment");
                p->depth--;
                return (Width){0, 0};
            }
            p->i++;
            *repeatable = 0;
            p->depth--;
            return (Width){0, 0};
        }
        if (c == '(') {
            Width w;
            p->i++;
            w = parse_conditional(p, open, repeatable);
            p->depth--;
            return w;
        }
        if (flag_letter(c) || c == '-') {
            Width w = parse_inline_flags(p, open, repeatable);
            p->depth--;
            return w;
        }
        {
            char ext[48];
            size_t o = 0;
            size_t k = p->i - 1;
            ext[o++] = '?';
            while (k < p->n && p->pat[k] != ')' && o + 1 < sizeof ext) {
                uint32_t ch = p->pat[k];
                ext[o++] = (char)(ch < 0x80 ? ch : '?');
                k++;
            }
            ext[o] = 0;
            perr(p, open + 1, "unknown extension %s", ext);
            p->depth--;
            return (Width){0, 0};
        }
    }

    {
        int g = ++p->ngroups;
        Inst sv;
        Width w;

        sv = inst_new(OP_SAVE);
        sv.a = 2 * g;
        emit(p, sv);
        w = parse_group_body(p, open);
        sv = inst_new(OP_SAVE);
        sv.a = 2 * g + 1;
        emit(p, sv);
        *repeatable = 1;
        p->depth--;
        return w;
    }
}

/* ---- 원자 ---- */

static Width parse_atom(P *p, int *repeatable) {
    uint32_t c;
    Width w = {1, 1};

    *repeatable = 1;
    if (p->i >= p->n) {
        perr_nopos(p, "unexpected end of pattern");
        return (Width){0, 0};
    }
    c = p->pat[p->i];
    switch (c) {
    case '(':
        return parse_group(p, repeatable);
    case '[':
        return parse_class(p);
    case '.':
        {
            Inst x = inst_new(OP_ANY);
            x.flags = p->flags;
            emit(p, x);
            p->i++;
            return w;
        }
    case '^':
        {
            Inst x = inst_new(OP_AT);
            x.a = AT_BOL;
            x.flags = p->flags;
            emit(p, x);
            p->i++;
            *repeatable = 0;
            return (Width){0, 0};
        }
    case '$':
        {
            Inst x = inst_new(OP_AT);
            x.a = AT_EOL;
            x.flags = p->flags;
            emit(p, x);
            p->i++;
            *repeatable = 0;
            return (Width){0, 0};
        }
    case '\\':
        return parse_escape_atom(p, repeatable);
    default:
        emit_literal(p, c);
        p->i++;
        return w;
    }
}

/* ---- 수량자 ---- */

static int read_number(P *p, long *out) {
    long v = 0;
    int digits = 0;
    while (p->i < p->n && p->pat[p->i] >= '0' && p->pat[p->i] <= '9') {
        v = v * 10 + (long)(p->pat[p->i] - '0');
        if (v > 1000000)
            v = 1000000;
        p->i++;
        digits++;
    }
    *out = v;
    return digits;
}

static int parse_brace_quant(P *p, long *min, long *max) {
    size_t save = p->i;

    p->i++;  /* '{' */
    if (read_number(p, min) == 0) {
        p->i = save;
        return 0;
    }
    if (p->i < p->n && p->pat[p->i] == '}') {
        p->i++;
        *max = *min;
        return 1;
    }
    if (p->i >= p->n || p->pat[p->i] != ',') {
        p->i = save;
        return 0;
    }
    p->i++;
    if (p->i < p->n && p->pat[p->i] == '}') {
        p->i++;
        *max = -1;
        return 1;
    }
    if (read_number(p, max) == 0 || p->i >= p->n || p->pat[p->i] != '}') {
        p->i = save;
        return 0;
    }
    p->i++;
    if (*min > *max)
        perr(p, save + 1, "min repeat greater than max repeat");
    return 1;
}

static Width parse_quant(P *p) {
    Inst *saved_prog = p->prog;
    size_t saved_len = p->len, saved_cap = p->cap;
    Inst *body;
    size_t blen;
    Width w, qw = {0, 0};
    int repeatable = 1;
    long min = 0, max = 0;
    int has_quant = 0, greedy = 1, possessive = 0;
    size_t qpos;

    skip_ignored(p);
    if (p->failed || p->i >= p->n)
        return qw;
    {
        uint32_t c = p->pat[p->i];
        if (c == '*' || c == '+' || c == '?') {
            perr(p, p->i, "nothing to repeat");
            return qw;
        }
        if (c == '{' && brace_looks_quant(p, p->i)) {
            perr(p, p->i, "nothing to repeat");
            return qw;
        }
    }

    p->prog = NULL;
    p->len = 0;
    p->cap = 0;
    w = parse_atom(p, &repeatable);
    body = p->prog;
    blen = p->len;
    p->prog = saved_prog;
    p->len = saved_len;
    p->cap = saved_cap;
    if (p->failed) {
        free(body);
        return w;
    }

    skip_ignored(p);
    qpos = p->i;
    if (p->i < p->n) {
        uint32_t c = p->pat[p->i];
        if (c == '*') {
            min = 0;
            max = -1;
            p->i++;
            has_quant = 1;
        }
        else if (c == '+') {
            min = 1;
            max = -1;
            p->i++;
            has_quant = 1;
        }
        else if (c == '?') {
            min = 0;
            max = 1;
            p->i++;
            has_quant = 1;
        }
        else if (c == '{') {
            if (parse_brace_quant(p, &min, &max))
                has_quant = 1;
        }
    }
    if (has_quant && !repeatable) {
        perr(p, qpos, "nothing to repeat");
        free(body);
        return w;
    }
    if (has_quant && !p->failed) {
        skip_ignored(p);
        if (p->i < p->n && p->pat[p->i] == '?') {
            greedy = 0;
            p->i++;
        }
        else if (p->i < p->n && p->pat[p->i] == '+') {
            possessive = 1;
            p->i++;
        }
        skip_ignored(p);
        if (p->i < p->n) {
            uint32_t c = p->pat[p->i];
            if (c == '*' || c == '+' || c == '?' || (c == '{' && brace_looks_quant(p, p->i))) {
                perr(p, p->i, "multiple repeat");
                free(body);
                return w;
            }
        }
    }

    if (p->failed) {
        free(body);
        return w;
    }

    if (!has_quant) {
        append_body(p, body, blen, (int)p->len);
        free(body);
        return w;
    }

    {
        long long qmin = (long long)w.min * min;
        long long qmax;
        if (max < 0 || w.max < 0)
            qmax = -1;
        else
            qmax = (long long)w.max * max;
        if (qmin > 1000000000LL)
            qmin = 1000000000LL;
        if (qmax > 1000000000LL)
            qmax = 1000000000LL;
        qw.min = (long)qmin;
        qw.max = (long)qmax;
    }

    emit_quant(p, body, blen, min, max, greedy, possessive);
    free(body);
    return qw;
}

/* ---- 컴파일 진입점 ---- */

AsterRegex *Regex_Compile(Interp *in, const char *pattern, size_t len, int flags,
                          char *err, size_t errcap) {
    P p;
    AsterRegex *re;
    size_t k;

    (void)in;
    memset(&p, 0, sizeof(p));
    p.pat = decode_cps(pattern, len, &p.n);
    p.flags = flags & F_ALL;
    if (!(p.flags & F_ASCII))
        p.flags |= F_UNICODE;
    p.i = 0;
    p.global_prefix_end = 0;

    {
        Inst sv = inst_new(OP_SAVE);
        sv.a = 0;
        emit(&p, sv);
    }
    if (!p.failed)
        parse_alt(&p);
    if (!p.failed && p.i < p.n) {
        if (p.pat[p.i] == ')')
            perr(&p, p.i, "unbalanced parenthesis");
        else
            perr(&p, p.i, "unexpected character");
    }
    if (!p.failed) {
        Inst sv = inst_new(OP_SAVE);
        sv.a = 1;
        emit(&p, sv);
        emit(&p, inst_new(OP_MATCH));
    }

    if (!p.failed) {
        int ci;
        for (ci = 0; ci < p.nconds && !p.failed; ci++) {
            int g;
            if (p.conds[ci].has_name) {
                g = find_group_name(&p, p.conds[ci].name);
                if (g < 0)
                    perr(&p, p.conds[ci].pos, "unknown group name '%s'", p.conds[ci].name);
                else
                    p.prog[p.conds[ci].inst].a = g;
            }
            else {
                g = p.conds[ci].group;
                if (g <= 0 || g > p.ngroups)
                    perr(&p, p.conds[ci].pos, "invalid group reference %d", g);
            }
        }
    }

    if (!p.failed) {
        /* 음수 슬롯은 반복 진행 확인용 스크래치 슬롯이다. 그룹 슬롯 뒤로 옮긴다. */
        int base = 2 * (p.ngroups + 1);
        for (k = 0; k < p.len; k++) {
            if (p.prog[k].op == OP_PSET || p.prog[k].op == OP_PCHK) {
                if (p.prog[k].a < 0)
                    p.prog[k].a = base + (-p.prog[k].a - 1);
            }
        }
    }

    if (p.failed) {
        snprintf(err, errcap, "%s", p.err);
        for (k = 0; k < p.len; k++)
            if (p.prog[k].cls)
                free(p.prog[k].cls);
        free(p.prog);
        free(p.pat);
        return NULL;
    }

    re = xmalloc(sizeof(*re));
    memset(re, 0, sizeof(*re));
    re->prog = p.prog;
    re->nprog = p.len;
    re->ngroups = p.ngroups;
    re->flags = p.flags;
    re->pattern = xstrndup(pattern, len);
    re->plen = len;
    re->nscratch = p.nscratch;
    re->nslots = 2 * (p.ngroups + 1) + (size_t)p.nscratch;
    if (p.nnames > 0) {
        re->names = xmalloc((size_t)p.nnames * sizeof(GroupName));
        memcpy(re->names, p.names, (size_t)p.nnames * sizeof(GroupName));
    }
    re->nnames = p.nnames;
    free(p.pat);
    return re;
}

const char *Regex_PatternText(const AsterRegex *re) { return re->pattern; }
size_t Regex_PatternLength(const AsterRegex *re) { return re->plen; }
int Regex_Flags(const AsterRegex *re) { return re->flags; }
int Regex_GroupCount(const AsterRegex *re) { return re->ngroups; }

int Regex_GroupIndex(const AsterRegex *re, const char *name, size_t namelen) {
    int i;
    for (i = 0; i < re->nnames; i++) {
        if (strlen(re->names[i].name) == namelen &&
            !memcmp(re->names[i].name, name, namelen))
            return re->names[i].index;
    }
    return -1;
}

/* ---- VM ---- */

typedef struct {
    int pc;
    size_t pos;
    size_t log_len;
} VFrame;

typedef struct {
    int slot;
    size_t old;
} LogEnt;

typedef struct {
    AsterRegex *re;
    const uint32_t *cps;
    size_t len;
    size_t *slots;
    size_t nslots;
    VFrame *frames;
    size_t nframes, cap_frames;
    LogEnt *log;
    size_t nlog, cap_log;
    long steps;
    long max_steps;
    int hard;
} Vm;

typedef struct {
    AsterString *s;
    uint32_t *cps;
    size_t *boff;
    size_t len;
} Subject;

static void subject_init(Subject *sub, AsterString *s) {
    size_t i = 0, cp = 0;
    sub->s = s;
    sub->len = s->cp_length;
    sub->cps = xmalloc((sub->len + 1) * sizeof(uint32_t));
    sub->boff = xmalloc((sub->len + 1) * sizeof(size_t));
    while (i < s->length) {
        size_t adv;
        sub->cps[cp] = Aster_UTF8_Decode(s->data + i, s->length - i, &adv);
        sub->boff[cp] = i;
        i += adv;
        cp++;
    }
    sub->boff[sub->len] = s->length;
}

static void subject_free(Subject *sub) {
    free(sub->cps);
    free(sub->boff);
}

static void vm_init(Vm *vm, AsterRegex *re, Subject *sub) {
    memset(vm, 0, sizeof(*vm));
    vm->re = re;
    vm->cps = sub->cps;
    vm->len = sub->len;
    vm->nslots = re->nslots;
    vm->slots = xmalloc(vm->nslots * sizeof(size_t));
    vm->max_steps = 10000000L;
}

static void vm_free(Vm *vm) {
    free(vm->slots);
    free(vm->frames);
    free(vm->log);
}

static void vm_prepare(Vm *vm) {
    size_t i;
    for (i = 0; i < vm->nslots; i++)
        vm->slots[i] = SIZE_MAX;
    vm->nlog = 0;
    vm->nframes = 0;
}

static void vm_push_frame(Vm *vm, int pc, size_t pos) {
    if (vm->nframes == vm->cap_frames) {
        vm->cap_frames = vm->cap_frames ? vm->cap_frames * 2 : 32;
        vm->frames = xrealloc(vm->frames, vm->cap_frames * sizeof(VFrame));
    }
    vm->frames[vm->nframes].pc = pc;
    vm->frames[vm->nframes].pos = pos;
    vm->frames[vm->nframes].log_len = vm->nlog;
    vm->nframes++;
}

static void vm_log(Vm *vm, int slot, size_t old) {
    if (vm->nlog == vm->cap_log) {
        vm->cap_log = vm->cap_log ? vm->cap_log * 2 : 64;
        vm->log = xrealloc(vm->log, vm->cap_log * sizeof(LogEnt));
    }
    vm->log[vm->nlog].slot = slot;
    vm->log[vm->nlog].old = old;
    vm->nlog++;
}

static void vm_undo(Vm *vm, size_t mark) {
    while (vm->nlog > mark) {
        vm->nlog--;
        vm->slots[vm->log[vm->nlog].slot] = vm->log[vm->nlog].old;
    }
}

static int at_match(Inst *ins, const uint32_t *cps, size_t len, size_t pos) {
    switch (ins->a) {
    case AT_BOL:
        return pos == 0 || ((ins->flags & F_MULTILINE) && cps[pos - 1] == '\n');
    case AT_EOL:
        if (ins->flags & F_MULTILINE)
            return pos == len || cps[pos] == '\n';
        return pos == len || (pos + 1 == len && cps[pos] == '\n');
    case AT_BOS:
        return pos == 0;
    case AT_EOS:
        return pos == len;
    case AT_BOUND:
        {
            int before = pos > 0 && is_word_cp(cps[pos - 1], (ins->flags & F_ASCII) != 0);
            int after = pos < len && is_word_cp(cps[pos], (ins->flags & F_ASCII) != 0);
            return before != after;
        }
    case AT_NBOUND:
        {
            int before = pos > 0 && is_word_cp(cps[pos - 1], (ins->flags & F_ASCII) != 0);
            int after = pos < len && is_word_cp(cps[pos], (ins->flags & F_ASCII) != 0);
            return before == after;
        }
    }
    return 0;
}

static int vm_run(Vm *vm, int pc, size_t pos, int stop_mode, size_t stop_pos,
                  size_t *end_out) {
    size_t base_frames = vm->nframes;
    size_t base_log = vm->nlog;
    Inst *prog = vm->re->prog;

    for (;;) {
        Inst *in;
        if (++vm->steps > vm->max_steps) {
            vm->hard = 1;
            goto fail;
        }
        in = &prog[pc];
        switch (in->op) {
        case OP_CHAR:
            if (pos < vm->len && cp_eq(vm->cps[pos], in->cp, in->flags)) {
                pos++;
                pc++;
            }
            else {
                goto fail;
            }
            break;
        case OP_ANY:
            if (pos < vm->len && ((in->flags & F_DOTALL) || vm->cps[pos] != '\n')) {
                pos++;
                pc++;
            }
            else {
                goto fail;
            }
            break;
        case OP_CLASS:
            if (pos < vm->len && class_match(in->cls, vm->cps[pos], in->flags)) {
                pos++;
                pc++;
            }
            else {
                goto fail;
            }
            break;
        case OP_AT:
            if (at_match(in, vm->cps, vm->len, pos))
                pc++;
            else
                goto fail;
            break;
        case OP_SPLIT:
            vm_push_frame(vm, in->b, pos);
            pc = in->a;
            break;
        case OP_JMP:
            pc = in->a;
            break;
        case OP_FAIL:
            goto fail;
        case OP_SAVE:
            vm_log(vm, in->a, vm->slots[in->a]);
            vm->slots[in->a] = pos;
            pc++;
            break;
        case OP_PSET:
            vm_log(vm, in->a, vm->slots[in->a]);
            vm->slots[in->a] = pos;
            pc++;
            break;
        case OP_PCHK:
            if (vm->slots[in->a] == pos)
                pc = in->b;
            else
                pc++;
            break;
        case OP_MATCH:
            if (stop_mode && pos != stop_pos)
                goto fail;
            vm->nframes = base_frames;
            *end_out = pos;
            return 1;
        case OP_BACKREF:
            {
                size_t gs = vm->slots[2 * in->a];
                size_t ge = vm->slots[2 * in->a + 1];
                size_t glen, k;
                if (gs == SIZE_MAX || ge == SIZE_MAX)
                    goto fail;
                glen = ge - gs;
                if (pos + glen > vm->len)
                    goto fail;
                for (k = 0; k < glen; k++) {
                    if (!cp_eq(vm->cps[gs + k], vm->cps[pos + k], in->flags))
                        goto fail;
                }
                pos += glen;
                pc++;
            }
            break;
        case OP_COND:
            pc = (vm->slots[2 * in->a] != SIZE_MAX) ? in->b : in->c;
            break;
        case OP_ASSERT:
            {
                size_t save = pos;
                size_t bmark = vm->nlog;
                size_t e = 0;
                int ok;

                if (in->c >= 0) {
                    if (pos < (size_t)in->c)
                        ok = 0;
                    else
                        ok = vm_run(vm, in->a, pos - (size_t)in->c, 1, pos, &e);
                }
                else {
                    ok = vm_run(vm, in->a, pos, 0, 0, &e);
                }
                if (vm->hard)
                    goto fail;
                if (in->d) {
                    vm_undo(vm, bmark);
                    if (ok)
                        goto fail;
                }
                else {
                    if (!ok) {
                        vm_undo(vm, bmark);
                        goto fail;
                    }
                }
                pos = save;
                pc = in->b;
            }
            break;
        case OP_ATOMIC:
            {
                size_t bmark = vm->nlog;
                size_t e = 0;
                if (vm_run(vm, in->a, pos, 0, 0, &e)) {
                    if (vm->hard)
                        goto fail;
                    pos = e;
                    pc = in->b;
                }
                else {
                    vm_undo(vm, bmark);
                    goto fail;
                }
            }
            break;
        default:
            goto fail;
        }
        continue;

    fail:
        if (vm->nframes > base_frames) {
            VFrame f = vm->frames[--vm->nframes];
            vm_undo(vm, f.log_len);
            pc = f.pc;
            pos = f.pos;
            continue;
        }
        vm_undo(vm, base_log);
        return 0;
    }
}

/* ---- 실행 ---- */

static int regex_run(Interp *in, AsterRegex *re, Subject *sub, size_t start,
                     int mode, size_t *slots) {
    Vm vm;
    int found = 0, hard;

    vm_init(&vm, re, sub);
    if (start > sub->len) {
        vm_free(&vm);
        RT_Throw(in, "ValueError", "start position out of range");
        return -1;
    }
    if (mode == REGEX_MODE_SEARCH) {
        size_t s;
        for (s = start; s <= sub->len; s++) {
            size_t end = 0;
            vm_prepare(&vm);
            if (vm_run(&vm, 0, s, 0, 0, &end)) {
                found = 1;
                break;
            }
            if (vm.hard)
                break;
        }
    }
    else {
        size_t end = 0;
        vm_prepare(&vm);
        if (vm_run(&vm, 0, start, mode == REGEX_MODE_FULLMATCH ? 1 : 0,
                   sub->len, &end))
            found = 1;
    }
    hard = vm.hard;
    if (found)
        memcpy(slots, vm.slots, re->nslots * sizeof(size_t));
    vm_free(&vm);
    if (hard) {
        RT_Throw(in, "RuntimeError", "regular expression backtracking limit exceeded");
        return -1;
    }
    return found;
}

int Regex_Intern(Interp *in, AsterRegex *re) {
    if (in->nregex_objs == in->cap_regex_objs) {
        size_t cap = in->cap_regex_objs ? in->cap_regex_objs * 2 : 8;
        void **grown = realloc(in->regex_objs, cap * sizeof(void *));
        if (!grown)
            return -1;
        in->regex_objs = grown;
        in->cap_regex_objs = cap;
    }
    in->regex_objs[in->nregex_objs] = re;
    return (int)in->nregex_objs++;
}

AsterRegex *Regex_Get(Interp *in, int id) {
    if (id < 0 || (size_t)id >= in->nregex_objs)
        return NULL;
    return in->regex_objs[id];
}

int RMatch_Intern(Interp *in, RMatch *m) {
    if (in->nregex_matches == in->cap_regex_matches) {
        size_t cap = in->cap_regex_matches ? in->cap_regex_matches * 2 : 8;
        void **grown = realloc(in->regex_matches, cap * sizeof(void *));
        if (!grown) {
            free(m->slots);
            free(m);
            return -1;
        }
        in->regex_matches = grown;
        in->cap_regex_matches = cap;
    }
    in->regex_matches[in->nregex_matches] = m;
    return (int)in->nregex_matches++;
}

RMatch *RMatch_Get(Interp *in, int id) {
    if (id < 0 || (size_t)id >= in->nregex_matches)
        return NULL;
    return in->regex_matches[id];
}

void Regex_ReleaseMatch(Interp *in, int id) {
    RMatch *m;
    if (id < 0 || (size_t)id >= in->nregex_matches)
        return;
    m = in->regex_matches[id];
    if (m) {
        free(m->slots);
        free(m);
        in->regex_matches[id] = NULL;
    }
}

int Regex_Exec(Interp *in, AsterRegex *re, AsterString *subject, size_t start_cp,
               int mode, RMatch **out) {
    Subject sub;
    size_t *slots = xmalloc(re->nslots * sizeof(size_t));
    int r;

    subject_init(&sub, subject);
    r = regex_run(in, re, &sub, start_cp, mode, slots);
    subject_free(&sub);
    if (r == 1) {
        RMatch *m = xmalloc(sizeof(*m));
        int id;
        m->re = re;
        m->subject = subject;
        m->slots = xmalloc(re->nslots * sizeof(size_t));
        memcpy(m->slots, slots, re->nslots * sizeof(size_t));
        id = RMatch_Intern(in, m);
        if (id < 0) {
            free(slots);
            RT_Throw(in, "RuntimeError", "out of memory while storing a match");
            return -1;
        }
        *out = m;
        free(slots);
        return id;
    }
    free(slots);
    return -1;
}

size_t RMatch_Start(const RMatch *m, int group) {
    if (!m || group < 0 || group > m->re->ngroups)
        return SIZE_MAX;
    return m->slots[2 * group];
}

size_t RMatch_End(const RMatch *m, int group) {
    if (!m || group < 0 || group > m->re->ngroups)
        return SIZE_MAX;
    return m->slots[2 * group + 1];
}

AsterString *RMatch_Group(Interp *in, const RMatch *m, int group) {
    size_t s, e, b1, b2;
    (void)in;
    if (!m || group < 0 || group > m->re->ngroups)
        return NULL;
    s = m->slots[2 * group];
    e = m->slots[2 * group + 1];
    if (s == SIZE_MAX || e == SIZE_MAX)
        return NULL;
    b1 = AsterString_ByteOffset(m->subject, s);
    b2 = AsterString_ByteOffset(m->subject, e);
    return AsterString_FromUTF8(m->subject->data + b1, b2 - b1);
}

/* ---- 내장 함수 ---- */

static int want_args(Interp *in, const char *name, size_t nargs, size_t want) {
    if (nargs != want) {
        RT_Throw(in, "TypeError", "%s() takes %zu argument(s) but %zu were given",
                 name, want, nargs);
        return 0;
    }
    return 1;
}

static AsterString *want_str(Interp *in, const char *name, RValue *v) {
    if (!v || v->kind != RV_STR) {
        RT_Throw(in, "TypeError", "%s() expects a str argument", name);
        return NULL;
    }
    return v->u.s;
}

static int want_int(Interp *in, const char *name, RValue *v, long *out) {
    int ok;
    int64_t n;
    if (!v || v->kind != RV_INT) {
        RT_Throw(in, "TypeError", "%s() expects an int argument", name);
        return 0;
    }
    n = RValue_AsInt(in, v, &ok);
    if (!ok)
        return 0;
    *out = (long)n;
    return 1;
}

static AsterRegex *want_re(Interp *in, const char *name, long id) {
    AsterRegex *re = Regex_Get(in, (int)id);
    if (!re)
        RT_Throw(in, "ValueError", "%s(): invalid compiled pattern", name);
    return re;
}

static RMatch *want_match(Interp *in, const char *name, long id) {
    RMatch *m = RMatch_Get(in, (int)id);
    if (!m)
        RT_Throw(in, "ValueError", "%s(): invalid match handle", name);
    return m;
}

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static void buf_grow(Buf *b, size_t need) {
    if (b->len + need + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->len + need + 1)
            cap *= 2;
        b->data = xrealloc(b->data, cap);
        b->cap = cap;
    }
}

static void buf_put(Buf *b, const char *s, size_t n) {
    buf_grow(b, n);
    if (n)
        memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_put_cp(Buf *b, uint32_t cp) {
    char tmp[4];
    size_t k = Aster_UTF8_Encode(cp, tmp);
    buf_put(b, tmp, k);
}

static RValue *buf_value(Interp *in, Buf *b) {
    AsterString *s = AsterString_FromUTF8(b->data ? b->data : "", b->len);
    free(b->data);
    return RValue_Str(in, s);
}

static RValue *substr_value(Interp *in, AsterString *s, size_t cp_start, size_t cp_end) {
    size_t b1 = AsterString_ByteOffset(s, cp_start);
    size_t b2 = AsterString_ByteOffset(s, cp_end);
    return RValue_Str(in, AsterString_FromUTF8(s->data + b1, b2 - b1));
}

static RValue *re_bi_compile(Interp *in, RValue *self, RValue **args, size_t nargs) {
    AsterString *pat;
    long flags;
    char err[512];
    AsterRegex *re;
    int id;

    (void)self;
    if (!want_args(in, "compile", nargs, 2))
        return RValue_Null(in);
    pat = want_str(in, "compile", args[0]);
    if (!pat || !want_int(in, "compile", args[1], &flags))
        return RValue_Null(in);
    re = Regex_Compile(in, pat->data, pat->length, (int)flags, err, sizeof err);
    if (!re)
        return RT_ThrowValue(in, "RegexError", "%s", err);
    id = Regex_Intern(in, re);
    if (id < 0)
        return RT_ThrowValue(in, "RuntimeError", "out of memory");
    return RValue_Int(in, id);
}

static RValue *re_bi_exec(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid, pos, mode;
    AsterString *subject;
    AsterRegex *re;
    RMatch *m = NULL;
    int id;

    (void)self;
    if (!want_args(in, "exec", nargs, 4))
        return RValue_Null(in);
    if (!want_int(in, "exec", args[0], &reid) ||
        !want_int(in, "exec", args[2], &pos) ||
        !want_int(in, "exec", args[3], &mode))
        return RValue_Null(in);
    subject = want_str(in, "exec", args[1]);
    if (!subject)
        return RValue_Null(in);
    re = want_re(in, "exec", reid);
    if (!re)
        return RValue_Null(in);
    if (pos < 0 || mode < 0 || mode > 2)
        return RT_ThrowValue(in, "ValueError", "exec(): bad position or mode");
    if ((size_t)pos > subject->cp_length)
        return RT_ThrowValue(in, "ValueError", "start position out of range");
    id = Regex_Exec(in, re, subject, (size_t)pos, (int)mode, &m);
    if (in->sig != SIG_OK)
        return RValue_Null(in);
    return RValue_Int(in, id);
}

static RValue *re_bi_group(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid, g;
    RMatch *m;
    AsterString *s;

    (void)self;
    if (!want_args(in, "group", nargs, 2) ||
        !want_int(in, "group", args[0], &mid) ||
        !want_int(in, "group", args[1], &g))
        return RValue_Null(in);
    m = want_match(in, "group", mid);
    if (!m)
        return RValue_Null(in);
    if (g < 0 || g > m->re->ngroups)
        return RT_ThrowValue(in, "IndexError", "no such group");
    s = RMatch_Group(in, m, (int)g);
    return s ? RValue_Str(in, s) : RValue_Null(in);
}

static RValue *re_bi_groupname(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    AsterString *name;
    RMatch *m;
    int g;
    AsterString *s;

    (void)self;
    if (!want_args(in, "groupname", nargs, 2) ||
        !want_int(in, "groupname", args[0], &mid))
        return RValue_Null(in);
    name = want_str(in, "groupname", args[1]);
    if (!name)
        return RValue_Null(in);
    m = want_match(in, "groupname", mid);
    if (!m)
        return RValue_Null(in);
    g = Regex_GroupIndex(m->re, name->data, name->length);
    if (g < 0)
        return RT_ThrowValue(in, "IndexError", "no such group");
    s = RMatch_Group(in, m, g);
    return s ? RValue_Str(in, s) : RValue_Null(in);
}

static RValue *re_bi_start(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid, g;
    RMatch *m;
    size_t v;

    (void)self;
    if (!want_args(in, "start", nargs, 2) ||
        !want_int(in, "start", args[0], &mid) ||
        !want_int(in, "start", args[1], &g))
        return RValue_Null(in);
    m = want_match(in, "start", mid);
    if (!m)
        return RValue_Null(in);
    v = RMatch_Start(m, (int)g);
    return RValue_Int(in, v == SIZE_MAX ? -1 : (int64_t)v);
}

static RValue *re_bi_end(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid, g;
    RMatch *m;
    size_t v;

    (void)self;
    if (!want_args(in, "end", nargs, 2) ||
        !want_int(in, "end", args[0], &mid) ||
        !want_int(in, "end", args[1], &g))
        return RValue_Null(in);
    m = want_match(in, "end", mid);
    if (!m)
        return RValue_Null(in);
    v = RMatch_End(m, (int)g);
    return RValue_Int(in, v == SIZE_MAX ? -1 : (int64_t)v);
}

static RValue *re_bi_startname(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    AsterString *name;
    RMatch *m;
    int g;
    size_t v;

    (void)self;
    if (!want_args(in, "startname", nargs, 2) ||
        !want_int(in, "startname", args[0], &mid))
        return RValue_Null(in);
    name = want_str(in, "startname", args[1]);
    if (!name)
        return RValue_Null(in);
    m = want_match(in, "startname", mid);
    if (!m)
        return RValue_Null(in);
    g = Regex_GroupIndex(m->re, name->data, name->length);
    if (g < 0)
        return RT_ThrowValue(in, "IndexError", "no such group");
    v = RMatch_Start(m, g);
    return RValue_Int(in, v == SIZE_MAX ? -1 : (int64_t)v);
}

static RValue *re_bi_endname(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    AsterString *name;
    RMatch *m;
    int g;
    size_t v;

    (void)self;
    if (!want_args(in, "endname", nargs, 2) ||
        !want_int(in, "endname", args[0], &mid))
        return RValue_Null(in);
    name = want_str(in, "endname", args[1]);
    if (!name)
        return RValue_Null(in);
    m = want_match(in, "endname", mid);
    if (!m)
        return RValue_Null(in);
    g = Regex_GroupIndex(m->re, name->data, name->length);
    if (g < 0)
        return RT_ThrowValue(in, "IndexError", "no such group");
    v = RMatch_End(m, g);
    return RValue_Int(in, v == SIZE_MAX ? -1 : (int64_t)v);
}

static RValue *re_bi_groupcount(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid;
    AsterRegex *re;

    (void)self;
    if (!want_args(in, "groupcount", nargs, 1) ||
        !want_int(in, "groupcount", args[0], &reid))
        return RValue_Null(in);
    re = want_re(in, "groupcount", reid);
    if (!re)
        return RValue_Null(in);
    return RValue_Int(in, re->ngroups);
}

static RValue *re_bi_groupindex(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid;
    AsterRegex *re;
    AsterDict *d;
    int i;

    (void)self;
    if (!want_args(in, "groupindex", nargs, 1) ||
        !want_int(in, "groupindex", args[0], &reid))
        return RValue_Null(in);
    re = want_re(in, "groupindex", reid);
    if (!re)
        return RValue_Null(in);
    d = Dict_New(in);
    for (i = 0; i < re->nnames; i++) {
        int ok = 0;
        Dict_Set(in, d,
                 RValue_Str(in, AsterString_FromCStr(re->names[i].name)),
                 RValue_Int(in, re->names[i].index), &ok);
        if (!ok)
            return RValue_Null(in);
    }
    return RValue_Dict(in, d);
}

static RValue *re_bi_pattern(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid;
    AsterRegex *re;

    (void)self;
    if (!want_args(in, "pattern", nargs, 1) ||
        !want_int(in, "pattern", args[0], &reid))
        return RValue_Null(in);
    re = want_re(in, "pattern", reid);
    if (!re)
        return RValue_Null(in);
    return RValue_Str(in, AsterString_FromUTF8(re->pattern, re->plen));
}

static RValue *re_bi_flags(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid;
    AsterRegex *re;

    (void)self;
    if (!want_args(in, "flags", nargs, 1) ||
        !want_int(in, "flags", args[0], &reid))
        return RValue_Null(in);
    re = want_re(in, "flags", reid);
    if (!re)
        return RValue_Null(in);
    return RValue_Int(in, re->flags);
}

static RValue *re_bi_lastindex(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    RMatch *m;
    int g, last = -1;

    (void)self;
    if (!want_args(in, "lastindex", nargs, 1) ||
        !want_int(in, "lastindex", args[0], &mid))
        return RValue_Null(in);
    m = want_match(in, "lastindex", mid);
    if (!m)
        return RValue_Null(in);
    for (g = 1; g <= m->re->ngroups; g++) {
        if (m->slots[2 * g] != SIZE_MAX)
            last = g;
    }
    return RValue_Int(in, last);
}

static RValue *re_bi_lastgroup(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    RMatch *m;
    int g, i, last = -1;

    (void)self;
    if (!want_args(in, "lastgroup", nargs, 1) ||
        !want_int(in, "lastgroup", args[0], &mid))
        return RValue_Null(in);
    m = want_match(in, "lastgroup", mid);
    if (!m)
        return RValue_Null(in);
    for (g = 1; g <= m->re->ngroups; g++) {
        if (m->slots[2 * g] != SIZE_MAX)
            last = g;
    }
    if (last < 0)
        return RValue_Null(in);
    for (i = 0; i < m->re->nnames; i++) {
        if (m->re->names[i].index == last)
            return RValue_Str(in, AsterString_FromCStr(m->re->names[i].name));
    }
    return RValue_Null(in);
}

static RValue *re_bi_release(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;

    (void)self;
    if (!want_args(in, "release", nargs, 1) ||
        !want_int(in, "release", args[0], &mid))
        return RValue_Null(in);
    Regex_ReleaseMatch(in, (int)mid);
    return RValue_Null(in);
}

static void put_group_bytes(Buf *out, RMatch *m, int g) {
    size_t s = (g >= 0 && g <= m->re->ngroups) ? m->slots[2 * g] : SIZE_MAX;
    size_t e = (g >= 0 && g <= m->re->ngroups) ? m->slots[2 * g + 1] : SIZE_MAX;
    size_t b1, b2;
    if (s == SIZE_MAX || e == SIZE_MAX)
        return;
    b1 = m->subject->length ? AsterString_ByteOffset(m->subject, s) : 0;
    b2 = m->subject->length ? AsterString_ByteOffset(m->subject, e) : 0;
    buf_put(out, m->subject->data + b1, b2 - b1);
}

static int expand_into(Interp *in, Buf *out, RMatch *m, const char *t, size_t n) {
    size_t i = 0;
    while (i < n) {
        char c = t[i];
        if (c == '\\') {
            size_t start = i;
            char d;
            i++;
            if (i >= n) {
                RT_Throw(in, "RegexError", "bad escape (end of pattern) in replacement");
                return 0;
            }
            d = t[i];
            if (d == 'g' && i + 1 < n && t[i + 1] == '<') {
                size_t j = i + 2, g;
                int idx = -1;
                if (j >= n || t[j] == '>') {
                    RT_Throw(in, "RegexError", "missing group name in replacement");
                    return 0;
                }
                for (g = j; g < n && t[g] != '>'; g++)
                    ;
                if (g >= n) {
                    RT_Throw(in, "RegexError", "missing > in replacement");
                    return 0;
                }
                if (t[j] >= '0' && t[j] <= '9') {
                    long num = 0;
                    for (g = j; g < n && t[g] != '>'; g++)
                        num = num * 10 + (t[g] - '0');
                    idx = (int)num;
                }
                else {
                    idx = Regex_GroupIndex(m->re, t + j, g - j);
                }
                if (idx < 0 || idx > m->re->ngroups) {
                    RT_Throw(in, "RegexError", "invalid group reference in replacement");
                    return 0;
                }
                put_group_bytes(out, m, idx);
                i = g + 1;
            }
            else if (d >= '0' && d <= '9') {
                long num = 0;
                int cnt = 0;
                while (i < n && cnt < 2 && t[i] >= '0' && t[i] <= '9') {
                    num = num * 10 + (t[i] - '0');
                    i++;
                    cnt++;
                }
                if (num > m->re->ngroups) {
                    RT_Throw(in, "RegexError", "invalid group reference %ld at position %zu",
                             num, start);
                    return 0;
                }
                put_group_bytes(out, m, (int)num);
            }
            else if (d == 'n') { buf_put(out, "\n", 1); i++; }
            else if (d == 'r') { buf_put(out, "\r", 1); i++; }
            else if (d == 't') { buf_put(out, "\t", 1); i++; }
            else if (d == 'f') { buf_put(out, "\f", 1); i++; }
            else if (d == 'v') { buf_put(out, "\v", 1); i++; }
            else if (d == 'a') { buf_put(out, "\a", 1); i++; }
            else if (d == '\\') { buf_put(out, "\\", 1); i++; }
            else if (d == '$') { buf_put(out, "$", 1); i++; }
            else if (ascii_letter((unsigned char)d)) {
                RT_Throw(in, "RegexError", "bad escape \\%c at position %zu", d, start);
                return 0;
            }
            else {
                buf_put(out, &d, 1);
                i++;
            }
        }
        else {
            buf_put(out, &c, 1);
            i++;
        }
    }
    return 1;
}

static RValue *re_bi_expand(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long mid;
    RMatch *m;
    AsterString *tmpl;
    Buf out;

    (void)self;
    memset(&out, 0, sizeof out);
    if (!want_args(in, "expand", nargs, 2) ||
        !want_int(in, "expand", args[0], &mid))
        return RValue_Null(in);
    tmpl = want_str(in, "expand", args[1]);
    if (!tmpl)
        return RValue_Null(in);
    m = want_match(in, "expand", mid);
    if (!m)
        return RValue_Null(in);
    if (!expand_into(in, &out, m, tmpl->data, tmpl->length)) {
        free(out.data);
        return RValue_Null(in);
    }
    return buf_value(in, &out);
}

/* finditer와 같은 규칙: 빈 매치 뒤에는 한 칸 진행한다. */
typedef struct {
    Interp *in;
    AsterRegex *re;
    Subject sub;
    Vm vm;
    size_t pos;
} MatchIter;

static void mit_init(MatchIter *it, Interp *in, AsterRegex *re, AsterString *s) {
    memset(it, 0, sizeof(*it));
    it->in = in;
    it->re = re;
    subject_init(&it->sub, s);
    vm_init(&it->vm, re, &it->sub);
}

static void mit_free(MatchIter *it) {
    vm_free(&it->vm);
    subject_free(&it->sub);
}

static int mit_next(MatchIter *it, size_t *slots) {
    while (it->pos <= it->sub.len) {
        size_t s, ms, me;
        int found = 0;
        for (s = it->pos; s <= it->sub.len; s++) {
            size_t end = 0;
            vm_prepare(&it->vm);
            if (vm_run(&it->vm, 0, s, 0, 0, &end)) {
                found = 1;
                break;
            }
            if (it->vm.hard)
                return -1;
        }
        if (!found)
            return 0;
        memcpy(slots, it->vm.slots, it->re->nslots * sizeof(size_t));
        ms = slots[0];
        me = slots[1];
        it->pos = (me == ms) ? me + 1 : me;
        return 1;
    }
    return 0;
}

static RValue *re_bi_findall(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid;
    AsterString *subject;
    AsterRegex *re;
    MatchIter it;
    size_t *slots;
    AsterList *out;
    int r;

    (void)self;
    if (!want_args(in, "findall", nargs, 2) ||
        !want_int(in, "findall", args[0], &reid))
        return RValue_Null(in);
    subject = want_str(in, "findall", args[1]);
    if (!subject)
        return RValue_Null(in);
    re = want_re(in, "findall", reid);
    if (!re)
        return RValue_Null(in);
    it.re = NULL;
    mit_init(&it, in, re, subject);
    slots = xmalloc(re->nslots * sizeof(size_t));
    out = List_New(in);
    while ((r = mit_next(&it, slots)) == 1) {
        if (re->ngroups == 0) {
            List_Append(in, out, substr_value(in, subject, slots[0], slots[1]));
        }
        else if (re->ngroups == 1) {
            AsterString *g = RMatch_Group(in, &(RMatch){re, subject, slots}, 1);
            List_Append(in, out, RValue_Str(in, g ? g : AsterString_FromCStr("")));
        }
        else {
            AsterTuple *t = Tuple_New(in, (size_t)re->ngroups);
            int g;
            for (g = 1; g <= re->ngroups; g++) {
                AsterString *gs = RMatch_Group(in, &(RMatch){re, subject, slots}, g);
                t->items[g - 1] = RValue_Str(in, gs ? gs : AsterString_FromCStr(""));
            }
            List_Append(in, out, RValue_Tuple(in, t));
        }
    }
    free(slots);
    mit_free(&it);
    if (r < 0) {
        RT_Throw(in, "RuntimeError", "regular expression backtracking limit exceeded");
        return RValue_Null(in);
    }
    return RValue_List(in, out);
}

static RValue *re_bi_split(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid, maxsplit;
    AsterString *subject;
    AsterRegex *re;
    MatchIter it;
    size_t *slots;
    AsterList *out;
    size_t last = 0;
    long count = 0;
    int r = 0;

    (void)self;
    if (!want_args(in, "split", nargs, 3) ||
        !want_int(in, "split", args[0], &reid) ||
        !want_int(in, "split", args[2], &maxsplit))
        return RValue_Null(in);
    subject = want_str(in, "split", args[1]);
    if (!subject)
        return RValue_Null(in);
    re = want_re(in, "split", reid);
    if (!re)
        return RValue_Null(in);
    mit_init(&it, in, re, subject);
    slots = xmalloc(re->nslots * sizeof(size_t));
    out = List_New(in);
    while ((maxsplit <= 0 || count < maxsplit) && (r = mit_next(&it, slots)) == 1) {
        int g;
        List_Append(in, out, substr_value(in, subject, last, slots[0]));
        for (g = 1; g <= re->ngroups; g++) {
            AsterString *gs = RMatch_Group(in, &(RMatch){re, subject, slots}, g);
            List_Append(in, out, RValue_Str(in, gs ? gs : AsterString_FromCStr("")));
        }
        last = slots[1];
        count++;
    }
    if (r >= 0)
        List_Append(in, out, substr_value(in, subject, last, subject->cp_length));
    free(slots);
    mit_free(&it);
    if (r < 0) {
        RT_Throw(in, "RuntimeError", "regular expression backtracking limit exceeded");
        return RValue_Null(in);
    }
    return RValue_List(in, out);
}

static RValue *re_bi_sub(Interp *in, RValue *self, RValue **args, size_t nargs) {
    long reid, countlimit;
    AsterString *subject;
    AsterString *tmpl;
    AsterRegex *re;
    MatchIter it;
    size_t *slots;
    Buf out;
    size_t last = 0;
    long count = 0;
    int r;

    (void)self;
    memset(&out, 0, sizeof out);
    if (!want_args(in, "sub", nargs, 4) ||
        !want_int(in, "sub", args[0], &reid) ||
        !want_int(in, "sub", args[3], &countlimit))
        return RValue_Null(in);
    subject = want_str(in, "sub", args[1]);
    tmpl = want_str(in, "sub", args[2]);
    if (!subject || !tmpl)
        return RValue_Null(in);
    re = want_re(in, "sub", reid);
    if (!re)
        return RValue_Null(in);
    mit_init(&it, in, re, subject);
    slots = xmalloc(re->nslots * sizeof(size_t));
    while ((countlimit == 0 || count < countlimit) && (r = mit_next(&it, slots)) == 1) {
        RMatch view;
        size_t b1, b2;
        view.re = re;
        view.subject = subject;
        view.slots = slots;
        b1 = AsterString_ByteOffset(subject, last);
        b2 = AsterString_ByteOffset(subject, slots[0]);
        buf_put(&out, subject->data + b1, b2 - b1);
        if (!expand_into(in, &out, &view, tmpl->data, tmpl->length)) {
            free(slots);
            mit_free(&it);
            free(out.data);
            return RValue_Null(in);
        }
        last = slots[1];
        count++;
    }
    if (r == 0) {
        size_t b1 = AsterString_ByteOffset(subject, last);
        buf_put(&out, subject->data + b1, subject->length - b1);
    }
    free(slots);
    mit_free(&it);
    if (r < 0) {
        free(out.data);
        RT_Throw(in, "RuntimeError", "regular expression backtracking limit exceeded");
        return RValue_Null(in);
    }
    return buf_value(in, &out);
}

static RValue *re_bi_escape(Interp *in, RValue *self, RValue **args, size_t nargs) {
    static const char *special = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
    AsterString *s;
    Buf out;
    size_t i = 0;

    (void)self;
    if (!want_args(in, "escape", nargs, 1))
        return RValue_Null(in);
    s = want_str(in, "escape", args[0]);
    if (!s)
        return RValue_Null(in);
    memset(&out, 0, sizeof out);
    while (i < s->length) {
        size_t adv;
        uint32_t cp = Aster_UTF8_Decode(s->data + i, s->length - i, &adv);
        if (cp < 0x80 && cp != 0 && strchr(special, (int)cp)) {
            char bs = '\\';
            buf_put(&out, &bs, 1);
        }
        buf_put_cp(&out, cp);
        i += adv;
    }
    return buf_value(in, &out);
}

void Regex_Install(Interp *in) {
    AstModule *m = Module_RegisterBuiltin(in, "aster.util.regex.Native");

    if (!m)
        return;
    Env_Define(in, m->env, "compile",
               RValue_Builtin(in, Builtin_New(in, "compile", re_bi_compile)));
    Env_Define(in, m->env, "exec",
               RValue_Builtin(in, Builtin_New(in, "exec", re_bi_exec)));
    Env_Define(in, m->env, "group",
               RValue_Builtin(in, Builtin_New(in, "group", re_bi_group)));
    Env_Define(in, m->env, "groupname",
               RValue_Builtin(in, Builtin_New(in, "groupname", re_bi_groupname)));
    Env_Define(in, m->env, "start",
               RValue_Builtin(in, Builtin_New(in, "start", re_bi_start)));
    Env_Define(in, m->env, "end",
               RValue_Builtin(in, Builtin_New(in, "end", re_bi_end)));
    Env_Define(in, m->env, "startname",
               RValue_Builtin(in, Builtin_New(in, "startname", re_bi_startname)));
    Env_Define(in, m->env, "endname",
               RValue_Builtin(in, Builtin_New(in, "endname", re_bi_endname)));
    Env_Define(in, m->env, "groupcount",
               RValue_Builtin(in, Builtin_New(in, "groupcount", re_bi_groupcount)));
    Env_Define(in, m->env, "groupindex",
               RValue_Builtin(in, Builtin_New(in, "groupindex", re_bi_groupindex)));
    Env_Define(in, m->env, "pattern",
               RValue_Builtin(in, Builtin_New(in, "pattern", re_bi_pattern)));
    Env_Define(in, m->env, "flags",
               RValue_Builtin(in, Builtin_New(in, "flags", re_bi_flags)));
    Env_Define(in, m->env, "lastindex",
               RValue_Builtin(in, Builtin_New(in, "lastindex", re_bi_lastindex)));
    Env_Define(in, m->env, "lastgroup",
               RValue_Builtin(in, Builtin_New(in, "lastgroup", re_bi_lastgroup)));
    Env_Define(in, m->env, "release",
               RValue_Builtin(in, Builtin_New(in, "release", re_bi_release)));
    Env_Define(in, m->env, "expand",
               RValue_Builtin(in, Builtin_New(in, "expand", re_bi_expand)));
    Env_Define(in, m->env, "findall",
               RValue_Builtin(in, Builtin_New(in, "findall", re_bi_findall)));
    Env_Define(in, m->env, "split",
               RValue_Builtin(in, Builtin_New(in, "split", re_bi_split)));
    Env_Define(in, m->env, "sub",
               RValue_Builtin(in, Builtin_New(in, "sub", re_bi_sub)));
    Env_Define(in, m->env, "escape",
               RValue_Builtin(in, Builtin_New(in, "escape", re_bi_escape)));
}
