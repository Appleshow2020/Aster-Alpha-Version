/* Aster 렉서. Grammar/Tokens와 Grammar/aster.gram의 어휘 규칙을 따른다. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/lexer.h"

typedef struct {
    const char *src;
    size_t n;
    size_t pos;
    int line, col;
    AsterTokenList *out;
    AsterError *err;
} Lexer;

/* ---------------------------------------------------------------- 공통 */

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) {
        fputs("out of memory\n", stderr);
        abort();
    }
    return q;
}

/*
 * pos부터 end까지 소비하면서 줄/열을 갱신한다.
 * UTF-8 연속 바이트(0x80~0xBF)는 열을 올리지 않는다 (문자 단위 열).
 */
static void advance_to(Lexer *lx, size_t end) {
    const unsigned char *s = (const unsigned char *)lx->src;
    while (lx->pos < end) {
        unsigned char c = s[lx->pos];
        if (c == '\n') {
            lx->line++;
            lx->col = 0;
        }
        else if ((c & 0xC0) != 0x80) {
            lx->col++;
        }
        lx->pos++;
    }
}

static int fail_here(Lexer *lx, int span, const char *fmt, ...) {
    va_list ap;
    lx->err->line = lx->line;
    lx->err->col = lx->col;
    lx->err->length = span;
    va_start(ap, fmt);
    vsnprintf(lx->err->message, sizeof lx->err->message, fmt, ap);
    va_end(ap);
    return 0;
}

static int fail_at(Lexer *lx, size_t offset, int span, const char *fmt, ...) {
    va_list ap;
    if (offset > lx->pos)
        advance_to(lx, offset);
    lx->err->line = lx->line;
    lx->err->col = lx->col;
    lx->err->length = span;
    va_start(ap, fmt);
    vsnprintf(lx->err->message, sizeof lx->err->message, fmt, ap);
    va_end(ap);
    return 0;
}

static int push_token(Lexer *lx, int type, size_t start, int sline, int scol) {
    AsterTokenList *l = lx->out;
    AsterToken *t;
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->items = xrealloc(l->items, l->cap * sizeof *l->items);
    }
    t = &l->items[l->count++];
    t->type = type;
    t->start = lx->src + start;
    t->length = (int)(lx->pos - start);
    t->line = sline;
    t->col = scol;
    t->end_line = lx->line;
    t->end_col = lx->col;
    return 1;
}

/* ------------------------------------------------------------- 문자 분류 */

static int is_ascii_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_ascii_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

static int is_ascii_id_continue(unsigned char c) {
    return is_ascii_alpha(c) || is_ascii_digit(c) || c == '_';
}

/* 이 자리에서 시작하는 UTF-8 문자의 바이트 수. 올바르지 않으면 -1. */
static int utf8_len(const char *s, size_t n) {
    unsigned char c = (unsigned char)s[0];
    int len, i;
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        len = 2;
    else if ((c & 0xF0) == 0xE0)
        len = 3;
    else if ((c & 0xF8) == 0xF0)
        len = 4;
    else
        return -1;
    if (n < (size_t)len)
        return -1;
    for (i = 1; i < len; i++)
        if ((s[i] & 0xC0) != 0x80)
            return -1;
    return len;
}

