#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/object.h"

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("out of memory\n", stderr);
        abort();
    }
    return p;
}

uint32_t Aster_UTF8_Decode(const char *s, size_t n, size_t *advance)
{
    unsigned char c = (unsigned char)s[0];
    uint32_t cp;

    if (c < 0x80) {
        *advance = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && n >= 2 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(c & 0x1F) << 6) | ((uint32_t)s[1] & 0x3F);
        *advance = cp < 0x80 ? 1 : 2;
        return cp < 0x80 ? c : cp;
    }
    if ((c & 0xF0) == 0xE0 && n >= 3 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(c & 0x0F) << 12) | (((uint32_t)s[1] & 0x3F) << 6) |
             ((uint32_t)s[2] & 0x3F);
        if (cp >= 0x800) {
            *advance = 3;
            return cp;
        }
    }
    if ((c & 0xF8) == 0xF0 && n >= 4 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(c & 0x07) << 18) | (((uint32_t)s[1] & 0x3F) << 12) |
             (((uint32_t)s[2] & 0x3F) << 6) | ((uint32_t)s[3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            *advance = 4;
            return cp;
        }
    }
    *advance = 1;
    return c;
}

size_t Aster_UTF8_Encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static size_t count_code_points(const char *s, size_t n)
{
    size_t i = 0, count = 0;
    while (i < n) {
        size_t adv;
        Aster_UTF8_Decode(s + i, n - i, &adv);
        i += adv;
        count++;
    }
    return count;
}

size_t AsterString_ByteOffset(const AsterString *s, size_t cp_index)
{
    size_t i = 0, cp = 0;
    while (i < s->length && cp < cp_index) {
        size_t adv;
        Aster_UTF8_Decode(s->data + i, s->length - i, &adv);
        i += adv;
        cp++;
    }
    return i;
}

size_t AsterString_CodePointIndex(const AsterString *s, size_t byte_offset)
{
    size_t i = 0, cp = 0;
    if (byte_offset > s->length)
        byte_offset = s->length;
    while (i < byte_offset) {
        size_t adv;
        Aster_UTF8_Decode(s->data + i, s->length - i, &adv);
        i += adv;
        cp++;
    }
    return cp;
}

uint32_t Aster_ToUpperCP(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z')
        return cp - 0x20;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
        return cp - 0x20;
    if (cp == 0xFF)
        return 0x178;
    if (cp == 0x131)
        return 'I';
    if (cp == 0x17F)
        return 'S';
    if (cp >= 0x100 && cp <= 0x137)
        return (cp % 2 == 0) ? cp : cp - 1;
    if (cp >= 0x139 && cp <= 0x148)
        return (cp % 2 == 1) ? cp : cp - 1;
    if (cp == 0x149 || cp == 0x138)
        return cp;
    if (cp >= 0x14A && cp <= 0x177)
        return (cp % 2 == 0) ? cp : cp - 1;
    if (cp >= 0x179 && cp <= 0x17E)
        return (cp % 2 == 1) ? cp : cp - 1;
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2)
        return cp;
    if (cp >= 0x3B1 && cp <= 0x3C9)
        return cp == 0x3C2 ? 0x3A3 : cp - 0x20;
    if (cp >= 0x430 && cp <= 0x44F)
        return cp - 0x20;
    if (cp >= 0x450 && cp <= 0x45F)
        return cp - 0x50;
    return cp;
}

uint32_t Aster_ToLowerCP(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z')
        return cp + 0x20;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
        return cp + 0x20;
    if (cp == 0x178)
        return 0xFF;
    if (cp >= 0x100 && cp <= 0x137)
        return (cp % 2 == 1) ? cp : cp + 1;
    if (cp == 0x130)
        return cp;
    if (cp >= 0x139 && cp <= 0x148)
        return (cp % 2 == 0) ? cp : cp + 1;
    if (cp == 0x149 || cp == 0x138)
        return cp;
    if (cp >= 0x14A && cp <= 0x177)
        return (cp % 2 == 1) ? cp : cp + 1;
    if (cp >= 0x179 && cp <= 0x17E)
        return (cp % 2 == 0) ? cp : cp + 1;
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2)
        return cp + 0x20;
    if (cp >= 0x410 && cp <= 0x42F)
        return cp + 0x20;
    if (cp >= 0x400 && cp <= 0x40F)
        return cp + 0x50;
    return cp;
}

int Aster_IsSpaceCP(uint32_t cp)
{
    return (cp >= 0x09 && cp <= 0x0D) || cp == 0x20 || cp == 0x85 ||
           cp == 0xA0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000;
}

AsterString *AsterString_FromUTF8(const char *s, size_t n)
{
    AsterString *o = xmalloc(sizeof(AsterString) + n + 1);
    o->ob.kind = ASTER_OBJ_STRING;
    o->length = n;
    o->cp_length = count_code_points(s, n);
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
    char *e;

    for (precision = 1; precision <= 17; precision++) {
        snprintf(buf, size, "%.*g", precision, value);
        if (strtod(buf, NULL) == value)
            break;
    }
    /* Python repr처럼 지수가 -4..15 범위면 일반 표기를 쓴다. */
    e = strpbrk(buf, "eE");
    if (e) {
        int exp = atoi(e + 1);
        if (exp >= -4 && exp < 16) {
            int decimals = precision - 1 - exp;
            if (decimals < 0)
                decimals = 0;
            snprintf(buf, size, "%.*f", decimals, value);
        }
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
