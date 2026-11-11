#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/object.h"

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("메모리가 부족합니다\n", stderr);
        abort();
    }
    return p;
}

AsterString *AsterString_FromUTF8(const char *s, size_t n)
{
    AsterString *o = xmalloc(sizeof(AsterString) + n + 1);
    o->ob.kind = ASTER_OBJ_STRING;
    o->length = n;
    if (n)
        memcpy(o->data, s, n);
    o->data[n] = 0;
    return o;
}

AsterString *AsterString_FromCStr(const char *s)
{
    return AsterString_FromUTF8(s, strlen(s));
}

void Aster_StringRepr(FILE *out, const char *s, size_t n)
{
    int quote = '\'';
    int has_single = 0, has_double = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\'')
            has_single = 1;
        else if (s[i] == '"')
            has_double = 1;
    }
    if (has_single && !has_double)
        quote = '"';

    fputc(quote, out);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == (unsigned char)quote || c == '\\') {
            fputc('\\', out);
            fputc(c, out);
        }
        else if (c == '\n') {
            fputs("\\n", out);
        }
        else if (c == '\r') {
            fputs("\\r", out);
        }
        else if (c == '\t') {
            fputs("\\t", out);
        }
        else if (c < 0x20 || c == 0x7f) {
            fprintf(out, "\\x%02x", c);
        }
        else {
            fputc(c, out);
        }
    }
    fputc(quote, out);
}

/* Python repr과 같이 왕복 변환이 되는 가장 짧은 표기를 찾는다. */
void Aster_FormatDouble(char *buf, size_t size, double value)
{
    int precision;

    for (precision = 1; precision <= 17; precision++) {
        snprintf(buf, size, "%.*g", precision, value);
        if (strtod(buf, NULL) == value)
            break;
    }
    if (!strpbrk(buf, ".eEn"))
        strcat(buf, ".0");
}

void Aster_Value_Repr(FILE *out, Value v)
{
    switch (v.tag) {
    case VT_NULL:
        fputs("None", out);
        break;
    case VT_BOOL:
        fputs(v.u.i ? "True" : "False", out);
        break;
    case VT_INT:
        fprintf(out, "%lld", (long long)v.u.i);
        break;
    case VT_FLOAT: {
        char buf[64];
        Aster_FormatDouble(buf, sizeof buf, v.u.f);
        fputs(buf, out);
        break;
    }
    case VT_OBJ:
        if (v.u.o && v.u.o->kind == ASTER_OBJ_STRING) {
            AsterString *s = (AsterString *)v.u.o;
            Aster_StringRepr(out, s->data, s->length);
        }
        else {
            fputs("<object>", out);
        }
        break;
    }
}