/* 이름 시작 문자인가? (ASCII 영문/밑줄 또는 비ASCII UTF-8 문자) */
static int name_start(const char *s, size_t n, int *len) {
    unsigned char c = (unsigned char)s[0];
    int l;
    if (is_ascii_alpha(c) || c == '_') {
        *len = 1;
        return 1;
    }
    l = utf8_len(s, n);
    if (l > 1) {
        *len = l;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- 주석 */

static int skip_trivia(Lexer *lx) {
    for (;;) {
        unsigned char c;
        if (lx->pos >= lx->n)
            return 1;
        c = (unsigned char)lx->src[lx->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\f') {
            advance_to(lx, lx->pos + 1);
            continue;
        }
        if (c == '#') {
            size_t j = lx->pos + 1;
            while (j < lx->n && lx->src[j] != '\n')
                j++;
            advance_to(lx, j);
            continue;
        }
        if (c == '/' && lx->pos + 1 < lx->n && lx->src[lx->pos + 1] == '*') {
            size_t j = lx->pos + 2;
            while (j + 1 < lx->n && !(lx->src[j] == '*' && lx->src[j + 1] == '/'))
                j++;
            if (j + 1 >= lx->n)
                return fail_here(lx, 2, "Unterminated block comment");
            advance_to(lx, j + 2);
            continue;
        }
        return 1;
    }
}

/* --------------------------------------------------------------- 토큰 */

static int scan_name(Lexer *lx) {
    size_t j = lx->pos;
    int len = 0;
    name_start(lx->src + j, lx->n - j, &len);
    j += (size_t)len;
    while (j < lx->n) {
        unsigned char c = (unsigned char)lx->src[j];
        if (is_ascii_id_continue(c)) {
            j++;
            continue;
        }
        len = utf8_len(lx->src + j, lx->n - j);
        if (len > 1)
            j += (size_t)len;
        else
            break;
    }
    advance_to(lx, j);
    return 1;
}

static int scan_number(Lexer *lx, int *type) {
    size_t i = lx->pos, j = i;
    int is_float = 0;

    while (j < lx->n && is_ascii_digit((unsigned char)lx->src[j]))
        j++;
    if (j + 1 < lx->n && lx->src[j] == '.' && is_ascii_digit((unsigned char)lx->src[j + 1])) {
        is_float = 1;
        j++;
        while (j < lx->n && is_ascii_digit((unsigned char)lx->src[j]))
            j++;
    }
    if (j < lx->n) {
        unsigned char c = (unsigned char)lx->src[j];
        if (is_ascii_id_continue(c) || utf8_len(lx->src + j, lx->n - j) > 1)
            return fail_at(lx, i, (int)(j - i + 1),
                           "An identifier character cannot immediately follow a number");
    }
    advance_to(lx, j);
    *type = is_float ? TOK_FLOAT : TOK_INT;
    return 1;
}

/* 이스케이프 하나를 검사한다. 성공하면 다음 위치를 *next에 둔다.
 * 실패하면 msg에 이유를 쓰고 0을 돌려준다 (위치는 호출자가 정한다). */
static int scan_escape(Lexer *lx, size_t j, size_t *next, char *msg, size_t msgcap) {
    const char *s = lx->src;
    size_t n = lx->n;
    size_t k = j + 1;
    char c;

    if (k >= n || s[k] == '\n') {
        snprintf(msg, msgcap, "There is no character immediately after '\\\\'");
        return 0;
    }
    c = s[k];
    if (strchr("ntrabfv\\'\"", c)) {
        *next = k + 1;
        return 1;
    }
    if (c >= '0' && c <= '7') {
        size_t m = k + 1;
        int count = 1;
        while (m < n && count < 3 && s[m] >= '0' && s[m] <= '7') {
            m++;
            count++;
        }
        *next = m;
        return 1;
    }
    if (c == 'x' || c == 'u' || c == 'U') {
        int width = c == 'x' ? 2 : c == 'u' ? 4 : 8;
        unsigned value = 0;
        int i;
        for (i = 0; i < width; i++) {
            char d;
            if (k + 1 + (size_t)i >= n) {
                snprintf(msg, msgcap, "'\\%c' must be followed by %d hexadecimal digits", c, width);
                return 0;
            }
            d = s[k + 1 + (size_t)i];
            if (d >= '0' && d <= '9')
                value = value * 16 + (unsigned)(d - '0');
            else if (d >= 'a' && d <= 'f')
                value = value * 16 + (unsigned)(d - 'a' + 10);
            else if (d >= 'A' && d <= 'F')
                value = value * 16 + (unsigned)(d - 'A' + 10);
            else {
                snprintf(msg, msgcap, "'\\%c' must be followed by %d hexadecimal digits", c, width);
                return 0;
            }
        }
        if (value > 0x10FFFFu) {
            snprintf(msg, msgcap, "the escape sequence is outside the Unicode range");
            return 0;
        }
        *next = k + 1 + (size_t)width;
        return 1;
    }
    if (c == 'N') {
        size_t m;
        if (k + 1 >= n || s[k + 1] != '{') {
            snprintf(msg, msgcap, "'\\N' must be followed by '{name}'");
            return 0;
        }
        m = k + 2;
        while (m < n && s[m] != '}' && s[m] != '\n')
            m++;
        if (m >= n || s[m] != '}') {
            snprintf(msg, msgcap, "'\\N{' is not closed by '}'");
            return 0;
        }
        snprintf(msg, msgcap,
                 "Unicode name escapes ('\\N{...}') are not supported by the C front end yet");
        return 0;
    }
    snprintf(msg, msgcap, "Unknown escape sequence: '\\%c'", c);
    return 0;
}

static int scan_string(Lexer *lx) {
    size_t i = lx->pos, j = i + 1;
    char quote = lx->src[i];

    for (;;) {
        if (j >= lx->n || lx->src[j] == '\n')
            return fail_here(lx, (int)(j - i), "Unterminated string literal");
        if (lx->src[j] == quote) {
            j++;
            break;
        }
        if (lx->src[j] == '\\') {
            size_t next;
            char msg[160];
            if (!scan_escape(lx, j, &next, msg, sizeof msg))
                return fail_at(lx, j, 2, "%s", msg);
            j = next;
        }
        else {
            j++;
        }
    }
    advance_to(lx, j);
    return 1;
}

static int scan_operator(Lexer *lx, int *type) {
    const char *s = lx->src;
    size_t i = lx->pos;
    size_t rem = lx->n - i;
    int k;

    if (rem >= 2 && s[i] == '&' && s[i + 1] == '&')
        return fail_here(lx, 2, "'&&' is not used. Use 'and' for logical conjunction");
    if (rem >= 2 && s[i] == '|' && s[i + 1] == '|')
        return fail_here(lx, 2, "'||' is not used. Use 'or' for logical disjunction");

    for (k = 0; k < _AsterToken_ExactCount; k++) {
        const ExactToken *e = &_AsterToken_Exact[k];
        size_t len = strlen(e->text);
        if (len <= rem && memcmp(s + i, e->text, len) == 0) {
            advance_to(lx, i + len);
            *type = e->type;
            return 1;
        }
    }

    switch (s[i]) {
    case '!':
        return fail_here(lx, 1, "'!' is not used. Use 'not' for logical negation ('!=' is allowed)");
    default: {
        unsigned char c = (unsigned char)s[i];
        char repr[32];
        if (c >= 0x20 && c < 0x7f)
            snprintf(repr, sizeof repr, "'%c'", c);
        else
            snprintf(repr, sizeof repr, "byte 0x%02x", c);
        return fail_here(lx, 1, "Unknown character: %s", repr);
    }
    }
}

/* ------------------------------------------------------------- 엔트리 */

int Aster_Tokenize(const char *source, size_t length, AsterTokenList *out, AsterError *err) {
    char *buf;
    size_t i = 0, m = 0;
    Lexer lx;

    memset(out, 0, sizeof *out);
    memset(err, 0, sizeof *err);

    buf = xrealloc(NULL, length + 1);
    if (length >= 3 && (unsigned char)source[0] == 0xEF &&
        (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF)
        i = 3;
    while (i < length) {
        char c = source[i++];
        if (c == '\r') {
            buf[m++] = '\n';
            if (i < length && source[i] == '\n')
                i++;
        }
        else {
            buf[m++] = c;
        }
    }
    buf[m] = 0;
    out->source = buf;
    out->source_length = m;

    lx.src = buf;
    lx.n = m;
    lx.pos = 0;
    lx.line = 1;
    lx.col = 0;
    lx.out = out;
    lx.err = err;

    for (;;) {
        int len, type;
        size_t start;
        int sline, scol;
        unsigned char c;

        if (!skip_trivia(&lx))
            return 0;
        if (lx.pos >= lx.n)
            break;

        start = lx.pos;
        sline = lx.line;
        scol = lx.col;
        c = (unsigned char)buf[start];

        if (name_start(buf + start, lx.n - start, &len)) {
            if (!scan_name(&lx))
                return 0;
            type = TOK_NAME;
        }
        else if (is_ascii_digit(c)) {
            if (!scan_number(&lx, &type))
                return 0;
        }
        else if (c == '"' || c == '\'') {
            if (!scan_string(&lx))
                return 0;
            type = TOK_STRING;
        }
        else {
            if (!scan_operator(&lx, &type))
                return 0;
        }
        push_token(&lx, type, start, sline, scol);
    }

    {
        AsterTokenList *l = out;
        if (l->count == l->cap) {
            l->cap = l->cap ? l->cap * 2 : 64;
            l->items = xrealloc(l->items, l->cap * sizeof *l->items);
        }
        l->items[l->count].type = TOK_ENDMARKER;
        l->items[l->count].start = buf + m;
        l->items[l->count].length = 0;
        l->items[l->count].line = lx.line;
        l->items[l->count].col = lx.col;
        l->items[l->count].end_line = lx.line;
        l->items[l->count].end_col = lx.col;
        l->count++;
    }
    return 1;
}

void Aster_TokenList_Clear(AsterTokenList *list) {
    free(list->items);
    free(list->source);
    memset(list, 0, sizeof *list);
}

/* ------------------------------------------------------------ 문자열 해독 */

static int put_utf8(char *p, unsigned cp) {
    if (cp < 0x80) {
        p[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        p[0] = (char)(0xC0 | (cp >> 6));
        p[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        p[0] = (char)(0xE0 | (cp >> 12));
        p[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        p[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    p[0] = (char)(0xF0 | (cp >> 18));
    p[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    p[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    p[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

char *Aster_DecodeString(const char *raw, int length, int *out_length, AsterError *err) {
    char *out;
    size_t j, end, o = 0;

    memset(err, 0, sizeof *err);
    if (length < 2 || raw[0] != raw[length - 1] || (raw[0] != '"' && raw[0] != '\'')) {
        snprintf(err->message, sizeof err->message, "Not a string literal");
        return NULL;
    }
    out = xrealloc(NULL, (size_t)length + 4);
    j = 1;
    end = (size_t)length - 1;
    while (j < end) {
        char c = raw[j];
        if (c != '\\') {
            out[o++] = c;
            j++;
            continue;
        }
        {
            size_t k = j + 1;
            char e;
            if (k >= end) {
                snprintf(err->message, sizeof err->message,
                         "There is no character immediately after '\\\\'");
                free(out);
                return NULL;
            }
            e = raw[k];
            if (e == 'n') { out[o++] = '\n'; j = k + 1; }
            else if (e == 't') { out[o++] = '\t'; j = k + 1; }
            else if (e == 'r') { out[o++] = '\r'; j = k + 1; }
            else if (e == 'a') { out[o++] = '\a'; j = k + 1; }
            else if (e == 'b') { out[o++] = '\b'; j = k + 1; }
            else if (e == 'f') { out[o++] = '\f'; j = k + 1; }
            else if (e == 'v') { out[o++] = '\v'; j = k + 1; }
            else if (e == '\\' || e == '\'' || e == '"') { out[o++] = e; j = k + 1; }
            else if (e >= '0' && e <= '7') {
                unsigned value = 0;
                size_t m = k;
                while (m < end && m < k + 3 && raw[m] >= '0' && raw[m] <= '7')
                    value = value * 8 + (unsigned)(raw[m++] - '0');
                o += (size_t)put_utf8(out + o, value);
                j = m;
            }
            else if (e == 'x' || e == 'u' || e == 'U') {
                int width = e == 'x' ? 2 : e == 'u' ? 4 : 8;
                unsigned value = 0;
                int i;
                for (i = 0; i < width; i++) {
                    char d = raw[k + 1 + (size_t)i];
                    if (d >= '0' && d <= '9') value = value * 16 + (unsigned)(d - '0');
                    else if (d >= 'a' && d <= 'f') value = value * 16 + (unsigned)(d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F') value = value * 16 + (unsigned)(d - 'A' + 10);
                    else {
                        snprintf(err->message, sizeof err->message,
                                 "'\\%c' must be followed by %d hexadecimal digits", e, width);
                        free(out);
                        return NULL;
                    }
                }
                o += (size_t)put_utf8(out + o, value);
                j = k + 1 + (size_t)width;
            }
            else {
                snprintf(err->message, sizeof err->message,
                         "Unknown escape sequence: '\\%c'", e);
                free(out);
                return NULL;
            }
        }
    }
    out[o] = 0;
    *out_length = (int)o;
    return out;
}
