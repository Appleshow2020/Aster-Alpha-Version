#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* clock_gettime/nanosleep/getcwd 선언을 위해 시스템 헤더보다 먼저 정의한다. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/runtime.h"

#if !defined(_WIN32)
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#endif

#define MAX_DEPTH 64

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("out of memory\n", stderr);
        abort();
    }
    return q;
}

/* ---- 값 생성 ---- */

RValue *RValue_New(Interp *in, RKind kind)
{
    RValue *v = Arena_Alloc(in->arena, sizeof(RValue));
    v->kind = kind;
    return v;
}

RValue *RValue_Null(Interp *in)
{
    return RValue_New(in, RV_NULL);
}

RValue *RValue_Int(Interp *in, int64_t i)
{
    RValue *v = RValue_New(in, RV_INT);
    v->u.i = i;
    return v;
}

RValue *RValue_Float(Interp *in, double f)
{
    RValue *v = RValue_New(in, RV_FLOAT);
    v->u.f = f;
    return v;
}

RValue *RValue_Bool(Interp *in, int b)
{
    RValue *v = RValue_New(in, RV_BOOL);
    v->u.b = b ? 1 : 0;
    return v;
}

RValue *RValue_Str(Interp *in, AsterString *s)
{
    RValue *v = RValue_New(in, RV_STR);
    v->u.s = s;
    return v;
}

RValue *RValue_Object(Interp *in, AstInstance *obj)
{
    RValue *v = RValue_New(in, RV_OBJECT);
    v->u.obj = obj;
    return v;
}

RValue *RValue_Struct(Interp *in, AstStructValue *stv)
{
    RValue *v = RValue_New(in, RV_STRUCT);
    v->u.stv = stv;
    return v;
}

RValue *RValue_Func(Interp *in, AstFn *fn)
{
    RValue *v = RValue_New(in, RV_FUNC);
    v->u.fn = fn;
    return v;
}

RValue *RValue_Builtin(Interp *in, AstBuiltin *builtin)
{
    RValue *v = RValue_New(in, RV_BUILTIN);
    v->u.builtin = builtin;
    return v;
}

RValue *RValue_Ptr(Interp *in, RValue **slot, typeref_ty ty)
{
    RValue *v = RValue_New(in, RV_PTR);
    v->u.ptr.slot = slot;
    v->u.ptr.ty = ty;
    return v;
}

RValue *RValue_Exception(Interp *in, const char *name, RValue *message)
{
    AstException *e = Arena_Alloc(in->arena, sizeof(AstException));
    RValue *v = RValue_New(in, RV_EXC);
    e->name = name;
    e->message = message ? message : RValue_Null(in);
    v->u.exc = e;
    return v;
}

RValue *RValue_ExcClass(Interp *in, const char *name)
{
    RValue *v = RValue_New(in, RV_EXC_CLASS);
    v->u.exc_name = name;
    return v;
}

RValue *RValue_Class(Interp *in, AstClass *cls)
{
    RValue *v = RValue_New(in, RV_CLASS);
    v->u.cls = cls;
    return v;
}

RValue *RValue_StructType(Interp *in, AstStructType *type)
{
    RValue *v = RValue_New(in, RV_STRUCTTYPE);
    v->u.stype = type;
    return v;
}

RValue *RValue_List(Interp *in, AsterList *list)
{
    RValue *v = RValue_New(in, RV_LIST);
    v->u.list = list;
    return v;
}

RValue *RValue_Tuple(Interp *in, AsterTuple *tuple)
{
    RValue *v = RValue_New(in, RV_TUPLE);
    v->u.tuple = tuple;
    return v;
}

RValue *RValue_Dict(Interp *in, AsterDict *dict)
{
    RValue *v = RValue_New(in, RV_DICT);
    v->u.dict = dict;
    return v;
}

RValue *RValue_Set(Interp *in, AsterSet *set)
{
    RValue *v = RValue_New(in, RV_SET);
    v->u.set = set;
    return v;
}

RValue *RValue_FrozenSet(Interp *in, AsterSet *set)
{
    RValue *v = RValue_New(in, RV_FROZENSET);
    v->u.set = set;
    return v;
}

RValue *RValue_Module(Interp *in, AstModule *module)
{
    RValue *v = RValue_New(in, RV_MODULE);
    v->u.module = module;
    return v;
}

RValue *RValue_Super(Interp *in, AstClass *owner, RValue *self)
{
    AsterSuper *s = Arena_Alloc(in->arena, sizeof(AsterSuper));
    RValue *v = RValue_New(in, RV_SUPER);
    s->owner = owner;
    s->self = self;
    v->u.super = s;
    return v;
}

RValue *RValue_Enum(Interp *in, AstEnumType *type, const char *name, size_t index)
{
    RValue *v = RValue_New(in, RV_ENUM);
    v->u.en.type = type;
    v->u.en.name = name;
    v->u.en.index = index;
    return v;
}

RValue *RValue_EnumType(Interp *in, AstEnumType *type)
{
    RValue *v = RValue_New(in, RV_ENUMTYPE);
    v->u.etype = type;
    return v;
}

RValue *RValue_Overload(Interp *in, AstOverload *overload)
{
    RValue *v = RValue_New(in, RV_OVERLOAD);
    v->u.overload = overload;
    return v;
}

/* ---- 호출 스택 (트레이스백) ---- */

void Interp_FrameAppend(Interp *in, const char *name, int line)
{
    if (in->nframes == in->cap_frames) {
        size_t cap = in->cap_frames ? in->cap_frames * 2 : 16;
        AstFrame *grown = Arena_Alloc(in->arena, cap * sizeof(AstFrame));
        if (in->nframes)
            memcpy(grown, in->frames, in->nframes * sizeof(AstFrame));
        in->frames = grown;
        in->cap_frames = cap;
    }
    in->frames[in->nframes].name = name;
    in->frames[in->nframes].line = line;
    in->nframes++;
}

void Interp_FramePush(Interp *in, const char *name)
{
    Interp_FrameAppend(in, name, in->line);
}

void Interp_FramePop(Interp *in)
{
    if (in->nframes > 0)
        in->nframes--;
}

void Interp_FrameTruncate(Interp *in, size_t n)
{
    if (n < in->nframes)
        in->nframes = n;
}

void Interp_PrintTraceback(FILE *out, Interp *in)
{
    size_t i;

    if (!in || in->nframes == 0)
        return;
    fprintf(out, "Traceback (most recent call last):\n");
    for (i = 0; i < in->nframes; i++) {
        fprintf(out, "  at %s (line %d)\n",
                in->frames[i].name ? in->frames[i].name : "?", in->frames[i].line);
    }
}

RValue *RValue_Generator(Interp *in, AstFn *fn, Env *body_env, RValue *self)
{
    AstGenerator *gen = Arena_Alloc(in->arena, sizeof(AstGenerator));
    Interp *gin = Arena_Alloc(in->arena, sizeof(Interp));
    RValue *v = RValue_New(in, RV_GENERATOR);

    *gin = *in;
    gin->sig = SIG_OK;
    gin->ret = NULL;
    gin->exc = NULL;
    gin->yield_buf = NULL;
    gin->gen = gen;
    gin->depth = 0;
    gin->predeclaring = 0;
    gin->frames = NULL;
    gin->nframes = 0;
    gin->cap_frames = 0;
    Interp_FramePush(gin, fn->name ? fn->name : "lambda");

    gen->fn = fn;
    gen->body_env = body_env;
    gen->self = self;
    gen->interp = gin;
    gen->yielded = NULL;
    gen->started = 0;
    gen->done = 0;
    gen->failed = 0;
    gen->ctx = NULL;
    gen->caller_ctx = NULL;
    gen->stack = NULL;
    gen->caller_gen = NULL;
    v->u.gen = gen;
    return v;
}

/* ---- 컬렉션 ---- */

AsterList *List_New(Interp *in)
{
    return Arena_Alloc(in->arena, sizeof(AsterList));
}

static void grow_items(Interp *in, RValue ***items, size_t *cap, size_t need)
{
    size_t ncap = *cap ? *cap : 8;
    while (ncap < need)
        ncap *= 2;
    (void)in;
    *items = xrealloc(*items, ncap * sizeof(RValue *));
    *cap = ncap;
}

void List_Append(Interp *in, AsterList *list, RValue *value)
{
    if (list->len == list->cap)
        grow_items(in, &list->items, &list->cap, list->len + 1);
    list->items[list->len++] = value;
}

void List_Insert(Interp *in, AsterList *list, size_t index, RValue *value)
{
    if (index > list->len)
        index = list->len;
    if (list->len == list->cap)
        grow_items(in, &list->items, &list->cap, list->len + 1);
    memmove(list->items + index + 1, list->items + index,
            (list->len - index) * sizeof(RValue *));
    list->items[index] = value;
    list->len++;
}

void List_RemoveAt(AsterList *list, size_t index)
{
    if (index >= list->len)
        return;
    memmove(list->items + index, list->items + index + 1,
            (list->len - index - 1) * sizeof(RValue *));
    list->len--;
}

AsterTuple *Tuple_New(Interp *in, size_t len)
{
    AsterTuple *tuple = Arena_Alloc(in->arena, sizeof(AsterTuple));
    tuple->len = len;
    if (len)
        tuple->items = Arena_Alloc(in->arena, len * sizeof(RValue *));
    return tuple;
}

AsterDict *Dict_New(Interp *in)
{
    return Arena_Alloc(in->arena, sizeof(AsterDict));
}

static void dict_alloc_index(AsterDict *dict, size_t cap)
{
    size_t i;
    dict->index = xrealloc(dict->index, cap * sizeof(long));
    memset(dict->index, 0, cap * sizeof(long));
    dict->icap = cap;
    for (i = 0; i < dict->len; i++) {
        size_t pos = (size_t)(dict->entries[i].hash & (cap - 1));
        while (dict->index[pos])
            pos = (pos + 1) & (cap - 1);
        dict->index[pos] = (long)(i + 1);
    }
}

long Dict_FindIndex(const AsterDict *dict, const RValue *key)
{
    uint64_t h;
    size_t pos, start;

    if (dict->len == 0)
        return -1;
    if (!dict->index || dict->icap == 0) {
        size_t i;
        for (i = 0; i < dict->len; i++)
            if (RValue_Equal(dict->entries[i].key, key))
                return (long)i;
        return -1;
    }
    h = RValue_Hash(key);
    start = pos = (size_t)(h & (dict->icap - 1));
    for (;;) {
        long slot = dict->index[pos];
        if (!slot)
            return -1;
        if (dict->entries[slot - 1].hash == h &&
            RValue_Equal(dict->entries[slot - 1].key, key))
            return slot - 1;
        pos = (pos + 1) & (dict->icap - 1);
        if (pos == start)
            return -1;
    }
}

RValue *Dict_Lookup(const AsterDict *dict, const RValue *key)
{
    long i = Dict_FindIndex(dict, key);
    return i < 0 ? NULL : dict->entries[i].value;
}

int Dict_Set(Interp *in, AsterDict *dict, RValue *key, RValue *value, int *ok)
{
    uint64_t h;
    size_t pos, start;
    long found;

    *ok = 1;
    if (!RValue_Hashable(key)) {
        *ok = 0;
        RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(key));
        return 0;
    }
    if (!dict->index)
        dict_alloc_index(dict, 8);
    else if (dict->len * 4 >= dict->icap * 3)
        dict_alloc_index(dict, dict->icap * 2);
    h = RValue_Hash(key);
    found = -1;
    start = pos = (size_t)(h & (dict->icap - 1));
    for (;;) {
        long slot = dict->index[pos];
        if (!slot)
            break;
        if (dict->entries[slot - 1].hash == h &&
            RValue_Equal(dict->entries[slot - 1].key, key)) {
            found = slot - 1;
            break;
        }
        pos = (pos + 1) & (dict->icap - 1);
        if (pos == start)
            break;
    }
    if (found >= 0) {
        dict->entries[found].value = value;
        return 1;
    }
    if (dict->len == dict->cap) {
        size_t ncap = dict->cap ? dict->cap * 2 : 8;
        dict->entries = xrealloc(dict->entries, ncap * sizeof(AsterDictEntry));
        dict->cap = ncap;
    }
    dict->entries[dict->len].key = key;
    dict->entries[dict->len].value = value;
    dict->entries[dict->len].hash = h;
    dict->index[pos] = (long)(dict->len + 1);
    dict->len++;
    return 1;
}

void Dict_RemoveAt(AsterDict *dict, size_t index)
{
    if (index >= dict->len)
        return;
    memmove(dict->entries + index, dict->entries + index + 1,
            (dict->len - index - 1) * sizeof(AsterDictEntry));
    dict->len--;
    if (dict->index)
        dict_alloc_index(dict, dict->icap);
}

AsterSet *Set_New(Interp *in)
{
    return Arena_Alloc(in->arena, sizeof(AsterSet));
}

static void set_alloc_index(AsterSet *set, size_t cap)
{
    size_t i;
    set->index = xrealloc(set->index, cap * sizeof(long));
    memset(set->index, 0, cap * sizeof(long));
    set->icap = cap;
    for (i = 0; i < set->len; i++) {
        size_t pos = (size_t)(set->hashes[i] & (cap - 1));
        while (set->index[pos])
            pos = (pos + 1) & (cap - 1);
        set->index[pos] = (long)(i + 1);
    }
}

long Set_FindIndex(const AsterSet *set, const RValue *value)
{
    uint64_t h;
    size_t pos, start;

    if (set->len == 0)
        return -1;
    if (!set->index || set->icap == 0) {
        size_t i;
        for (i = 0; i < set->len; i++)
            if (RValue_Equal(set->items[i], value))
                return (long)i;
        return -1;
    }
    h = RValue_Hash(value);
    start = pos = (size_t)(h & (set->icap - 1));
    for (;;) {
        long slot = set->index[pos];
        if (!slot)
            return -1;
        if (set->hashes[slot - 1] == h && RValue_Equal(set->items[slot - 1], value))
            return slot - 1;
        pos = (pos + 1) & (set->icap - 1);
        if (pos == start)
            return -1;
    }
}

int Set_Has(const AsterSet *set, const RValue *value)
{
    return Set_FindIndex(set, value) >= 0;
}

int Set_Add(Interp *in, AsterSet *set, RValue *value, int *ok)
{
    uint64_t h;

    if (ok)
        *ok = 1;
    if (!RValue_Hashable(value)) {
        if (ok)
            *ok = 0;
        RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(value));
        return 0;
    }
    if (!set->index)
        set_alloc_index(set, 8);
    else if (set->len * 4 >= set->icap * 3)
        set_alloc_index(set, set->icap * 2);
    if (Set_FindIndex(set, value) >= 0)
        return 1;
    if (set->len == set->cap) {
        size_t ncap = set->cap ? set->cap * 2 : 8;
        set->items = xrealloc(set->items, ncap * sizeof(RValue *));
        set->hashes = xrealloc(set->hashes, ncap * sizeof(uint64_t));
        set->cap = ncap;
    }
    h = RValue_Hash(value);
    set->items[set->len] = value;
    set->hashes[set->len] = h;
    {
        size_t pos = (size_t)(h & (set->icap - 1));
        while (set->index[pos])
            pos = (pos + 1) & (set->icap - 1);
        set->index[pos] = (long)(set->len + 1);
    }
    set->len++;
    return 1;
}

void Set_RemoveAt(AsterSet *set, size_t index)
{
    if (index >= set->len)
        return;
    memmove(set->items + index, set->items + index + 1,
            (set->len - index - 1) * sizeof(RValue *));
    memmove(set->hashes + index, set->hashes + index + 1,
            (set->len - index - 1) * sizeof(uint64_t));
    set->len--;
    if (set->index)
        set_alloc_index(set, set->icap);
}

void Dict_Clear(AsterDict *dict)
{
    dict->len = 0;
    if (dict->index && dict->icap)
        memset(dict->index, 0, dict->icap * sizeof(long));
}

void Set_Clear(AsterSet *set)
{
    set->len = 0;
    if (set->index && set->icap)
        memset(set->index, 0, set->icap * sizeof(long));
}

int RValue_Hashable(const RValue *v)
{
    size_t i;

    if (!v)
        return 1;
    switch (v->kind) {
    case RV_LIST:
    case RV_DICT:
    case RV_SET:
        return 0;
    case RV_TUPLE:
        for (i = 0; i < v->u.tuple->len; i++) {
            if (!RValue_Hashable(v->u.tuple->items[i]))
                return 0;
        }
        return 1;
    case RV_FROZENSET:
        for (i = 0; i < v->u.set->len; i++) {
            if (!RValue_Hashable(v->u.set->items[i]))
                return 0;
        }
        return 1;
    case RV_STRUCT:
        for (i = 0; i < v->u.stv->type->nfields; i++) {
            if (!RValue_Hashable(v->u.stv->cells[i]))
                return 0;
        }
        return 1;
    default:
        return 1;
    }
}

/* ---- 해시 ---- */

static uint64_t hash_mix(uint64_t h, uint64_t x)
{
    h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

static uint64_t hash_bytes(const char *s, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hash_int(int64_t v)
{
    uint64_t x = (uint64_t)v;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return x;
}

uint64_t RValue_Hash(const RValue *v)
{
    size_t i;

    if (!v)
        return 0x9e3779b9ULL;
    switch (v->kind) {
    case RV_NULL:
        return 0x9e3779b9ULL;
    case RV_BOOL:
        return (uint64_t)(v->u.b ? 1 : 0);
    case RV_INT:
        return hash_int(v->u.i);
    case RV_FLOAT: {
        double f = v->u.f;
        /* int 1과 float 1.0은 같으므로 같은 해시를 준다. */
        if (f >= -9223372036854775808.0 && f < 9223372036854775808.0 &&
            f == (double)(int64_t)f)
            return hash_int((int64_t)f);
        {
            uint64_t bits;
            memcpy(&bits, &f, sizeof bits);
            return hash_int((int64_t)bits);
        }
    }
    case RV_STR:
        return hash_bytes(v->u.s->data, v->u.s->length);
    case RV_TUPLE: {
        uint64_t h = 0x51ed270bULL;
        for (i = 0; i < v->u.tuple->len; i++)
            h = hash_mix(h, RValue_Hash(v->u.tuple->items[i]));
        return h;
    }
    case RV_FROZENSET: {
        uint64_t h = 0x2545f491ULL;
        for (i = 0; i < v->u.set->len; i++)
            h += RValue_Hash(v->u.set->items[i]);
        return h;
    }
    case RV_STRUCT: {
        uint64_t h = hash_int((int64_t)(uintptr_t)v->u.stv->type);
        for (i = 0; i < v->u.stv->type->nfields; i++)
            h = hash_mix(h, RValue_Hash(v->u.stv->cells[i]));
        return h;
    }
    case RV_OBJECT:
        return hash_int((int64_t)(uintptr_t)v->u.obj);
    case RV_PTR:
        return hash_int((int64_t)(uintptr_t)v->u.ptr.slot);
    case RV_FUNC:
        return hash_int((int64_t)(uintptr_t)v->u.fn);
    case RV_BUILTIN:
        return hash_int((int64_t)(uintptr_t)v->u.builtin);
    case RV_CLASS:
        return hash_int((int64_t)(uintptr_t)v->u.cls);
    case RV_STRUCTTYPE:
        return hash_int((int64_t)(uintptr_t)v->u.stype);
    case RV_ENUM:
        return hash_mix(hash_int((int64_t)(uintptr_t)v->u.en.type),
                        (uint64_t)v->u.en.index);
    case RV_ENUMTYPE:
        return hash_int((int64_t)(uintptr_t)v->u.etype);
    case RV_EXC:
        return hash_int((int64_t)(uintptr_t)v->u.exc);
    case RV_EXC_CLASS:
        return hash_bytes(v->u.exc_name, strlen(v->u.exc_name));
    case RV_MODULE:
        return hash_int((int64_t)(uintptr_t)v->u.module);
    case RV_LIST:
        return hash_int((int64_t)(uintptr_t)v->u.list);
    case RV_DICT:
        return hash_int((int64_t)(uintptr_t)v->u.dict);
    case RV_SET:
        return hash_int((int64_t)(uintptr_t)v->u.set);
    case RV_SUPER:
        return hash_int((int64_t)(uintptr_t)v->u.super);
    case RV_OVERLOAD:
        return hash_int((int64_t)(uintptr_t)v->u.overload);
    case RV_GENERATOR:
        return hash_int((int64_t)(uintptr_t)v->u.gen);
    }
    return 0;
}

const char *RValue_TypeName(const RValue *v)
{
    if (!v)
        return "null";
    switch (v->kind) {
    case RV_NULL:
        return "null";
    case RV_INT:
        return "int";
    case RV_FLOAT:
        return "float";
    case RV_BOOL:
        return "bool";
    case RV_STR:
        return "str";
    case RV_OBJECT:
        return v->u.obj->cls->name;
    case RV_STRUCT:
        return v->u.stv->type->tag;
    case RV_FUNC:
    case RV_BUILTIN:
        return "function";
    case RV_PTR:
        return "pointer";
    case RV_EXC:
        return v->u.exc->name;
    case RV_EXC_CLASS:
        return v->u.exc_name;
    case RV_CLASS:
        return v->u.cls->name;
    case RV_STRUCTTYPE:
        return v->u.stype->tag;
    case RV_LIST:
        return "list";
    case RV_TUPLE:
        return "tuple";
    case RV_DICT:
        return "dict";
    case RV_SET:
        return "set";
    case RV_FROZENSET:
        return "frozenset";
    case RV_MODULE:
        return "module";
    case RV_SUPER:
        return "super";
    case RV_ENUM:
        return v->u.en.type->name;
    case RV_ENUMTYPE:
        return v->u.etype->name;
    case RV_OVERLOAD:
        return "function";
    case RV_GENERATOR:
        return "generator";
    }
    return "object";
}

int64_t RValue_Length(Interp *in, const RValue *v, int *ok)
{
    *ok = 1;
    if (!v) {
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot call len() on null");
        return 0;
    }
    switch (v->kind) {
    case RV_STR:
        return (int64_t)v->u.s->cp_length;
    case RV_LIST:
        return (int64_t)v->u.list->len;
    case RV_TUPLE:
        return (int64_t)v->u.tuple->len;
    case RV_DICT:
        return (int64_t)v->u.dict->len;
    case RV_SET:
    case RV_FROZENSET:
        return (int64_t)v->u.set->len;
    case RV_OBJECT:
        if (RValue_HasMethod(in, v, "__len__")) {
            RValue *r = Interp_CallMethod(in, (RValue *)v, "__len__", NULL, 0);
            if (!r) {
                *ok = 0;
                return 0;
            }
            if (r->kind != RV_INT && r->kind != RV_BOOL) {
                *ok = 0;
                RT_Throw(in, "TypeError", "__len__ must return an integer");
                return 0;
            }
            return r->kind == RV_INT ? r->u.i : r->u.b;
        }
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot call len() on '%s'", RValue_TypeName(v));
        return 0;
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot call len() on '%s'", RValue_TypeName(v));
        return 0;
    }
}

int RValue_Iterable(const RValue *v)
{
    if (!v)
        return 0;
    switch (v->kind) {
    case RV_STR:
    case RV_LIST:
    case RV_TUPLE:
    case RV_DICT:
    case RV_SET:
    case RV_FROZENSET:
        return 1;
    case RV_GENERATOR:
    case RV_OBJECT:
        return 1;
    default:
        return 0;
    }
}

size_t RValue_IterLen(Interp *in, const RValue *v, int *ok)
{
    *ok = 1;
    if (!v)
        goto bad;
    switch (v->kind) {
    case RV_STR:
        return v->u.s->cp_length;
    case RV_LIST:
        return v->u.list->len;
    case RV_TUPLE:
        return v->u.tuple->len;
    case RV_DICT:
        return v->u.dict->len;
    case RV_SET:
    case RV_FROZENSET:
        return v->u.set->len;
    default:
        break;
    }
bad:
    *ok = 0;
    RT_Throw(in, "TypeError", "'%s' is not iterable", RValue_TypeName(v));
    return 0;
}

RValue *RValue_IterAt(Interp *in, const RValue *v, size_t index, int *ok)
{
    *ok = 1;
    switch (v->kind) {
    case RV_STR: {
        size_t off = AsterString_ByteOffset(v->u.s, index);
        size_t adv = 1;
        Aster_UTF8_Decode(v->u.s->data + off, v->u.s->length - off, &adv);
        return RValue_Str(in, AsterString_FromUTF8(v->u.s->data + off, adv));
    }
    case RV_LIST:
        return v->u.list->items[index];
    case RV_TUPLE:
        return v->u.tuple->items[index];
    case RV_DICT:
        return v->u.dict->entries[index].key;
    case RV_SET:
    case RV_FROZENSET:
        return v->u.set->items[index];
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' is not iterable", RValue_TypeName(v));
        return NULL;
    }
}

/* ---- 순회 커서 ---- */

int Iter_Begin(Interp *in, RValue *src, IterCursor *cur)
{
    memset(cur, 0, sizeof(*cur));
    cur->src = src;
    if (!src) {
        RT_Throw(in, "TypeError", "cannot iterate over null");
        return 0;
    }
    switch (src->kind) {
    case RV_STR:
    case RV_LIST:
    case RV_TUPLE:
    case RV_DICT:
    case RV_SET:
    case RV_FROZENSET: {
        int ok = 1;
        cur->mode = 0;
        cur->len = RValue_IterLen(in, src, &ok);
        return ok;
    }
    case RV_GENERATOR:
        cur->mode = 1;
        cur->gen = src->u.gen;
        return 1;
    case RV_OBJECT: {
        cur->mode = 2;
        if (RValue_HasMethod(in, src, "__next__")) {
            cur->obj = src;
            return 1;
        }
        if (RValue_HasMethod(in, src, "__iter__")) {
            RValue *iter = Interp_CallMethod(in, src, "__iter__", NULL, 0);
            if (in->sig != SIG_OK)
                return 0;
            if (!iter || !RValue_HasMethod(in, iter, "__next__")) {
                RT_Throw(in, "TypeError",
                         "__iter__ of '%s' did not return a value with __next__",
                         src->u.obj->cls->name);
                return 0;
            }
            cur->obj = iter;
            return 1;
        }
        RT_Throw(in, "TypeError", "'%s' object is not iterable (missing __iter__/__next__)",
                 src->u.obj->cls->name);
        return 0;
    }
    default:
        RT_Throw(in, "TypeError", "'%s' is not iterable", RValue_TypeName(src));
        return 0;
    }
}

RValue *Iter_Next(Interp *in, IterCursor *cur, int *ended, int *ok)
{
    *ended = 0;
    *ok = 1;
    switch (cur->mode) {
    case 0:
        if (cur->index >= cur->len) {
            *ended = 1;
            return NULL;
        }
        return RValue_IterAt(in, cur->src, cur->index++, ok);
    case 1: {
        RValue *v = Generator_Next(in, cur->gen, ended);
        if (in->sig != SIG_OK) {
            *ok = 0;
            return NULL;
        }
        return v;
    }
    case 2: {
        RValue *v = Interp_CallMethod(in, cur->obj, "__next__", NULL, 0);
        if (in->sig == SIG_THROW) {
            if (in->exc && in->exc->kind == RV_EXC &&
                !strcmp(in->exc->u.exc->name, "StopIteration")) {
                in->sig = SIG_OK;
                in->exc = NULL;
                *ended = 1;
                return NULL;
            }
            *ok = 0;
            return NULL;
        }
        if (!v) {
            *ended = 1;
            return NULL;
        }
        return v;
    }
    default:
        break;
    }
    *ok = 0;
    RT_Throw(in, "TypeError", "invalid iteration cursor");
    return NULL;
}

RValue *RValue_Copy(Interp *in, RValue *v)
{
    AstStructValue *copy;
    size_t i;

    if (!v || v->kind != RV_STRUCT)
        return v;
    copy = StructValue_New(in, v->u.stv->type);
    for (i = 0; i < v->u.stv->type->nfields; i++)
        copy->cells[i] = RValue_Copy(in, v->u.stv->cells[i]);
    return RValue_Struct(in, copy);
}

/* ---- 환경 ---- */

Env *Env_New(Interp *in, Env *parent)
{
    Env *env = Arena_Alloc(in->arena, sizeof(Env));
    env->parent = parent;
    return env;
}

RValue **Env_LookupCell(Env *env, const char *name)
{
    for (; env; env = env->parent) {
        Binding *b;
        for (b = env->vars; b; b = b->next) {
            if (!strcmp(b->name, name))
                return &b->cell;
        }
    }
    return NULL;
}

void Env_Define(Interp *in, Env *env, const char *name, RValue *value)
{
    Binding *b = Arena_Alloc(in->arena, sizeof(Binding));
    b->name = name;
    b->cell = value;
    b->is_const = 0;
    b->next = env->vars;
    env->vars = b;
}

void Env_DefineConst(Interp *in, Env *env, const char *name, RValue *value)
{
    Binding *b = Arena_Alloc(in->arena, sizeof(Binding));
    b->name = name;
    b->cell = value;
    b->is_const = 1;
    b->next = env->vars;
    env->vars = b;
}

int Env_IsConst(Env *env, const char *name)
{
    for (; env; env = env->parent) {
        Binding *b;
        for (b = env->vars; b; b = b->next) {
            if (!strcmp(b->name, name))
                return b->is_const;
        }
    }
    return 0;
}

int Env_Assign(Env *env, const char *name, RValue *value)
{
    RValue **cell = Env_LookupCell(env, name);
    if (!cell)
        return 0;
    *cell = value;
    return 1;
}

int Env_HasLocal(Env *env, const char *name)
{
    Binding *b;
    for (b = env->vars; b; b = b->next) {
        if (!strcmp(b->name, name))
            return 1;
    }
    return 0;
}

/* ---- 클래스/인스턴스 ---- */

size_t Class_FieldCount(AstClass *cls)
{
    size_t i, n = cls->base ? Class_FieldCount(cls->base) : 0;
    for (i = 0; i < cls->nfields; i++)
        if (!cls->fields[i].is_static)
            n++;
    return n;
}

static void fill_fields(Interp *in, AstClass *cls, InstField *fields, size_t *index)
{
    size_t i;

    if (cls->base)
        fill_fields(in, cls->base, fields, index);
    for (i = 0; i < cls->nfields; i++) {
        if (cls->fields[i].is_static)
            continue;
        fields[*index].name = cls->fields[i].name;
        fields[*index].cell = RValue_Null(in);
        (*index)++;
    }
}

AstInstance *Instance_New(Interp *in, AstClass *cls)
{
    AstInstance *obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    size_t index = 0;

    obj->cls = cls;
    obj->nfields = Class_FieldCount(cls);
    obj->fields = obj->nfields ? Arena_Alloc(in->arena, obj->nfields * sizeof(InstField)) : NULL;
    fill_fields(in, cls, obj->fields, &index);
    return obj;
}

InstField *Instance_FindField(AstInstance *obj, const char *name)
{
    size_t i;
    for (i = 0; i < obj->nfields; i++) {
        if (!strcmp(obj->fields[i].name, name))
            return &obj->fields[i];
    }
    return NULL;
}

AstClass *Class_FindBase(AstClass *cls, const char *name)
{
    for (; cls; cls = cls->base) {
        if (!strcmp(cls->name, name))
            return cls;
    }
    return NULL;
}

MethodDesc *Class_FindMethod(AstClass *cls, const char *name)
{
    size_t i;
    for (; cls; cls = cls->base) {
        for (i = 0; i < cls->nmethods; i++) {
            if (!strcmp(cls->methods[i].name, name))
                return &cls->methods[i];
        }
    }
    return NULL;
}

PropDesc *Class_FindProperty(AstClass *cls, const char *name)
{
    size_t i;
    for (; cls; cls = cls->base) {
        for (i = 0; i < cls->nprops; i++) {
            if (!strcmp(cls->props[i].name, name))
                return &cls->props[i];
        }
    }
    return NULL;
}

/* ---- 구조체 ---- */

AstStructType *Struct_FindType(Env *env, const char *name)
{
    for (; env; env = env->parent) {
        Binding *b;
        for (b = env->vars; b; b = b->next) {
            if (!strcmp(b->name, name) && b->cell && b->cell->kind == RV_STRUCTTYPE)
                return b->cell->u.stype;
        }
    }
    return NULL;
}

int StructValue_FieldIndex(AstStructType *type, const char *name)
{
    size_t i;
    for (i = 0; i < type->nfields; i++) {
        if (!strcmp(type->fields[i].name, name))
            return (int)i;
    }
    return -1;
}

static RValue *default_for_type_depth(Interp *in, Env *env, typeref_ty ty, int depth);

AstStructValue *StructValue_New(Interp *in, AstStructType *type)
{
    AstStructValue *stv = Arena_Alloc(in->arena, sizeof(AstStructValue));
    stv->type = type;
    stv->cells = type->nfields ? Arena_Alloc(in->arena, type->nfields * sizeof(RValue *)) : NULL;
    return stv;
}

/* ---- 기본값/크기 ---- */

static RValue *default_for_type_depth(Interp *in, Env *env, typeref_ty ty, int depth);

static RValue *struct_default(Interp *in, Env *env, AstStructType *type, int depth)
{
    AstStructValue *stv;
    size_t i;

    if (depth > MAX_DEPTH) {
        RT_Throw(in, "RuntimeError", "struct default values are nested too deeply");
        return RValue_Null(in);
    }
    stv = StructValue_New(in, type);
    for (i = 0; i < type->nfields; i++)
        stv->cells[i] = default_for_type_depth(in, env, type->fields[i].ty, depth + 1);
    return RValue_Struct(in, stv);
}

static RValue *default_for_type_depth(Interp *in, Env *env, typeref_ty ty, int depth)
{
    if (!ty)
        return RValue_Null(in);
    if (depth > MAX_DEPTH) {
        RT_Throw(in, "RuntimeError", "type default values are nested too deeply");
        return RValue_Null(in);
    }

    switch (ty->kind) {
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT:
            return RValue_Int(in, 0);
        case FloatT:
            return RValue_Float(in, 0.0);
        case BoolT:
            return RValue_Bool(in, 0);
        case StrT:
            return RValue_Str(in, AsterString_FromCStr(""));
        case NullT:
            return RValue_Null(in);
        }
        return RValue_Null(in);
    case Pointer_kind:
    case Nullable_kind:
    case FunctionTy_kind:
        return RValue_Null(in);
    case StructRef_kind: {
        AstStructType *type = Struct_FindType(env, ty->v.StructRef.tag);
        if (!type) {
            RT_Throw(in, "TypeError", "cannot find struct type '%s'", ty->v.StructRef.tag);
            return RValue_Null(in);
        }
        return struct_default(in, env, type, depth + 1);
    }
    case Named_kind: {
        const char *name = ty->v.Named.name;
        RValue **cell;
        if (!strcmp(name, "list") || !strcmp(name, "List"))
            return RValue_List(in, List_New(in));
        if (!strcmp(name, "dict") || !strcmp(name, "Dict") || !strcmp(name, "map"))
            return RValue_Dict(in, Dict_New(in));
        if (!strcmp(name, "set") || !strcmp(name, "Set"))
            return RValue_Set(in, Set_New(in));
        if (!strcmp(name, "frozenset") || !strcmp(name, "FrozenSet"))
            return RValue_FrozenSet(in, Set_New(in));
        if (!strcmp(name, "tuple") || !strcmp(name, "Tuple"))
            return RValue_Tuple(in, Tuple_New(in, 0));
        if (!strcmp(name, "var") || !strcmp(name, "any"))
            return RValue_Null(in);
        cell = Env_LookupCell(env, name);
        if (cell && *cell && (*cell)->kind == RV_STRUCTTYPE)
            return struct_default(in, env, (*cell)->u.stype, depth + 1);
        return RValue_Null(in);
    }
    }
    return RValue_Null(in);
}

RValue *DefaultForType(Interp *in, Env *env, typeref_ty ty)
{
    return default_for_type_depth(in, env, ty, 0);
}

static size_t align_up(size_t n, size_t align)
{
    return align ? (n + align - 1) / align * align : n;
}

static size_t type_size_depth(Interp *in, Env *env, typeref_ty ty, int depth);

static size_t struct_size_depth(Interp *in, Env *env, AstStructType *type, int depth)
{
    size_t offset = 0;
    size_t align = 1;
    size_t i;

    if (depth > MAX_DEPTH) {
        RT_Throw(in, "RuntimeError", "struct size computation is nested too deeply");
        return 0;
    }
    for (i = 0; i < type->nfields; i++) {
        size_t field_align = 8;
        size_t field_size = type_size_depth(in, env, type->fields[i].ty, depth + 1);
        typeref_ty ft = type->fields[i].ty;

        if (ft && ft->kind == Primitive_kind && ft->v.Primitive.kind == BoolT)
            field_align = 1;
        offset = align_up(offset, field_align) + field_size;
        if (field_align > align)
            align = field_align;
    }
    return align_up(offset, align);
}

static size_t type_size_depth(Interp *in, Env *env, typeref_ty ty, int depth)
{
    if (!ty)
        return 8;
    if (depth > MAX_DEPTH) {
        RT_Throw(in, "RuntimeError", "type size computation is nested too deeply");
        return 0;
    }

    switch (ty->kind) {
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT:
        case FloatT:
        case StrT:
        case NullT:
            return 8;
        case BoolT:
            return 1;
        }
        return 8;
    case Pointer_kind:
    case FunctionTy_kind:
        return 8;
    case Nullable_kind:
        if (ty->v.Nullable.base && ty->v.Nullable.base->kind == Primitive_kind &&
            ty->v.Nullable.base->v.Primitive.kind == BoolT)
            return 2;
        return 8;
    case StructRef_kind: {
        AstStructType *type = Struct_FindType(env, ty->v.StructRef.tag);
        return type ? struct_size_depth(in, env, type, depth + 1) : 0;
    }
    case Named_kind: {
        RValue **cell = Env_LookupCell(env, ty->v.Named.name);
        if (cell && *cell && (*cell)->kind == RV_STRUCTTYPE)
            return struct_size_depth(in, env, (*cell)->u.stype, depth + 1);
        return 8;
    }
    }
    return 8;
}

size_t Type_SizeOf(Interp *in, Env *env, typeref_ty ty)
{
    return type_size_depth(in, env, ty, 0);
}

int Type_IsNullable(typeref_ty ty)
{
    if (!ty)
        return 1;
    switch (ty->kind) {
    case Nullable_kind:
    case Pointer_kind:
        return 1;
    case Named_kind:
    case FunctionTy_kind:
        return 1;
    case StructRef_kind:
        return 0;
    case Primitive_kind:
        return ty->v.Primitive.kind == NullT;
    }
    return 1;
}

/* ---- 값 연산 ---- */

int RValue_IsNumber(const RValue *v)
{
    return v && (v->kind == RV_INT || v->kind == RV_FLOAT || v->kind == RV_BOOL);
}

int RValue_Truthy(const RValue *v)
{
    if (!v)
        return 0;
    switch (v->kind) {
    case RV_NULL:
        return 0;
    case RV_INT:
        return v->u.i != 0;
    case RV_FLOAT:
        return v->u.f != 0.0;
    case RV_BOOL:
        return v->u.b != 0;
    case RV_STR:
        return v->u.s && v->u.s->length != 0;
    case RV_PTR:
        return v->u.ptr.slot != NULL;
    case RV_LIST:
        return v->u.list->len != 0;
    case RV_TUPLE:
        return v->u.tuple->len != 0;
    case RV_DICT:
        return v->u.dict->len != 0;
    case RV_SET:
    case RV_FROZENSET:
        return v->u.set->len != 0;
    default:
        return 1;
    }
}

static int string_equal(AsterString *a, AsterString *b)
{
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

static int seq_equal(RValue **a, size_t na, RValue **b, size_t nb)
{
    size_t i;
    if (na != nb)
        return 0;
    for (i = 0; i < na; i++) {
        if (!RValue_Equal(a[i], b[i]))
            return 0;
    }
    return 1;
}

static int set_equal(const AsterSet *a, const AsterSet *b)
{
    size_t i;
    if (a->len != b->len)
        return 0;
    for (i = 0; i < a->len; i++) {
        if (!Set_Has(b, a->items[i]))
            return 0;
    }
    return 1;
}

static int dict_equal(const AsterDict *a, const AsterDict *b)
{
    size_t i;
    RValue *value;
    if (a->len != b->len)
        return 0;
    for (i = 0; i < a->len; i++) {
        value = Dict_Lookup(b, a->entries[i].key);
        if (!value || !RValue_Equal(a->entries[i].value, value))
            return 0;
    }
    return 1;
}

int RValue_Equal(const RValue *a, const RValue *b)
{
    int a_is_set, b_is_set;

    if (!a || !b)
        return a == b;

    if (RValue_IsNumber(a) && RValue_IsNumber(b)) {
        if (a->kind == RV_FLOAT || b->kind == RV_FLOAT) {
            double x = a->kind == RV_FLOAT ? a->u.f : (double)(a->kind == RV_BOOL ? a->u.b : a->u.i);
            double y = b->kind == RV_FLOAT ? b->u.f : (double)(b->kind == RV_BOOL ? b->u.b : b->u.i);
            return x == y;
        }
        return (a->kind == RV_BOOL ? a->u.b : a->u.i) == (b->kind == RV_BOOL ? b->u.b : b->u.i);
    }
    a_is_set = a->kind == RV_SET || a->kind == RV_FROZENSET;
    b_is_set = b->kind == RV_SET || b->kind == RV_FROZENSET;
    if (a_is_set && b_is_set)
        return set_equal(a->u.set, b->u.set);
    if (a->kind != b->kind)
        return 0;

    switch (a->kind) {
    case RV_NULL:
        return 1;
    case RV_STR:
        return string_equal(a->u.s, b->u.s);
    case RV_OBJECT:
        return a->u.obj == b->u.obj;
    case RV_PTR:
        return a->u.ptr.slot == b->u.ptr.slot;
    case RV_FUNC:
        return a->u.fn == b->u.fn;
    case RV_BUILTIN:
        return a->u.builtin == b->u.builtin;
    case RV_EXC:
        return a->u.exc == b->u.exc;
    case RV_EXC_CLASS:
        return !strcmp(a->u.exc_name, b->u.exc_name);
    case RV_CLASS:
        return a->u.cls == b->u.cls;
    case RV_STRUCTTYPE:
        return a->u.stype == b->u.stype;
    case RV_LIST:
        return seq_equal(a->u.list->items, a->u.list->len,
                         b->u.list->items, b->u.list->len);
    case RV_TUPLE:
        return seq_equal(a->u.tuple->items, a->u.tuple->len,
                         b->u.tuple->items, b->u.tuple->len);
    case RV_DICT:
        return dict_equal(a->u.dict, b->u.dict);
    case RV_SET:
    case RV_FROZENSET:
        return set_equal(a->u.set, b->u.set);
    case RV_ENUM:
        return a->u.en.type == b->u.en.type && a->u.en.index == b->u.en.index;
    case RV_ENUMTYPE:
        return a->u.etype == b->u.etype;
    case RV_MODULE:
        return a->u.module == b->u.module;
    case RV_SUPER:
        return a->u.super == b->u.super;
    case RV_OVERLOAD:
        return a->u.overload == b->u.overload;
    case RV_GENERATOR:
        return a->u.gen == b->u.gen;
    case RV_STRUCT: {
        size_t i;
        if (a->u.stv->type != b->u.stv->type)
            return 0;
        for (i = 0; i < a->u.stv->type->nfields; i++) {
            if (!RValue_Equal(a->u.stv->cells[i], b->u.stv->cells[i]))
                return 0;
        }
        return 1;
    }
    default:
        return 0;
    }
}

int RValue_Compare(Interp *in, const RValue *a, const RValue *b, int *out)
{
    *out = 0;
    if (RValue_IsNumber(a) && RValue_IsNumber(b)) {
        if (a->kind != RV_FLOAT && b->kind != RV_FLOAT) {
            int64_t x = a->kind == RV_BOOL ? a->u.b : a->u.i;
            int64_t y = b->kind == RV_BOOL ? b->u.b : b->u.i;
            *out = x < y ? -1 : x > y ? 1 : 0;
            return 1;
        }
        else {
            double x = a->kind == RV_FLOAT ? a->u.f : (double)(a->kind == RV_BOOL ? a->u.b : a->u.i);
            double y = b->kind == RV_FLOAT ? b->u.f : (double)(b->kind == RV_BOOL ? b->u.b : b->u.i);
            *out = x < y ? -1 : x > y ? 1 : 0;
            return 1;
        }
    }
    if (a->kind == RV_STR && b->kind == RV_STR) {
        size_t n = a->u.s->length < b->u.s->length ? a->u.s->length : b->u.s->length;
        int cmp = n ? memcmp(a->u.s->data, b->u.s->data, n) : 0;
        if (cmp == 0)
            cmp = a->u.s->length < b->u.s->length ? -1 : a->u.s->length > b->u.s->length ? 1 : 0;
        *out = cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
        return 1;
    }
    if ((a->kind == RV_LIST && b->kind == RV_LIST) ||
        (a->kind == RV_TUPLE && b->kind == RV_TUPLE)) {
        RValue **ai = a->kind == RV_LIST ? a->u.list->items : a->u.tuple->items;
        size_t an = a->kind == RV_LIST ? a->u.list->len : a->u.tuple->len;
        RValue **bi = b->kind == RV_LIST ? b->u.list->items : b->u.tuple->items;
        size_t bn = b->kind == RV_LIST ? b->u.list->len : b->u.tuple->len;
        size_t i;
        for (i = 0; i < an && i < bn; i++) {
            int cmp;
            if (RValue_Equal(ai[i], bi[i]))
                continue;
            if (!RValue_Compare(in, ai[i], bi[i], &cmp))
                return 0;
            *out = cmp;
            return 1;
        }
        *out = an < bn ? -1 : an > bn ? 1 : 0;
        return 1;
    }
    RT_Throw(in, "TypeError", "value has no comparable size");
    return 0;
}

static int seq_index(Interp *in, const RValue *index, int64_t *out, int *ok)
{
    *ok = 1;
    if (index && (index->kind == RV_INT || index->kind == RV_BOOL)) {
        *out = index->kind == RV_INT ? index->u.i : index->u.b;
        return 1;
    }
    *ok = 0;
    RT_Throw(in, "TypeError", "index must be an integer (got '%s')",
             RValue_TypeName(index));
    return 0;
}

static void key_repr(const RValue *key, char *buf, size_t cap)
{
    if (key && key->kind == RV_STR)
        snprintf(buf, cap, "'%s'", key->u.s->data);
    else if (key && key->kind == RV_INT)
        snprintf(buf, cap, "%lld", (long long)key->u.i);
    else if (key && key->kind == RV_BOOL)
        snprintf(buf, cap, "%s", key->u.b ? "true" : "false");
    else if (key && key->kind == RV_FLOAT)
        snprintf(buf, cap, "%g", key->u.f);
    else
        snprintf(buf, cap, "<%s>", RValue_TypeName(key));
}

RValue *RValue_GetItem(Interp *in, RValue *container, RValue *index, int *ok)
{
    int64_t i;
    RValue *value;

    *ok = 1;
    if (!container) {
        *ok = 0;
        return RT_ThrowValue(in, "TypeError", "cannot index null");
    }
    switch (container->kind) {
    case RV_LIST:
    case RV_TUPLE: {
        RValue **items;
        size_t len;
        if (container->kind == RV_LIST) {
            items = container->u.list->items;
            len = container->u.list->len;
        }
        else {
            items = container->u.tuple->items;
            len = container->u.tuple->len;
        }
        if (!seq_index(in, index, &i, ok))
            return NULL;
        if (i < 0)
            i += (int64_t)len;
        if (i < 0 || (size_t)i >= len) {
            *ok = 0;
            return RT_ThrowValue(in, "IndexError", "%s index out of range",
                                 container->kind == RV_LIST ? "list" : "tuple");
        }
        return RValue_Copy(in, items[i]);
    }
    case RV_STR: {
        size_t off, adv = 1;
        if (!seq_index(in, index, &i, ok))
            return NULL;
        if (i < 0)
            i += (int64_t)container->u.s->cp_length;
        if (i < 0 || (size_t)i >= container->u.s->cp_length) {
            *ok = 0;
            return RT_ThrowValue(in, "IndexError", "string index out of range");
        }
        off = AsterString_ByteOffset(container->u.s, (size_t)i);
        Aster_UTF8_Decode(container->u.s->data + off,
                          container->u.s->length - off, &adv);
        return RValue_Str(in, AsterString_FromUTF8(container->u.s->data + off, adv));
    }
    case RV_DICT:
        if (!RValue_Hashable(index)) {
            *ok = 0;
            return RT_ThrowValue(in, "TypeError", "unhashable type: '%s'",
                                 RValue_TypeName(index));
        }
        value = Dict_Lookup(container->u.dict, index);
        if (!value) {
            char buf[128];
            key_repr(index, buf, sizeof buf);
            *ok = 0;
            return RT_ThrowValue(in, "KeyError", "%s", buf);
        }
        return RValue_Copy(in, value);
    case RV_OBJECT:
        if (RValue_HasMethod(in, container, "__getitem__")) {
            RValue *args[1];
            args[0] = index;
            return Interp_CallMethod(in, container, "__getitem__", args, 1);
        }
        *ok = 0;
        return RT_ThrowValue(in, "TypeError", "'%s' object cannot be indexed",
                             RValue_TypeName(container));
    default:
        *ok = 0;
        return RT_ThrowValue(in, "TypeError", "'%s' object cannot be indexed",
                             RValue_TypeName(container));
    }
}

int RValue_SetItem(Interp *in, RValue *container, RValue *index, RValue *value, int *ok)
{
    int64_t i;

    *ok = 1;
    if (!container) {
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot index null");
        return 0;
    }
    switch (container->kind) {
    case RV_LIST:
        if (!seq_index(in, index, &i, ok))
            return 0;
        if (i < 0)
            i += (int64_t)container->u.list->len;
        if (i < 0 || (size_t)i >= container->u.list->len) {
            *ok = 0;
            RT_Throw(in, "IndexError", "list assignment index out of range");
            return 0;
        }
        container->u.list->items[i] = RValue_Copy(in, value);
        return 1;
    case RV_DICT:
        if (!Dict_Set(in, container->u.dict, index, RValue_Copy(in, value), ok))
            return 0;
        return 1;
    case RV_TUPLE:
    case RV_FROZENSET:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' object is immutable",
                 container->kind == RV_TUPLE ? "tuple" : "frozenset");
        return 0;
    case RV_SET:
        *ok = 0;
        RT_Throw(in, "TypeError", "'set' does not support index assignment");
        return 0;
    case RV_STR:
        *ok = 0;
        RT_Throw(in, "TypeError", "'str' object is immutable");
        return 0;
    case RV_OBJECT:
        if (RValue_HasMethod(in, container, "__setitem__")) {
            RValue *args[2];
            args[0] = index;
            args[1] = value;
            if (!Interp_CallMethod(in, container, "__setitem__", args, 2)) {
                *ok = 0;
                return 0;
            }
            return 1;
        }
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' object does not support index assignment",
                 RValue_TypeName(container));
        return 0;
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' object does not support index assignment",
                 RValue_TypeName(container));
        return 0;
    }
}

static int string_contains(AsterString *haystack, AsterString *needle)
{
    size_t i;
    if (needle->length == 0)
        return 1;
    if (needle->length > haystack->length)
        return 0;
    for (i = 0; i + needle->length <= haystack->length; i++) {
        if (memcmp(haystack->data + i, needle->data, needle->length) == 0)
            return 1;
    }
    return 0;
}

int RValue_Contains(Interp *in, const RValue *container, const RValue *item, int *ok)
{
    size_t i;

    *ok = 1;
    if (!container) {
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot use 'in' with null");
        return 0;
    }
    switch (container->kind) {
    case RV_STR:
        if (!item || item->kind != RV_STR) {
            *ok = 0;
            RT_Throw(in, "TypeError", "the left operand of 'in <string>' must be a string");
            return 0;
        }
        return string_contains(container->u.s, item->u.s);
    case RV_LIST:
        for (i = 0; i < container->u.list->len; i++) {
            if (RValue_Equal(container->u.list->items[i], item))
                return 1;
        }
        return 0;
    case RV_TUPLE:
        for (i = 0; i < container->u.tuple->len; i++) {
            if (RValue_Equal(container->u.tuple->items[i], item))
                return 1;
        }
        return 0;
    case RV_SET:
    case RV_FROZENSET:
        if (!RValue_Hashable(item)) {
            *ok = 0;
            RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(item));
            return 0;
        }
        return Set_Has(container->u.set, item);
    case RV_DICT:
        if (!RValue_Hashable(item)) {
            *ok = 0;
            RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(item));
            return 0;
        }
        return Dict_Lookup(container->u.dict, item) != NULL;
    case RV_OBJECT:
        if (RValue_HasMethod(in, container, "__contains__")) {
            RValue *args[1];
            RValue *r;
            args[0] = (RValue *)item;
            r = Interp_CallMethod(in, (RValue *)container, "__contains__", args, 1);
            if (!r) {
                *ok = 0;
                return 0;
            }
            return RValue_Truthy(r);
        }
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' object is not iterable",
                 RValue_TypeName(container));
        return 0;
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' object is not iterable",
                 RValue_TypeName(container));
        return 0;
    }
}

AsterString *RString_Repeat(Interp *in, AsterString *s, int64_t times)
{
    char *buf;
    size_t total;
    size_t i;
    AsterString *out;

    if (times <= 0 || s->length == 0)
        return AsterString_FromCStr("");
    if ((uint64_t)s->length > (uint64_t)SIZE_MAX / (uint64_t)times) {
        RT_Throw(in, "OverflowError", "string is too large");
        return NULL;
    }
    total = s->length * (size_t)times;
    buf = xrealloc(NULL, total + 1);
    for (i = 0; i < (size_t)times; i++)
        memcpy(buf + i * s->length, s->data, s->length);
    buf[total] = 0;
    out = AsterString_FromUTF8(buf, total);
    free(buf);
    return out;
}

double RValue_AsDouble(Interp *in, const RValue *v, int *ok)
{
    *ok = 1;
    switch (v->kind) {
    case RV_INT:
        return (double)v->u.i;
    case RV_FLOAT:
        return v->u.f;
    case RV_BOOL:
        return (double)v->u.b;
    case RV_STR: {
        char *end;
        double d;
        errno = 0;
        d = strtod(v->u.s->data, &end);
        if (end == v->u.s->data || *end != '\0') {
            *ok = 0;
            RT_Throw(in, "ValueError", "cannot convert string to float: '%s'", v->u.s->data);
            return 0;
        }
        return d;
    }
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot convert a non-number to float");
        return 0;
    }
}

int64_t RValue_AsInt(Interp *in, const RValue *v, int *ok)
{
    *ok = 1;
    switch (v->kind) {
    case RV_INT:
        return v->u.i;
    case RV_BOOL:
        return v->u.b;
    case RV_FLOAT:
        if (v->u.f >= 9223372036854775808.0 || v->u.f < -9223372036854775808.0) {
            *ok = 0;
            RT_Throw(in, "OverflowError", "float value is out of int range");
            return 0;
        }
        return (int64_t)v->u.f;
    case RV_STR: {
        char *end;
        long long n;
        errno = 0;
        n = strtoll(v->u.s->data, &end, 10);
        if (end == v->u.s->data || *end != '\0') {
            *ok = 0;
            RT_Throw(in, "ValueError", "cannot convert string to int: '%s'", v->u.s->data);
            return 0;
        }
        if (errno == ERANGE) {
            *ok = 0;
            RT_Throw(in, "OverflowError", "integer overflow");
            return 0;
        }
        return n;
    }
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "cannot convert a non-number to int");
        return 0;
    }
}

/* ---- 값 출력/문자열화 ---- */

typedef struct {
    char *data;
    size_t len, cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    size_t cap;
    if (need <= b->cap)
        return;
    cap = b->cap ? b->cap : 64;
    while (cap < need)
        cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap = cap;
}

static void buf_write(Buf *b, const char *s, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_puts(Buf *b, const char *s)
{
    buf_write(b, s, strlen(s));
}

static void buf_putc(Buf *b, char c)
{
    buf_write(b, &c, 1);
}

static void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[256];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n < sizeof tmp) {
        buf_write(b, tmp, (size_t)n);
        return;
    }
    {
        char *big = xrealloc(NULL, (size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        buf_write(b, big, (size_t)n);
        free(big);
    }
}

static void buf_repr_string(Buf *b, const char *s, size_t n)
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

    buf_putc(b, (char)quote);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == (unsigned char)quote || c == '\\') {
            buf_putc(b, '\\');
            buf_putc(b, (char)c);
        }
        else if (c == '\n') {
            buf_puts(b, "\\n");
        }
        else if (c == '\r') {
            buf_puts(b, "\\r");
        }
        else if (c == '\t') {
            buf_puts(b, "\\t");
        }
        else if (c < 0x20 || c == 0x7f) {
            buf_printf(b, "\\x%02x", c);
        }
        else {
            buf_putc(b, (char)c);
        }
    }
    buf_putc(b, (char)quote);
}

static void render_value(Buf *b, const RValue *v, int repr, int depth);

static void render_tuple(Buf *b, const AsterTuple *tuple, int repr, int depth)
{
    (void)repr;
    if (depth > 8) {
        buf_puts(b, "...");
        return;
    }
    buf_putc(b, '(');
    if (tuple->len == 1) {
        render_value(b, tuple->items[0], 1, depth + 1);
        buf_putc(b, ',');
    }
    else {
        size_t i;
        for (i = 0; i < tuple->len; i++) {
            if (i)
                buf_puts(b, ", ");
            render_value(b, tuple->items[i], 1, depth + 1);
        }
    }
    buf_putc(b, ')');
}

static void render_dict(Buf *b, const AsterDict *dict, int repr, int depth)
{
    size_t i;
    (void)repr;

    if (depth > 8) {
        buf_puts(b, "...");
        return;
    }
    buf_putc(b, '{');
    for (i = 0; i < dict->len; i++) {
        if (i)
            buf_puts(b, ", ");
        render_value(b, dict->entries[i].key, 1, depth + 1);
        buf_puts(b, ": ");
        render_value(b, dict->entries[i].value, 1, depth + 1);
    }
    buf_putc(b, '}');
}

static void render_set(Buf *b, const AsterSet *set, int repr, int depth, int frozen)
{
    size_t i;
    (void)repr;

    if (set->len == 0) {
        buf_puts(b, frozen ? "frozenset()" : "set()");
        return;
    }
    if (depth > 8) {
        buf_puts(b, "...");
        return;
    }
    if (frozen)
        buf_puts(b, "frozenset(");
    buf_putc(b, '{');
    for (i = 0; i < set->len; i++) {
        if (i)
            buf_puts(b, ", ");
        render_value(b, set->items[i], 1, depth + 1);
    }
    buf_putc(b, '}');
    if (frozen)
        buf_putc(b, ')');
}

static void render_struct(Buf *b, const AstStructValue *stv, int repr, int depth)
{
    size_t i;

    if (depth > 8) {
        buf_puts(b, "...");
        return;
    }
    buf_puts(b, stv->type->tag);
    buf_putc(b, '(');
    for (i = 0; i < stv->type->nfields; i++) {
        if (i)
            buf_puts(b, ", ");
        buf_puts(b, stv->type->fields[i].name);
        buf_puts(b, "=");
        render_value(b, stv->cells[i], repr, depth + 1);
    }
    buf_putc(b, ')');
}

static void render_value(Buf *b, const RValue *v, int repr, int depth)
{
    char num[64];

    if (!v) {
        buf_puts(b, "null");
        return;
    }
    switch (v->kind) {
    case RV_NULL:
        buf_puts(b, "null");
        break;
    case RV_INT:
        buf_printf(b, "%lld", (long long)v->u.i);
        break;
    case RV_FLOAT:
        Aster_FormatDouble(num, sizeof num, v->u.f);
        buf_puts(b, num);
        break;
    case RV_BOOL:
        buf_puts(b, v->u.b ? "true" : "false");
        break;
    case RV_STR:
        if (repr)
            buf_repr_string(b, v->u.s->data, v->u.s->length);
        else
            buf_write(b, v->u.s->data, v->u.s->length);
        break;
    case RV_OBJECT:
        buf_printf(b, "<%s object>", v->u.obj->cls->name);
        break;
    case RV_STRUCT:
        render_struct(b, v->u.stv, repr, depth);
        break;
    case RV_LIST: {
        size_t i;
        if (depth > 8) {
            buf_puts(b, "...");
            break;
        }
        buf_putc(b, '[');
        for (i = 0; i < v->u.list->len; i++) {
            if (i)
                buf_puts(b, ", ");
            render_value(b, v->u.list->items[i], 1, depth + 1);
        }
        buf_putc(b, ']');
        break;
    }
    case RV_TUPLE:
        render_tuple(b, v->u.tuple, repr, depth);
        break;
    case RV_DICT:
        render_dict(b, v->u.dict, repr, depth);
        break;
    case RV_SET:
        render_set(b, v->u.set, repr, depth, 0);
        break;
    case RV_FROZENSET:
        render_set(b, v->u.set, repr, depth, 1);
        break;
    case RV_FUNC:
        buf_printf(b, "<fn %s>", v->u.fn->name ? v->u.fn->name : "lambda");
        break;
    case RV_BUILTIN:
        buf_printf(b, "<builtin %s>", v->u.builtin->name);
        break;
    case RV_PTR:
        buf_puts(b, "<pointer>");
        break;
    case RV_EXC: {
        const RValue *msg = v->u.exc->message;
        if (msg && msg->kind == RV_STR && msg->u.s->length > 0) {
            if (repr) {
                buf_puts(b, v->u.exc->name);
                buf_putc(b, '(');
                buf_repr_string(b, msg->u.s->data, msg->u.s->length);
                buf_putc(b, ')');
            }
            else {
                buf_write(b, msg->u.s->data, msg->u.s->length);
            }
        }
        else {
            buf_puts(b, v->u.exc->name);
        }
        break;
    }
    case RV_EXC_CLASS:
        buf_puts(b, v->u.exc_name);
        break;
    case RV_CLASS:
        buf_printf(b, "<class %s>", v->u.cls->name);
        break;
    case RV_STRUCTTYPE:
        buf_printf(b, "<struct %s>", v->u.stype->tag);
        break;
    case RV_MODULE:
        buf_printf(b, "<module %s>", v->u.module->name);
        break;
    case RV_SUPER:
        buf_puts(b, "<super>");
        break;
    case RV_ENUM:
        buf_printf(b, "%s.%s", v->u.en.type->name, v->u.en.name);
        break;
    case RV_ENUMTYPE:
        buf_printf(b, "<enum %s>", v->u.etype->name);
        break;
    case RV_OVERLOAD:
        buf_printf(b, "<fn %s>", v->u.overload->count ? v->u.overload->fns[0]->name
                                                      : "overload");
        break;
    case RV_GENERATOR:
        buf_puts(b, "<generator>");
        break;
    }
}

/* ---- 연산자 훅 (__str__/__repr__) ---- */

static AsterString *object_str_hook(Interp *in, const RValue *v, const char *name)
{
    RValue *r;

    if (!v || v->kind != RV_OBJECT || !RValue_HasMethod(in, v, name))
        return NULL;
    r = Interp_CallMethod(in, (RValue *)v, name, NULL, 0);
    if (!r)
        return NULL;
    if (r->kind == RV_STR)
        return r->u.s;
    RT_Throw(in, "TypeError", "%s must return a string", name);
    return NULL;
}

void RValue_Print(FILE *out, const RValue *v, int repr)
{
    Buf b = {0};
    render_value(&b, v, repr, 0);
    if (b.data)
        fwrite(b.data, 1, b.len, out);
    free(b.data);
}

AsterString *RValue_ToString(Interp *in, const RValue *v)
{
    Buf b = {0};
    AsterString *s;

    if (v && v->kind == RV_STR)
        return v->u.s;
    if (v && v->kind == RV_OBJECT) {
        AsterString *hooked = object_str_hook(in, v, "__str__");
        if (hooked)
            return hooked;
    }
    render_value(&b, v, 0, 0);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    return s;
}

AsterString *RValue_Repr(Interp *in, const RValue *v)
{
    Buf b = {0};
    AsterString *s;

    if (v && v->kind == RV_OBJECT) {
        AsterString *hooked = object_str_hook(in, v, "__repr__");
        if (hooked)
            return hooked;
    }
    render_value(&b, v, 1, 0);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    return s;
}

AsterString *RString_Concat(Interp *in, AsterString *a, AsterString *b)
{
    Buf buf = {0};
    AsterString *s;

    buf_write(&buf, a->data, a->length);
    buf_write(&buf, b->data, b->length);
    s = AsterString_FromUTF8(buf.data ? buf.data : "", buf.len);
    free(buf.data);
    (void)in;
    return s;
}

/* ---- 예외 ---- */

static const char *builtin_exc_parent(const char *name)
{
    if (!strcmp(name, "Exception"))
        return NULL;
    if (!strcmp(name, "ZeroDivisionError") || !strcmp(name, "OverflowError"))
        return "ArithmeticError";
    if (!strcmp(name, "NullPointerError") || !strcmp(name, "RecursionError"))
        return "RuntimeError";
    if (!strcmp(name, "IndexError") || !strcmp(name, "KeyError"))
        return "LookupError";
    if (!strcmp(name, "AssertionError"))
        return "Exception";
    if (!strcmp(name, "ModuleNotFoundError"))
        return "ImportError";
    return "Exception";
}

const char *Exception_Parent(Interp *in, const char *name)
{
    size_t i;
    for (i = 0; i < in->nexc_bases; i++) {
        if (!strcmp(in->exc_bases[i].name, name))
            return in->exc_bases[i].base;
    }
    return builtin_exc_parent(name);
}

void Exception_Register(Interp *in, const char *name, const char *base)
{
    size_t i;
    for (i = 0; i < in->nexc_bases; i++) {
        if (!strcmp(in->exc_bases[i].name, name))
            return;
    }
    if (in->nexc_bases == in->cap_exc_bases) {
        in->cap_exc_bases = in->cap_exc_bases ? in->cap_exc_bases * 2 : 8;
        in->exc_bases = xrealloc(in->exc_bases,
                                 in->cap_exc_bases * sizeof *in->exc_bases);
    }
    in->exc_bases[in->nexc_bases].name = name;
    in->exc_bases[in->nexc_bases].base = base ? base : "Exception";
    in->nexc_bases++;
}

int Exception_Matches(Interp *in, const char *thrown, const char *caught)
{
    const char *cur = thrown;
    int guard = 0;

    while (cur && guard++ < 64) {
        if (!strcmp(cur, caught))
            return 1;
        cur = Exception_Parent(in, cur);
    }
    return 0;
}

RValue *Exception_NameValue(Interp *in, const char *name)
{
    return RValue_ExcClass(in, name);
}

void RT_Throw(Interp *in, const char *name, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    AsterString *s;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    s = AsterString_FromCStr(msg);
    in->exc = RValue_Exception(in, name, RValue_Str(in, s));
    in->sig = SIG_THROW;
}

RValue *RT_ThrowValue(Interp *in, const char *name, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    AsterString *s;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    s = AsterString_FromCStr(msg);
    in->exc = RValue_Exception(in, name, RValue_Str(in, s));
    in->sig = SIG_THROW;
    return RValue_Null(in);
}

/* ---- 내장 함수 ---- */

AstBuiltin *Builtin_New(Interp *in, const char *name, BuiltinFn fn)
{
    AstBuiltin *b = Arena_Alloc(in->arena, sizeof(AstBuiltin));
    b->name = name;
    b->fn = fn;
    return b;
}

static RValue *bi_print(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    (void)self;
    for (i = 0; i < nargs; i++) {
        if (i)
            fputc(' ', stdout);
        if (args[i] && args[i]->kind == RV_OBJECT &&
            RValue_HasMethod(in, args[i], "__str__")) {
            AsterString *s = RValue_ToString(in, args[i]);
            if (in->sig != SIG_OK)
                return RValue_Null(in);
            fwrite(s->data, 1, s->length, stdout);
        }
        else {
            RValue_Print(stdout, args[i], 0);
        }
    }
    return RValue_Null(in);
}

static RValue *bi_println(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    bi_print(in, self, args, nargs);
    fputc('\n', stdout);
    return RValue_Null(in);
}

static RValue *bi_eprint(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    (void)self;
    for (i = 0; i < nargs; i++) {
        if (i)
            fputc(' ', stderr);
        RValue_Print(stderr, args[i], 0);
    }
    return RValue_Null(in);
}

static RValue *bi_eprintln(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    bi_eprint(in, self, args, nargs);
    fputc('\n', stderr);
    return RValue_Null(in);
}

static RValue *zero_value(Interp *in, RValue *v);

static RValue *zero_struct(Interp *in, AstStructValue *stv)
{
    AstStructValue *copy = StructValue_New(in, stv->type);
    size_t i;

    for (i = 0; i < stv->type->nfields; i++)
        copy->cells[i] = zero_value(in, stv->cells[i]);
    return RValue_Struct(in, copy);
}

static RValue *zero_value(Interp *in, RValue *v)
{
    if (!v)
        return RValue_Null(in);
    switch (v->kind) {
    case RV_NULL:
    case RV_PTR:
        return RValue_Null(in);
    case RV_INT:
        return RValue_Int(in, 0);
    case RV_FLOAT:
        return RValue_Float(in, 0.0);
    case RV_BOOL:
        return RValue_Bool(in, 0);
    case RV_STR:
        return RValue_Str(in, AsterString_FromCStr(""));
    case RV_STRUCT:
        return zero_struct(in, v->u.stv);
    default:
        return RT_ThrowValue(in, "ValueError", "this value cannot be initialized with memset");
    }
}

static RValue *bi_memset(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t value;

    (void)self;
    if (nargs != 3)
        return RT_ThrowValue(in, "TypeError", "memset requires 3 arguments");
    if (args[0]->kind != RV_PTR || !args[0]->u.ptr.slot)
        return RT_ThrowValue(in, "TypeError", "the first argument of memset must be a pointer");
    value = RValue_AsInt(in, args[1], &ok);
    if (!ok)
        return NULL;
    if (value != 0)
        return RT_ThrowValue(in, "ValueError", "memset currently only supports zero initialization");
    *args[0]->u.ptr.slot = zero_value(in, *args[0]->u.ptr.slot);
    return args[0];
}

static AstFn *make_builtin_method(Interp *in, const char *name, BuiltinFn fn)
{
    AstFn *f = Arena_Alloc(in->arena, sizeof(AstFn));
    f->kind = FN_BUILTIN;
    f->name = name;
    f->builtin.name = name;
    f->builtin.fn = fn;
    return f;
}

/* iterable을 값 배열로 펼친다. 반환 배열은 호출자가 free한다. */
static int iter_values(Interp *in, RValue *src, RValue ***out, size_t *nout)
{
    RValue **items = NULL;
    size_t n = 0;
    size_t i;

    *out = NULL;
    *nout = 0;
    if (!src) {
        RT_Throw(in, "TypeError", "null is not iterable");
        return 0;
    }
    switch (src->kind) {
    case RV_LIST:
        n = src->u.list->len;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = src->u.list->items[i];
        break;
    case RV_TUPLE:
        n = src->u.tuple->len;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = src->u.tuple->items[i];
        break;
    case RV_SET:
    case RV_FROZENSET:
        n = src->u.set->len;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = src->u.set->items[i];
        break;
    case RV_DICT:
        n = src->u.dict->len;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = src->u.dict->entries[i].key;
        break;
    case RV_STR: {
        size_t off = 0;
        n = src->u.s->cp_length;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++) {
            size_t adv = 1;
            Aster_UTF8_Decode(src->u.s->data + off, src->u.s->length - off, &adv);
            items[i] = RValue_Str(in, AsterString_FromUTF8(src->u.s->data + off, adv));
            off += adv;
        }
        break;
    }
    case RV_GENERATOR:
    case RV_OBJECT: {
        IterCursor cur;
        size_t cap = 0;
        if (!Iter_Begin(in, src, &cur))
            return 0;
        for (;;) {
            int ended = 0, ok = 1;
            RValue *v = Iter_Next(in, &cur, &ended, &ok);
            if (!ok) {
                free(items);
                return 0;
            }
            if (ended)
                break;
            if (n == cap) {
                cap = cap ? cap * 2 : 8;
                items = xrealloc(items, cap * sizeof(RValue *));
            }
            items[n++] = v;
        }
        break;
    }
    default:
        RT_Throw(in, "TypeError", "'%s' object is not iterable", RValue_TypeName(src));
        return 0;
    }
    *out = items;
    *nout = n;
    return 1;
}

static RValue *sequence_from_args(Interp *in, RValue **args, size_t nargs, int as_tuple)
{
    RValue **items;
    size_t n;

    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "too many arguments");
    if (nargs == 0) {
        items = NULL;
        n = 0;
    }
    else if (!iter_values(in, args[0], &items, &n)) {
        return NULL;
    }
    if (as_tuple) {
        AsterTuple *tuple = Tuple_New(in, n);
        size_t i;
        for (i = 0; i < n; i++)
            tuple->items[i] = RValue_Copy(in, items[i]);
        free(items);
        return RValue_Tuple(in, tuple);
    }
    else {
        AsterList *list = List_New(in);
        size_t i;
        for (i = 0; i < n; i++)
            List_Append(in, list, RValue_Copy(in, items[i]));
        free(items);
        return RValue_List(in, list);
    }
}

static RValue *bi_len(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t n;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "len takes exactly 1 argument");
    n = RValue_Length(in, args[0], &ok);
    if (!ok)
        return NULL;
    return RValue_Int(in, n);
}

static RValue *bi_typename(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "typeName takes exactly 1 argument");
    return RValue_Str(in, AsterString_FromCStr(RValue_TypeName(args[0])));
}

static RValue *bi_repr(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "repr takes exactly 1 argument");
    return RValue_Str(in, RValue_Repr(in, args[0]));
}

static RValue *bi_chr(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t cp;
    char buf[4];
    size_t n;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "chr takes exactly 1 argument");
    cp = RValue_AsInt(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (cp < 0 || cp > 0x10FFFF) {
        RT_Throw(in, "ValueError", "chr argument must be between 0 and 0x10FFFF");
        return NULL;
    }
    n = Aster_UTF8_Encode((uint32_t)cp, buf);
    return RValue_Str(in, AsterString_FromUTF8(buf, n));
}

static RValue *bi_ord(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterString *s;
    uint32_t cp;
    size_t adv;
    (void)self;
    if (nargs != 1 || !args[0] || args[0]->kind != RV_STR)
        return RT_ThrowValue(in, "TypeError", "ord takes exactly 1 string argument");
    s = args[0]->u.s;
    if (s->cp_length != 1) {
        RT_Throw(in, "ValueError", "ord requires a single-character string");
        return NULL;
    }
    cp = Aster_UTF8_Decode(s->data, s->length, &adv);
    return RValue_Int(in, (int64_t)cp);
}

/* ---- 표준 라이브러리(aster.*)가 쓰는 플랫폼 헬퍼 ---- */

#if defined(_WIN32)
typedef struct AsterFileTime {
    unsigned long low;
    unsigned long high;
} AsterFileTime;
__declspec(dllimport) void __stdcall GetSystemTimeAsFileTime(AsterFileTime *ft);
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
__declspec(dllimport) unsigned long __stdcall GetCurrentProcessId(void);
__declspec(dllimport) unsigned long __stdcall GetCurrentDirectoryA(unsigned long size, char *buf);
__declspec(dllimport) int __cdecl _putenv_s(const char *name, const char *value);
#endif

static RValue *bi_now_ms(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t ms;
    (void)self;
    (void)args;
    if (nargs != 0)
        return RT_ThrowValue(in, "TypeError", "nowMillis takes no arguments");
#if defined(_WIN32)
    {
        AsterFileTime ft;
        unsigned long long ticks;
        GetSystemTimeAsFileTime(&ft);
        ticks = ((unsigned long long)ft.high << 32) | ft.low;
        ms = (int64_t)(ticks / 10000ULL) - 11644473600000LL;
    }
#else
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
#endif
    return RValue_Int(in, ms);
}

static RValue *bi_sleep_ms(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t ms;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "sleepMillis takes exactly 1 argument");
    ms = RValue_AsInt(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (ms < 0)
        ms = 0;
#if defined(_WIN32)
    Sleep((unsigned long)ms);
#else
    {
        struct timespec req;
        req.tv_sec = (time_t)(ms / 1000);
        req.tv_nsec = (long)((ms % 1000) * 1000000);
        nanosleep(&req, NULL);
    }
#endif
    return RValue_Null(in);
}

static RValue *bi_cwd(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    char buf[4096];
    const char *p = NULL;
    (void)self;
    (void)args;
    if (nargs != 0)
        return RT_ThrowValue(in, "TypeError", "cwd takes no arguments");
#if defined(_WIN32)
    if (GetCurrentDirectoryA(sizeof buf, buf))
        p = buf;
#else
    p = getcwd(buf, sizeof buf);
#endif
    if (!p)
        return RT_ThrowValue(in, "OSError", "cannot read the current directory");
    return RValue_Str(in, AsterString_FromCStr(p));
}

static RValue *bi_pid(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    (void)args;
    if (nargs != 0)
        return RT_ThrowValue(in, "TypeError", "pid takes no arguments");
#if defined(_WIN32)
    return RValue_Int(in, (int64_t)GetCurrentProcessId());
#else
    return RValue_Int(in, (int64_t)getpid());
#endif
}

static RValue *bi_platform(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    (void)args;
    if (nargs != 0)
        return RT_ThrowValue(in, "TypeError", "platform takes no arguments");
#if defined(_WIN32)
    return RValue_Str(in, AsterString_FromCStr("windows"));
#elif defined(__APPLE__)
    return RValue_Str(in, AsterString_FromCStr("macos"));
#else
    return RValue_Str(in, AsterString_FromCStr("linux"));
#endif
}

static RValue *bi_setenv(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    if (nargs != 2 || !args[0] || args[0]->kind != RV_STR ||
        !args[1] || args[1]->kind != RV_STR)
        return RT_ThrowValue(in, "TypeError", "setenv takes exactly 2 string arguments");
#if defined(_WIN32)
    return RValue_Int(in, _putenv_s(args[0]->u.s->data, args[1]->u.s->data));
#else
    return RValue_Int(in, setenv(args[0]->u.s->data, args[1]->u.s->data, 1));
#endif
}

static RValue *bi_next(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ended = 0;
    RValue *v;
    (void)self;
    if (nargs != 1 || !args[0] || args[0]->kind != RV_GENERATOR)
        return RT_ThrowValue(in, "TypeError", "next takes exactly 1 generator argument");
    v = Generator_Next(in, args[0]->u.gen, &ended);
    if (in->sig != SIG_OK)
        return NULL;
    if (ended) {
        RT_Throw(in, "StopIteration", "generator is exhausted");
        return NULL;
    }
    return v;
}

static RValue *bi_list(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    return sequence_from_args(in, args, nargs, 0);
}

static RValue *bi_tuple(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    return sequence_from_args(in, args, nargs, 1);
}

static RValue *bi_set(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue **items;
    size_t n;
    size_t i;
    AsterSet *set;
    int ok;

    (void)self;
    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "too many arguments");
    if (nargs == 0) {
        items = NULL;
        n = 0;
    }
    else if (!iter_values(in, args[0], &items, &n)) {
        return NULL;
    }
    set = Set_New(in);
    for (i = 0; i < n; i++) {
        if (!Set_Add(in, set, RValue_Copy(in, items[i]), &ok)) {
            free(items);
            return NULL;
        }
    }
    free(items);
    return RValue_Set(in, set);
}

static RValue *bi_frozenset(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *set = bi_set(in, self, args, nargs);
    if (!set)
        return NULL;
    return RValue_FrozenSet(in, set->u.set);
}

static RValue *bi_dict(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterDict *dict = Dict_New(in);
    int ok;

    (void)self;
    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "too many arguments");
    if (nargs == 1) {
        if (args[0]->kind == RV_DICT) {
            size_t i;
            for (i = 0; i < args[0]->u.dict->len; i++) {
                if (!Dict_Set(in, dict, args[0]->u.dict->entries[i].key,
                              RValue_Copy(in, args[0]->u.dict->entries[i].value), &ok))
                    return NULL;
            }
        }
        else {
            RValue **items;
            size_t n;
            size_t i;
            if (!iter_values(in, args[0], &items, &n))
                return NULL;
            for (i = 0; i < n; i++) {
                RValue *pair = items[i];
                if (!pair || (pair->kind != RV_LIST && pair->kind != RV_TUPLE)) {
                    free(items);
                    return RT_ThrowValue(in, "TypeError",
                                         "dict requires an iterable of (key, value) pairs");
                }
                if ((pair->kind == RV_LIST ? pair->u.list->len : pair->u.tuple->len) != 2) {
                    free(items);
                    return RT_ThrowValue(in, "ValueError",
                                         "dict element does not have length 2");
                }
                if (!Dict_Set(in, dict,
                              RValue_Copy(in, pair->kind == RV_LIST
                                                  ? pair->u.list->items[0]
                                                  : pair->u.tuple->items[0]),
                              RValue_Copy(in, pair->kind == RV_LIST
                                                  ? pair->u.list->items[1]
                                                  : pair->u.tuple->items[1]),
                              &ok)) {
                    free(items);
                    return NULL;
                }
            }
            free(items);
        }
    }
    return RValue_Dict(in, dict);
}

static int method_index(Interp *in, RValue *v, int64_t *out)
{
    if (v && v->kind == RV_INT) {
        *out = v->u.i;
        return 1;
    }
    if (v && v->kind == RV_BOOL) {
        *out = v->u.b;
        return 1;
    }
    RT_Throw(in, "TypeError", "index must be an integer");
    return 0;
}

static size_t dict_index(const AsterDict *dict, const RValue *key, int *found)
{
    long i = Dict_FindIndex(dict, key);
    *found = i >= 0;
    return i >= 0 ? (size_t)i : 0;
}

/* ---- list 메서드 ---- */

static RValue *list_m_append(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "append() takes exactly 1 argument");
    List_Append(in, self->u.list, RValue_Copy(in, args[0]));
    return RValue_Null(in);
}

static RValue *list_m_extend(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue **items;
    size_t n;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "extend() takes exactly 1 argument");
    if (!iter_values(in, args[0], &items, &n))
        return NULL;
    for (i = 0; i < n; i++)
        List_Append(in, self->u.list, RValue_Copy(in, items[i]));
    free(items);
    return RValue_Null(in);
}

static RValue *list_m_insert(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t index;
    size_t len;

    if (nargs != 2)
        return RT_ThrowValue(in, "TypeError", "insert() takes exactly 2 arguments");
    if (!method_index(in, args[0], &index))
        return NULL;
    len = self->u.list->len;
    if (index < 0) {
        index += (int64_t)len;
        if (index < 0)
            index = 0;
    }
    if ((size_t)index > len)
        index = (int64_t)len;
    List_Insert(in, self->u.list, (size_t)index, RValue_Copy(in, args[1]));
    return RValue_Null(in);
}

static RValue *list_m_pop(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t index = -1;
    RValue *item;

    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "pop() takes at most 1 argument");
    if (nargs == 1 && !method_index(in, args[0], &index))
        return NULL;
    if (self->u.list->len == 0)
        return RT_ThrowValue(in, "IndexError", "cannot pop from an empty list");
    if (index < 0)
        index += (int64_t)self->u.list->len;
    if (index < 0 || (size_t)index >= self->u.list->len)
        return RT_ThrowValue(in, "IndexError", "pop index out of range");
    item = self->u.list->items[index];
    List_RemoveAt(self->u.list, (size_t)index);
    return item;
}

static RValue *list_m_remove(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "remove() takes exactly 1 argument");
    for (i = 0; i < self->u.list->len; i++) {
        if (RValue_Equal(self->u.list->items[i], args[0])) {
            List_RemoveAt(self->u.list, i);
            return RValue_Null(in);
        }
    }
    return RT_ThrowValue(in, "ValueError", "list.remove(x): x not in list");
}

static RValue *list_m_clear(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    self->u.list->len = 0;
    return RValue_Null(in);
}

static RValue *list_m_index(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "index() takes exactly 1 argument");
    for (i = 0; i < self->u.list->len; i++) {
        if (RValue_Equal(self->u.list->items[i], args[0]))
            return RValue_Int(in, (int64_t)i);
    }
    return RT_ThrowValue(in, "ValueError", "x is not in list");
}

static RValue *list_m_count(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    int64_t count = 0;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "count() takes exactly 1 argument");
    for (i = 0; i < self->u.list->len; i++) {
        if (RValue_Equal(self->u.list->items[i], args[0]))
            count++;
    }
    return RValue_Int(in, count);
}

static RValue *list_m_copy(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.list->len; i++)
        List_Append(in, list, RValue_Copy(in, self->u.list->items[i]));
    return RValue_List(in, list);
}

static RValue *list_m_map(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *out;
    size_t i;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "map() takes exactly 1 argument");
    if (!args[0] || (args[0]->kind != RV_FUNC && args[0]->kind != RV_BUILTIN &&
                     args[0]->kind != RV_OVERLOAD))
        return RT_ThrowValue(in, "TypeError", "map() expects a function");
    out = List_New(in);
    for (i = 0; i < self->u.list->len; i++) {
        RValue *call_args[1];
        RValue *mapped;
        call_args[0] = self->u.list->items[i];
        mapped = Interp_CallValue(in, args[0], call_args, 1);
        if (!mapped)
            return NULL;
        List_Append(in, out, RValue_Copy(in, mapped));
    }
    return RValue_List(in, out);
}

static RValue *list_m_filter(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *out;
    size_t i;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "filter() takes exactly 1 argument");
    if (!args[0] || (args[0]->kind != RV_FUNC && args[0]->kind != RV_BUILTIN &&
                     args[0]->kind != RV_OVERLOAD))
        return RT_ThrowValue(in, "TypeError", "filter() expects a function");
    out = List_New(in);
    for (i = 0; i < self->u.list->len; i++) {
        RValue *call_args[1];
        RValue *verdict;
        call_args[0] = self->u.list->items[i];
        verdict = Interp_CallValue(in, args[0], call_args, 1);
        if (!verdict)
            return NULL;
        if (RValue_Truthy(verdict))
            List_Append(in, out, RValue_Copy(in, self->u.list->items[i]));
    }
    return RValue_List(in, out);
}

static RValue *list_m_reverse(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i, j;
    (void)args;
    (void)nargs;
    for (i = 0, j = self->u.list->len; i + 1 < j; i++, j--) {
        RValue *tmp = self->u.list->items[i];
        self->u.list->items[i] = self->u.list->items[j - 1];
        self->u.list->items[j - 1] = tmp;
    }
    return RValue_Null(in);
}

static RValue *list_m_sort(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = self->u.list;
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 1; i < list->len; i++) {
        RValue *item = list->items[i];
        size_t j = i;
        while (j > 0) {
            int cmp;
            if (!RValue_Compare(in, list->items[j - 1], item, &cmp))
                return NULL;
            if (cmp <= 0)
                break;
            list->items[j] = list->items[j - 1];
            j--;
        }
        list->items[j] = item;
    }
    return RValue_Null(in);
}

/* ---- tuple 메서드 ---- */

static RValue *tuple_m_index(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "index() takes exactly 1 argument");
    for (i = 0; i < self->u.tuple->len; i++) {
        if (RValue_Equal(self->u.tuple->items[i], args[0]))
            return RValue_Int(in, (int64_t)i);
    }
    return RT_ThrowValue(in, "ValueError", "tuple.index(x): x not in tuple");
}

static RValue *tuple_m_count(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    int64_t count = 0;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "count() takes exactly 1 argument");
    for (i = 0; i < self->u.tuple->len; i++) {
        if (RValue_Equal(self->u.tuple->items[i], args[0]))
            count++;
    }
    return RValue_Int(in, count);
}

/* ---- dict 메서드 ---- */

static RValue *dict_m_get(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *value;

    if (nargs < 1 || nargs > 2)
        return RT_ThrowValue(in, "TypeError", "get() takes 1 or 2 arguments");
    if (!RValue_Hashable(args[0]))
        return RT_ThrowValue(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(args[0]));
    value = Dict_Lookup(self->u.dict, args[0]);
    if (value)
        return RValue_Copy(in, value);
    if (nargs == 2)
        return args[1];
    return RValue_Null(in);
}

static RValue *dict_m_keys(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.dict->len; i++)
        List_Append(in, list, RValue_Copy(in, self->u.dict->entries[i].key));
    return RValue_List(in, list);
}

static RValue *dict_m_values(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.dict->len; i++)
        List_Append(in, list, RValue_Copy(in, self->u.dict->entries[i].value));
    return RValue_List(in, list);
}

static RValue *dict_m_items(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.dict->len; i++) {
        AsterTuple *pair = Tuple_New(in, 2);
        pair->items[0] = RValue_Copy(in, self->u.dict->entries[i].key);
        pair->items[1] = RValue_Copy(in, self->u.dict->entries[i].value);
        List_Append(in, list, RValue_Tuple(in, pair));
    }
    return RValue_List(in, list);
}

static RValue *dict_m_pop(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int found;
    size_t index;
    RValue *value;

    if (nargs < 1 || nargs > 2)
        return RT_ThrowValue(in, "TypeError", "pop() takes 1 or 2 arguments");
    if (!RValue_Hashable(args[0]))
        return RT_ThrowValue(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(args[0]));
    index = dict_index(self->u.dict, args[0], &found);
    if (!found) {
        if (nargs == 2)
            return args[1];
        {
            char buf[128];
            key_repr(args[0], buf, sizeof buf);
            return RT_ThrowValue(in, "KeyError", "%s", buf);
        }
    }
    value = self->u.dict->entries[index].value;
    Dict_RemoveAt(self->u.dict, index);
    return value;
}

static RValue *dict_m_setdefault(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *value;
    RValue *def = NULL;
    int ok;

    if (nargs < 1 || nargs > 2)
        return RT_ThrowValue(in, "TypeError", "setdefault() takes 1 or 2 arguments");
    if (!RValue_Hashable(args[0]))
        return RT_ThrowValue(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(args[0]));
    value = Dict_Lookup(self->u.dict, args[0]);
    if (value)
        return RValue_Copy(in, value);
    if (nargs == 2)
        def = RValue_Copy(in, args[1]);
    else
        def = RValue_Null(in);
    if (!Dict_Set(in, self->u.dict, RValue_Copy(in, args[0]), def, &ok))
        return NULL;
    return def;
}

static RValue *dict_m_update(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "update() takes exactly 1 argument");
    if (args[0]->kind == RV_DICT) {
        size_t i;
        for (i = 0; i < args[0]->u.dict->len; i++) {
            if (!Dict_Set(in, self->u.dict,
                          RValue_Copy(in, args[0]->u.dict->entries[i].key),
                          RValue_Copy(in, args[0]->u.dict->entries[i].value), &ok))
                return NULL;
        }
    }
    else {
        RValue **items;
        size_t n;
        size_t i;
        if (!iter_values(in, args[0], &items, &n))
            return NULL;
        for (i = 0; i < n; i++) {
            RValue *pair = items[i];
            size_t plen = pair && pair->kind == RV_LIST ? pair->u.list->len
                           : pair && pair->kind == RV_TUPLE ? pair->u.tuple->len : 0;
            if (plen != 2) {
                free(items);
                return RT_ThrowValue(in, "ValueError",
                                     "update() requires (key, value) pairs");
            }
            if (!Dict_Set(in, self->u.dict,
                          RValue_Copy(in, pair->kind == RV_LIST
                                              ? pair->u.list->items[0]
                                              : pair->u.tuple->items[0]),
                          RValue_Copy(in, pair->kind == RV_LIST
                                              ? pair->u.list->items[1]
                                              : pair->u.tuple->items[1]), &ok)) {
                free(items);
                return NULL;
            }
        }
        free(items);
    }
    return RValue_Null(in);
}

static RValue *dict_m_clear(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    Dict_Clear(self->u.dict);
    return RValue_Null(in);
}

static RValue *dict_m_copy(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterDict *dict = Dict_New(in);
    int ok;
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.dict->len; i++) {
        if (!Dict_Set(in, dict, RValue_Copy(in, self->u.dict->entries[i].key),
                      RValue_Copy(in, self->u.dict->entries[i].value), &ok))
            return NULL;
    }
    return RValue_Dict(in, dict);
}

static RValue *set_result(Interp *in, RValue *self, AsterSet *set)
{
    return self->kind == RV_FROZENSET ? RValue_FrozenSet(in, set) : RValue_Set(in, set);
}

/* ---- set 메서드 ---- */

static RValue *set_m_add(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "add() takes exactly 1 argument");
    if (!Set_Add(in, self->u.set, RValue_Copy(in, args[0]), &ok))
        return NULL;
    return RValue_Null(in);
}

static RValue *set_m_remove(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "remove() takes exactly 1 argument");
    for (i = 0; i < self->u.set->len; i++) {
        if (RValue_Equal(self->u.set->items[i], args[0])) {
            Set_RemoveAt(self->u.set, i);
            return RValue_Null(in);
        }
    }
    return RT_ThrowValue(in, "KeyError", "set.remove(x): x not in set");
}

static RValue *set_m_discard(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "discard() takes exactly 1 argument");
    for (i = 0; i < self->u.set->len; i++) {
        if (RValue_Equal(self->u.set->items[i], args[0])) {
            Set_RemoveAt(self->u.set, i);
            return RValue_Null(in);
        }
    }
    return RValue_Null(in);
}

static RValue *set_m_pop(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *item;
    (void)args;
    (void)nargs;
    if (self->u.set->len == 0)
        return RT_ThrowValue(in, "KeyError", "pop from an empty set");
    item = self->u.set->items[0];
    Set_RemoveAt(self->u.set, 0);
    return item;
}

static RValue *set_m_clear(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    Set_Clear(self->u.set);
    return RValue_Null(in);
}

static RValue *set_m_copy(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *set = Set_New(in);
    int add_ok;
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.set->len; i++)
        Set_Add(in, set, RValue_Copy(in, self->u.set->items[i]), &add_ok);
    return set_result(in, self, set);
}

static AsterSet *set_like_from(Interp *in, RValue *src, int *ok)
{
    AsterSet *set = Set_New(in);
    RValue **items;
    size_t n;
    size_t i;
    int add_ok;

    *ok = 0;
    if (!iter_values(in, src, &items, &n))
        return NULL;
    for (i = 0; i < n; i++) {
        if (!Set_Add(in, set, RValue_Copy(in, items[i]), &add_ok)) {
            free(items);
            return NULL;
        }
    }
    free(items);
    *ok = 1;
    return set;
}

static RValue *set_m_union(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *set = Set_New(in);
    AsterSet *other;
    int ok;
    size_t i;
    int add_ok;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "union() takes exactly 1 argument");
    other = set_like_from(in, args[0], &ok);
    if (!ok)
        return NULL;
    for (i = 0; i < self->u.set->len; i++)
        Set_Add(in, set, RValue_Copy(in, self->u.set->items[i]), &add_ok);
    for (i = 0; i < other->len; i++)
        Set_Add(in, set, RValue_Copy(in, other->items[i]), &add_ok);
    return set_result(in, self, set);
}

static RValue *set_m_intersection(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *set = Set_New(in);
    AsterSet *other;
    int ok;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "intersection() takes exactly 1 argument");
    other = set_like_from(in, args[0], &ok);
    if (!ok)
        return NULL;
    for (i = 0; i < self->u.set->len; i++) {
        if (Set_Has(other, self->u.set->items[i]))
            Set_Add(in, set, RValue_Copy(in, self->u.set->items[i]), &ok);
    }
    return set_result(in, self, set);
}

static RValue *set_m_difference(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *set = Set_New(in);
    AsterSet *other;
    int ok;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "difference() takes exactly 1 argument");
    other = set_like_from(in, args[0], &ok);
    if (!ok)
        return NULL;
    for (i = 0; i < self->u.set->len; i++) {
        if (!Set_Has(other, self->u.set->items[i]))
            Set_Add(in, set, RValue_Copy(in, self->u.set->items[i]), &ok);
    }
    return set_result(in, self, set);
}

static RValue *set_m_issubset(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *other;
    int ok;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "issubset() takes exactly 1 argument");
    other = set_like_from(in, args[0], &ok);
    if (!ok)
        return NULL;
    for (i = 0; i < self->u.set->len; i++) {
        if (!Set_Has(other, self->u.set->items[i]))
            return RValue_Bool(in, 0);
    }
    return RValue_Bool(in, 1);
}

static RValue *set_m_issuperset(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterSet *other;
    int ok;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "issuperset() takes exactly 1 argument");
    other = set_like_from(in, args[0], &ok);
    if (!ok)
        return NULL;
    for (i = 0; i < other->len; i++) {
        if (!Set_Has(self->u.set, other->items[i]))
            return RValue_Bool(in, 0);
    }
    return RValue_Bool(in, 1);
}

/* ---- 문자열 메서드 ---- */

static int str_arg(Interp *in, RValue *v, const char *what, AsterString **out)
{
    if (!v || v->kind != RV_STR) {
        RT_Throw(in, "TypeError", "%s must be a string", what);
        return 0;
    }
    *out = v->u.s;
    return 1;
}

static int is_space_byte(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static int find_sub(const char *hay, size_t hn, const char *needle, size_t nn, size_t from)
{
    size_t i;
    if (nn == 0)
        return (int)(from <= hn ? from : hn);
    if (nn > hn)
        return -1;
    for (i = from; i + nn <= hn; i++) {
        if (memcmp(hay + i, needle, nn) == 0)
            return (int)i;
    }
    return -1;
}

static RValue *str_case_convert(Interp *in, RValue *self, int upper)
{
    Buf b = {0};
    size_t i = 0, n = self->u.s->length;
    while (i < n) {
        size_t adv = 1, out_n;
        uint32_t cp = Aster_UTF8_Decode(self->u.s->data + i, n - i, &adv);
        char out[4];
        if (upper && cp == 0xDF) {
            buf_write(&b, "SS", 2);
            i += adv;
            continue;
        }
        cp = upper ? Aster_ToUpperCP(cp) : Aster_ToLowerCP(cp);
        out_n = Aster_UTF8_Encode(cp, out);
        buf_write(&b, out, out_n);
        i += adv;
    }
    {
        AsterString *s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
        free(b.data);
        return RValue_Str(in, s);
    }
}

static RValue *str_m_upper(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    return str_case_convert(in, self, 1);
}

static RValue *str_m_lower(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    return str_case_convert(in, self, 0);
}

static RValue *str_m_strip(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    const char *d = self->u.s->data;
    size_t n = self->u.s->length, start = 0, end = n;
    size_t adv;
    uint32_t cp;
    (void)args;
    (void)nargs;
    while (start < end) {
        cp = Aster_UTF8_Decode(d + start, end - start, &adv);
        if (!Aster_IsSpaceCP(cp))
            break;
        start += adv;
    }
    while (end > start) {
        size_t back = 1, cand;
        uint32_t c;
        /* 마지막 코드 포인트의 시작 바이트를 찾는다. */
        while (end - back > start && ((unsigned char)d[end - back] & 0xC0) == 0x80)
            back++;
        cand = end - back;
        c = Aster_UTF8_Decode(d + cand, end - cand, &adv);
        if (!Aster_IsSpaceCP(c))
            break;
        end = cand;
    }
    return RValue_Str(in, AsterString_FromUTF8(d + start, end - start));
}

static RValue *str_m_split(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    const char *d = self->u.s->data;
    size_t n = self->u.s->length;

    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "split() takes at most 1 argument");
    if (nargs == 1 && args[0]->kind != RV_NULL) {
        AsterString *sep;
        size_t i = 0;
        if (!str_arg(in, args[0], "split separator", &sep))
            return NULL;
        if (sep->length == 0)
            return RT_ThrowValue(in, "ValueError", "cannot split with an empty separator");
        for (;;) {
            int found = find_sub(d, n, sep->data, sep->length, i);
            if (found < 0) {
                List_Append(in, list, RValue_Str(in, AsterString_FromUTF8(d + i, n - i)));
                break;
            }
            List_Append(in, list, RValue_Str(in, AsterString_FromUTF8(d + i,
                                                                     (size_t)found - i)));
            i = (size_t)found + sep->length;
        }
        return RValue_List(in, list);
    }
    {
        size_t i = 0;
        while (i < n) {
            size_t start, adv;
            uint32_t cp;
            while (i < n) {
                cp = Aster_UTF8_Decode(d + i, n - i, &adv);
                if (!Aster_IsSpaceCP(cp))
                    break;
                i += adv;
            }
            start = i;
            while (i < n) {
                cp = Aster_UTF8_Decode(d + i, n - i, &adv);
                if (Aster_IsSpaceCP(cp))
                    break;
                i += adv;
            }
            if (i > start)
                List_Append(in, list, RValue_Str(in, AsterString_FromUTF8(d + start,
                                                                         i - start)));
        }
    }
    return RValue_List(in, list);
}

static RValue *str_m_join(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue **items;
    size_t n, i;
    Buf b = {0};
    AsterString *s;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "join() takes exactly 1 argument");
    if (!iter_values(in, args[0], &items, &n))
        return NULL;
    for (i = 0; i < n; i++) {
        if (!items[i] || items[i]->kind != RV_STR) {
            free(items);
            free(b.data);
            return RT_ThrowValue(in, "TypeError", "join() elements must be strings");
        }
        if (i)
            buf_write(&b, self->u.s->data, self->u.s->length);
        buf_write(&b, items[i]->u.s->data, items[i]->u.s->length);
    }
    free(items);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    return RValue_Str(in, s);
}

static RValue *str_m_replace(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterString *old, *new_s;
    Buf b = {0};
    size_t i = 0, n = self->u.s->length;
    int64_t limit = -1;
    AsterString *s;

    if (nargs < 2 || nargs > 3)
        return RT_ThrowValue(in, "TypeError", "replace() takes 2 or 3 arguments");
    if (!str_arg(in, args[0], "replace() old value", &old) ||
        !str_arg(in, args[1], "replace() new value", &new_s))
        return NULL;
    if (nargs == 3) {
        int ok;
        limit = RValue_AsInt(in, args[2], &ok);
        if (!ok)
            return NULL;
        if (limit < 0)
            limit = -1;
    }
    while (i < n) {
        int found = old->length ? find_sub(self->u.s->data, n, old->data, old->length, i)
                                : (int)i;
        if (found < 0 || limit == 0) {
            buf_write(&b, self->u.s->data + i, n - i);
            i = n;
            break;
        }
        buf_write(&b, self->u.s->data + i, (size_t)found - i);
        buf_write(&b, new_s->data, new_s->length);
        i = (size_t)found + (old->length ? old->length : 1);
        if (limit > 0)
            limit--;
        if (old->length == 0 && i < n) {
            buf_putc(&b, self->u.s->data[i]);
            i++;
        }
    }
    if (n == 0 && old->length == 0)
        buf_write(&b, new_s->data, new_s->length);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    return RValue_Str(in, s);
}

static RValue *string_predicate(Interp *in, RValue *self, RValue **args, size_t nargs,
                                int from_end)
{
    AsterString *sub;
    int found;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "startswith/endswith take exactly 1 argument");
    if (!str_arg(in, args[0], "argument", &sub))
        return NULL;
    if (sub->length > self->u.s->length)
        return RValue_Bool(in, 0);
    if (from_end)
        found = memcmp(self->u.s->data + self->u.s->length - sub->length, sub->data,
                       sub->length) == 0;
    else
        found = memcmp(self->u.s->data, sub->data, sub->length) == 0;
    return RValue_Bool(in, found);
}

static RValue *str_m_startswith(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    return string_predicate(in, self, args, nargs, 0);
}

static RValue *str_m_endswith(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    return string_predicate(in, self, args, nargs, 1);
}

static RValue *str_m_find(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterString *sub;
    int found;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "find() takes exactly 1 argument");
    if (!str_arg(in, args[0], "find() argument", &sub))
        return NULL;
    found = find_sub(self->u.s->data, self->u.s->length, sub->data, sub->length, 0);
    if (found >= 0)
        found = (int)AsterString_CodePointIndex(self->u.s, (size_t)found);
    return RValue_Int(in, found);
}

static RValue *str_m_count(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterString *sub;
    size_t i = 0;
    int64_t count = 0;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "count() takes exactly 1 argument");
    if (!str_arg(in, args[0], "count() argument", &sub))
        return NULL;
    if (sub->length == 0)
        return RValue_Int(in, 0);
    for (;;) {
        int found = find_sub(self->u.s->data, self->u.s->length, sub->data, sub->length, i);
        if (found < 0)
            break;
        count++;
        i = (size_t)found + sub->length;
    }
    return RValue_Int(in, count);
}

static RValue *str_m_to_int(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t n;
    (void)args;
    (void)nargs;
    n = RValue_AsInt(in, self, &ok);
    return ok ? RValue_Int(in, n) : NULL;
}

static RValue *str_m_to_float(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    double f;
    (void)args;
    (void)nargs;
    f = RValue_AsDouble(in, self, &ok);
    return ok ? RValue_Float(in, f) : NULL;
}

static RValue *str_m_length(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    return RValue_Int(in, (int64_t)self->u.s->cp_length);
}

static RValue *str_m_charat(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t index;
    size_t off, adv = 1;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "charAt() takes exactly 1 argument");
    if (!method_index(in, args[0], &index))
        return NULL;
    if (index < 0)
        index += (int64_t)self->u.s->cp_length;
    if (index < 0 || (size_t)index >= self->u.s->cp_length)
        return RT_ThrowValue(in, "IndexError", "string index out of range");
    off = AsterString_ByteOffset(self->u.s, (size_t)index);
    Aster_UTF8_Decode(self->u.s->data + off, self->u.s->length - off, &adv);
    return RValue_Str(in, AsterString_FromUTF8(self->u.s->data + off, adv));
}

static RValue *str_m_substring(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t start = 0, end = (int64_t)self->u.s->cp_length;
    size_t boff, eoff;
    if (nargs < 1 || nargs > 2)
        return RT_ThrowValue(in, "TypeError", "substring() takes 1 or 2 arguments");
    if (!method_index(in, args[0], &start))
        return NULL;
    if (nargs == 2 && !method_index(in, args[1], &end))
        return NULL;
    if (start < 0)
        start = 0;
    if (end > (int64_t)self->u.s->cp_length)
        end = (int64_t)self->u.s->cp_length;
    if (start > end || (size_t)start > self->u.s->cp_length)
        return RT_ThrowValue(in, "IndexError", "invalid substring range");
    boff = AsterString_ByteOffset(self->u.s, (size_t)start);
    eoff = AsterString_ByteOffset(self->u.s, (size_t)end);
    return RValue_Str(in, AsterString_FromUTF8(self->u.s->data + boff, eoff - boff));
}

static RValue *str_m_isdigit(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    (void)args;
    (void)nargs;
    if (self->u.s->length == 0)
        return RValue_Bool(in, 0);
    for (i = 0; i < self->u.s->length; i++)
        if (!(self->u.s->data[i] >= '0' && self->u.s->data[i] <= '9'))
            return RValue_Bool(in, 0);
    return RValue_Bool(in, 1);
}

static RValue *str_m_isalpha(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;
    (void)args;
    (void)nargs;
    if (self->u.s->length == 0)
        return RValue_Bool(in, 0);
    for (i = 0; i < self->u.s->length; i++) {
        unsigned char c = (unsigned char)self->u.s->data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80))
            return RValue_Bool(in, 0);
    }
    return RValue_Bool(in, 1);
}

static RValue *str_m_isspace(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i = 0, n = self->u.s->length;
    (void)args;
    (void)nargs;
    if (n == 0)
        return RValue_Bool(in, 0);
    while (i < n) {
        size_t adv = 1;
        uint32_t cp = Aster_UTF8_Decode(self->u.s->data + i, n - i, &adv);
        if (!Aster_IsSpaceCP(cp))
            return RValue_Bool(in, 0);
        i += adv;
    }
    return RValue_Bool(in, 1);
}

static RValue *str_m_chars(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i = 0, n = self->u.s->length;
    (void)args;
    (void)nargs;
    while (i < n) {
        size_t adv = 1;
        Aster_UTF8_Decode(self->u.s->data + i, n - i, &adv);
        List_Append(in, list, RValue_Str(in, AsterString_FromUTF8(self->u.s->data + i, adv)));
        i += adv;
    }
    return RValue_List(in, list);
}

static RValue *str_m_tostring(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)in;
    (void)args;
    (void)nargs;
    return self;
}

typedef struct {
    const char *name;
    BuiltinFn fn;
} StrMethodEntry;

static const StrMethodEntry str_methods[] = {
    {"upper", str_m_upper},
    {"toUpperCase", str_m_upper},
    {"lower", str_m_lower},
    {"toLowerCase", str_m_lower},
    {"strip", str_m_strip},
    {"trim", str_m_strip},
    {"split", str_m_split},
    {"join", str_m_join},
    {"replace", str_m_replace},
    {"startswith", str_m_startswith},
    {"endswith", str_m_endswith},
    {"find", str_m_find},
    {"indexOf", str_m_find},
    {"count", str_m_count},
    {"to_int", str_m_to_int},
    {"to_float", str_m_to_float},
    {"length", str_m_length},
    {"charAt", str_m_charat},
    {"substring", str_m_substring},
    {"isdigit", str_m_isdigit},
    {"isalpha", str_m_isalpha},
    {"isspace", str_m_isspace},
    {"chars", str_m_chars},
    {"toString", str_m_tostring},
};

AstBuiltin *String_Method(Interp *in, RValue *self, const char *name)
{
    size_t i;
    for (i = 0; i < sizeof str_methods / sizeof str_methods[0]; i++) {
        if (!strcmp(str_methods[i].name, name)) {
            AstBuiltin *b = Builtin_New(in, str_methods[i].name, str_methods[i].fn);
            b->self = self;
            return b;
        }
    }
    return NULL;
}

/* ---- range ---- */

static RValue *bi_range(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t start = 0, stop, step = 1, i;
    AsterList *list;
    (void)self;

    if (nargs < 1 || nargs > 3)
        return RT_ThrowValue(in, "TypeError", "range() takes 1 to 3 arguments");
    if (nargs == 1) {
        stop = RValue_AsInt(in, args[0], &ok);
        if (!ok)
            return NULL;
    }
    else {
        start = RValue_AsInt(in, args[0], &ok);
        if (!ok)
            return NULL;
        stop = RValue_AsInt(in, args[1], &ok);
        if (!ok)
            return NULL;
        if (nargs == 3) {
            step = RValue_AsInt(in, args[2], &ok);
            if (!ok)
                return NULL;
        }
    }
    if (step == 0)
        return RT_ThrowValue(in, "ValueError", "range() step cannot be zero");
    if (step > 0 && stop < start)
        return RValue_List(in, List_New(in));
    if (step < 0 && stop > start)
        return RValue_List(in, List_New(in));
    {
        uint64_t count = step > 0 ? (uint64_t)((stop - start + step - 1) / step)
                                  : (uint64_t)((start - stop - step - 1) / (-step));
        if (count > 10000000ULL)
            return RT_ThrowValue(in, "OverflowError", "range() result is too large");
        list = List_New(in);
        for (i = start; step > 0 ? i < stop : i > stop; i += step)
            List_Append(in, list, RValue_Int(in, i));
    }
    return RValue_List(in, list);
}

/* ---- 시스템/입출력 (Java 스타일) ---- */

static char *read_stdin_line(int *eof)
{
    size_t cap = 128, len = 0;
    char *buf = xrealloc(NULL, cap);
    int c = EOF;

    *eof = 0;
    while ((c = fgetc(stdin)) != EOF && c != '\n') {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0) {
        free(buf);
        *eof = 1;
        return NULL;
    }
    if (len > 0 && buf[len - 1] == '\r')
        len--;
    buf[len] = 0;
    return buf;
}

static RValue *inst_get(Interp *in, RValue *self, const char *name)
{
    (void)in;
    InstField *f;
    if (!self || self->kind != RV_OBJECT)
        return NULL;
    f = Instance_FindField(self->u.obj, name);
    return f ? f->cell : NULL;
}

static void inst_set(Interp *in, RValue *self, const char *name, RValue *value)
{
    InstField *f = Instance_FindField(self->u.obj, name);
    (void)in;
    if (f)
        f->cell = value;
}

static char *path_bytes(Interp *in, RValue *v, int *ok)
{
    *ok = 0;
    if (!v || v->kind != RV_STR) {
        RT_Throw(in, "TypeError", "file path must be a string");
        return NULL;
    }
    *ok = 1;
    return v->u.s->data;
}

static RValue *scanner_ctor(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)args;
    (void)nargs;
    inst_set(in, self, "tokens", RValue_List(in, List_New(in)));
    inst_set(in, self, "pos", RValue_Int(in, 0));
    inst_set(in, self, "pending", RValue_Null(in));
    return RValue_Null(in);
}

static RValue *scanner_fill_tokens(Interp *in, RValue *self)
{
    RValue *pending = inst_get(in, self, "pending");
    int eof = 0;
    char *line;
    AsterList *tokens;
    size_t i = 0, n;
    if (pending && pending->kind == RV_STR) {
        int len = (int)pending->u.s->length;
        line = xrealloc(NULL, (size_t)len + 1);
        memcpy(line, pending->u.s->data, (size_t)len);
        line[len] = 0;
        inst_set(in, self, "pending", RValue_Null(in));
    }
    else {
        line = read_stdin_line(&eof);
        if (!line) {
            RT_Throw(in, "EOFError", "end of input");
            return NULL;
        }
    }
    n = strlen(line);
    tokens = List_New(in);
    while (i < n) {
        size_t start;
        while (i < n && is_space_byte((unsigned char)line[i]))
            i++;
        start = i;
        while (i < n && !is_space_byte((unsigned char)line[i]))
            i++;
        if (i > start)
            List_Append(in, tokens, RValue_Str(in, AsterString_FromUTF8(line + start,
                                                                        i - start)));
    }
    free(line);
    inst_set(in, self, "tokens", RValue_List(in, tokens));
    inst_set(in, self, "pos", RValue_Int(in, 0));
    return self;
}

static RValue *scanner_next(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *tokens = inst_get(in, self, "tokens");
    RValue *posv = inst_get(in, self, "pos");
    int64_t pos = posv && posv->kind == RV_INT ? posv->u.i : 0;
    (void)args;
    (void)nargs;

    for (;;) {
        if (tokens && tokens->kind == RV_LIST && (size_t)pos < tokens->u.list->len) {
            inst_set(in, self, "pos", RValue_Int(in, pos + 1));
            return tokens->u.list->items[pos];
        }
        if (!scanner_fill_tokens(in, self))
            return NULL;
        tokens = inst_get(in, self, "tokens");
        pos = 0;
    }
}

static RValue *scanner_nextline(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *pending = inst_get(in, self, "pending");
    int eof = 0;
    char *line;
    (void)args;
    (void)nargs;
    if (pending && pending->kind == RV_STR) {
        inst_set(in, self, "pending", RValue_Null(in));
        return pending;
    }
    line = read_stdin_line(&eof);
    if (!line)
        return RT_ThrowValue(in, "EOFError", "end of input");
    {
        RValue *v = RValue_Str(in, AsterString_FromUTF8(line, strlen(line)));
        free(line);
        return v;
    }
}

static RValue *scanner_hasnextline(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *pending = inst_get(in, self, "pending");
    int eof = 0;
    char *line;
    (void)args;
    (void)nargs;
    if (pending && pending->kind == RV_STR)
        return RValue_Bool(in, 1);
    line = read_stdin_line(&eof);
    if (!line)
        return RValue_Bool(in, 0);
    inst_set(in, self, "pending",
             RValue_Str(in, AsterString_FromUTF8(line, strlen(line))));
    free(line);
    return RValue_Bool(in, 1);
}

static RValue *scanner_nextint(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *tok = scanner_next(in, self, args, nargs);
    int ok;
    int64_t n;
    if (!tok)
        return NULL;
    if (tok->kind == RV_STR) {
        char *end;
        long long v;
        const char *s = tok->u.s->data;
        errno = 0;
        v = strtoll(s, &end, 10);
        if (errno || end == s || *end)
            return RT_ThrowValue(in, "NumberFormatError", "cannot read an integer: %s", s);
        return RValue_Int(in, (int64_t)v);
    }
    n = RValue_AsInt(in, tok, &ok);
    return ok ? RValue_Int(in, n) : NULL;
}

static RValue *scanner_nextfloat(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *tok = scanner_next(in, self, args, nargs);
    int ok;
    double f;
    if (!tok)
        return NULL;
    if (tok->kind == RV_STR) {
        char *end;
        double v;
        const char *s = tok->u.s->data;
        errno = 0;
        v = strtod(s, &end);
        if (errno || end == s || *end)
            return RT_ThrowValue(in, "NumberFormatError", "cannot read a number: %s", s);
        return RValue_Float(in, v);
    }
    f = RValue_AsDouble(in, tok, &ok);
    return ok ? RValue_Float(in, f) : NULL;
}

static RValue *scanner_close(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return RValue_Null(in);
}

static RValue *files_readstring(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    char *path;
    FILE *f;
    Buf b = {0};
    char chunk[4096];
    size_t n;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "Files.readString() takes exactly 1 argument");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
        return RT_ThrowValue(in, "OSError", "cannot open file: %s", path);
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf_write(&b, chunk, n);
    fclose(f);
    {
        AsterString *s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
        free(b.data);
        return RValue_Str(in, s);
    }
}

static RValue *files_writestring(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    char *path;
    AsterString *content;
    FILE *f;
    (void)self;
    if (nargs != 2)
        return RT_ThrowValue(in, "TypeError",
                             "Files.writeString() takes exactly 2 arguments");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (!str_arg(in, args[1], "file contents", &content))
        return NULL;
    f = fopen(path, "wb");
    if (!f)
        return RT_ThrowValue(in, "OSError", "cannot write file: %s", path);
    if (content->length)
        fwrite(content->data, 1, content->length, f);
    fclose(f);
    return RValue_Null(in);
}

static RValue *files_readlines(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue *all = files_readstring(in, self, args, nargs);
    AsterList *list;
    const char *d;
    size_t n, i = 0;
    if (!all)
        return NULL;
    list = List_New(in);
    d = all->u.s->data;
    n = all->u.s->length;
    while (i < n) {
        size_t start = i;
        while (i < n && d[i] != '\n')
            i++;
        {
            size_t end = i;
            if (end > start && d[end - 1] == '\r')
                end--;
            List_Append(in, list,
                        RValue_Str(in, AsterString_FromUTF8(d + start, end - start)));
        }
        if (i < n)
            i++;
    }
    return RValue_List(in, list);
}

static RValue *files_exists(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    char *path;
    FILE *f;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "Files.exists() takes exactly 1 argument");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return RValue_Bool(in, 1);
    }
    return RValue_Bool(in, 0);
}

static RValue *files_delete(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    char *path;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "Files.delete() takes exactly 1 argument");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (remove(path) != 0)
        return RT_ThrowValue(in, "OSError", "cannot delete file: %s", path);
    return RValue_Null(in);
}

static RValue *system_exit(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t code = 0;
    (void)self;
    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "System.exit() takes at most 1 argument");
    if (nargs == 1) {
        code = RValue_AsInt(in, args[0], &ok);
        if (!ok)
            return NULL;
    }
    fflush(stdout);
    exit((int)code);
    return RValue_Null(in);
}

static RValue *system_set_recursion_limit(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t limit;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError",
                             "System.setRecursionLimit() takes exactly 1 argument");
    limit = RValue_AsInt(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (limit < 1)
        return RT_ThrowValue(in, "ValueError",
                             "recursion limit must be at least 1");
    if (limit > 100000)
        return RT_ThrowValue(in, "ValueError",
                             "recursion limit must be at most 100000");
    in->recursion_limit = (int)limit;
    return RValue_Null(in);
}

static RValue *system_get_recursion_limit(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    (void)args;
    (void)nargs;
    return RValue_Int(in, in->recursion_limit);
}

typedef struct {
    const char *name;
    BuiltinFn fn;
    int mutating;
} MethodEntry;

static const MethodEntry list_methods[] = {
    {"append", list_m_append, 1},
    {"extend", list_m_extend, 1},
    {"insert", list_m_insert, 1},
    {"pop", list_m_pop, 1},
    {"remove", list_m_remove, 1},
    {"clear", list_m_clear, 1},
    {"index", list_m_index, 0},
    {"count", list_m_count, 0},
    {"copy", list_m_copy, 0},
    {"map", list_m_map, 0},
    {"filter", list_m_filter, 0},
    {"reverse", list_m_reverse, 1},
    {"sort", list_m_sort, 1},
};

static const MethodEntry tuple_methods[] = {
    {"index", tuple_m_index, 0},
    {"count", tuple_m_count, 0},
};

static const MethodEntry dict_methods[] = {
    {"get", dict_m_get, 0},
    {"keys", dict_m_keys, 0},
    {"values", dict_m_values, 0},
    {"items", dict_m_items, 0},
    {"pop", dict_m_pop, 1},
    {"setdefault", dict_m_setdefault, 1},
    {"update", dict_m_update, 1},
    {"clear", dict_m_clear, 1},
    {"copy", dict_m_copy, 0},
};

static const MethodEntry set_methods[] = {
    {"add", set_m_add, 1},
    {"remove", set_m_remove, 1},
    {"discard", set_m_discard, 1},
    {"pop", set_m_pop, 1},
    {"clear", set_m_clear, 1},
    {"copy", set_m_copy, 0},
    {"union", set_m_union, 0},
    {"intersection", set_m_intersection, 0},
    {"difference", set_m_difference, 0},
    {"issubset", set_m_issubset, 0},
    {"issuperset", set_m_issuperset, 0},
};

AstBuiltin *Collection_Method(Interp *in, RValue *self, const char *name)
{
    const MethodEntry *table = NULL;
    size_t count = 0;
    size_t i;

    switch (self->kind) {
    case RV_LIST:
        table = list_methods;
        count = sizeof list_methods / sizeof list_methods[0];
        break;
    case RV_TUPLE:
        table = tuple_methods;
        count = sizeof tuple_methods / sizeof tuple_methods[0];
        break;
    case RV_DICT:
        table = dict_methods;
        count = sizeof dict_methods / sizeof dict_methods[0];
        break;
    case RV_SET:
    case RV_FROZENSET:
        table = set_methods;
        count = sizeof set_methods / sizeof set_methods[0];
        break;
    default:
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (!strcmp(table[i].name, name)) {
            AstBuiltin *b;
            if (self->kind == RV_FROZENSET && table[i].mutating)
                return NULL;
            b = Builtin_New(in, table[i].name, table[i].fn);
            b->self = self;
            return b;
        }
    }
    return NULL;
}

static int try_module_file(const char *dir, const char *rel, char *out, size_t cap)
{
    FILE *f;
    if (dir && dir[0])
        snprintf(out, cap, "%s/%s", dir, rel);
    else
        snprintf(out, cap, "%s", rel);
    f = fopen(out, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int Aster_ResolveModuleFile(const char *base_dir, const char *stdlib_dir,
                            const char **module_dirs, size_t nmodule_dirs,
                            const char *name, char *out, size_t cap)
{
    char rel[512], with_ast[600], with_init[620];
    size_t i, o = 0;

    for (i = 0; name[i] && o + 1 < sizeof(rel); i++)
        rel[o++] = name[i] == '.' ? '/' : name[i];
    rel[o] = 0;
    snprintf(with_ast, sizeof with_ast, "%s.ast", rel);
    snprintf(with_init, sizeof with_init, "%s/__init__.ast", rel);

    if (try_module_file(base_dir, with_ast, out, cap))
        return 1;
    if (try_module_file(base_dir, with_init, out, cap))
        return 1;
    if (try_module_file(stdlib_dir, with_ast, out, cap))
        return 1;
    if (try_module_file(stdlib_dir, with_init, out, cap))
        return 1;
    for (i = 0; i < nmodule_dirs; i++) {
        if (try_module_file(module_dirs[i], with_ast, out, cap))
            return 1;
        if (try_module_file(module_dirs[i], with_init, out, cap))
            return 1;
    }
    return 0;
}

AstModule *Module_New(Interp *in, const char *name, const char *path)
{
    AstModule *m = Arena_Alloc(in->arena, sizeof(AstModule));
    m->name = name;
    m->path = path;
    m->env = Env_New(in, in->globals);
    return m;
}

AstModule *Module_Builtin(Interp *in, const char *name)
{
    size_t i;
    for (i = 0; i < in->nbuiltin_modules; i++) {
        if (!strcmp(in->builtin_modules[i]->name, name))
            return in->builtin_modules[i];
    }
    return NULL;
}

static AstModule *register_builtin_module(Interp *in, const char *name)
{
    AstModule *m;
    if (in->nbuiltin_modules >= sizeof in->builtin_modules / sizeof in->builtin_modules[0])
        return NULL;
    m = Module_New(in, name, "<builtin>");
    in->builtin_modules[in->nbuiltin_modules++] = m;
    return m;
}

void Module_AddExport(AstModule *module, Interp *in, const char *name)
{
    size_t i;
    (void)in;
    for (i = 0; i < module->nexports; i++) {
        if (!strcmp(module->exports[i], name))
            return;
    }
    module->exports = xrealloc(module->exports,
                               (module->nexports + 1) * sizeof(const char *));
    module->exports[module->nexports++] = name;
}

RValue *Module_GetExported(Interp *in, AstModule *module, const char *name)
{
    RValue **cell;
    (void)in;
    cell = Env_LookupCell(module->env, name);
    size_t i;
    int explicitly_exported = module->nexports > 0;
    int public_name = name[0] != '_';

    if (!cell || !*cell)
        return NULL;
    if (explicitly_exported) {
        for (i = 0; i < module->nexports; i++)
            if (!strcmp(module->exports[i], name))
                return *cell;
        return NULL;
    }
    return public_name ? *cell : NULL;
}

void Interp_AddModuleDir(Interp *in, const char *dir)
{
    if (!dir || !dir[0])
        return;
    if (in->nmodule_dirs == in->cap_module_dirs) {
        size_t cap = in->cap_module_dirs ? in->cap_module_dirs * 2 : 4;
        const char **grown = realloc(in->module_dirs, cap * sizeof(const char *));
        if (!grown)
            return;
        in->module_dirs = grown;
        in->cap_module_dirs = cap;
    }
    in->module_dirs[in->nmodule_dirs++] = dir;
}

void Interp_Init(Interp *in)
{
    static const char *exceptions[] = {
        "Exception", "ArithmeticError", "ZeroDivisionError", "OverflowError",
        "ValueError", "TypeError", "NameError", "AttributeError", "KeyError",
        "IndexError", "RuntimeError", "NullPointerError", "LookupError",
        "AssertionError", "OSError", "EOFError", "IOException",
        "ImportError", "ModuleNotFoundError", "RecursionError",
        "NumberFormatError", "NotImplementedError", "StopIteration"
    };
    AstClass *stdout_cls, *stderr_cls, *stdin_cls, *scanner_cls, *files_cls;
    AstInstance *stdout_obj, *stderr_obj, *stdin_obj, *scanner_obj;
    AstInstance *files_obj;
    AsterList *arglist;
    size_t i;

    in->globals = Env_New(in, NULL);
    in->sig = SIG_OK;
    in->recursion_limit = ASTER_DEFAULT_RECURSION_LIMIT;

    Env_Define(in, in->globals, "memset", RValue_Builtin(in, Builtin_New(in, "memset", bi_memset)));
    Env_Define(in, in->globals, "len", RValue_Builtin(in, Builtin_New(in, "len", bi_len)));
    Env_Define(in, in->globals, "list", RValue_Builtin(in, Builtin_New(in, "list", bi_list)));
    Env_Define(in, in->globals, "tuple", RValue_Builtin(in, Builtin_New(in, "tuple", bi_tuple)));
    Env_Define(in, in->globals, "dict", RValue_Builtin(in, Builtin_New(in, "dict", bi_dict)));
    Env_Define(in, in->globals, "set", RValue_Builtin(in, Builtin_New(in, "set", bi_set)));
    Env_Define(in, in->globals, "frozenset",
               RValue_Builtin(in, Builtin_New(in, "frozenset", bi_frozenset)));
    Env_Define(in, in->globals, "range", RValue_Builtin(in, Builtin_New(in, "range", bi_range)));
    Env_Define(in, in->globals, "next", RValue_Builtin(in, Builtin_New(in, "next", bi_next)));
    Env_Define(in, in->globals, "typeName",
               RValue_Builtin(in, Builtin_New(in, "typeName", bi_typename)));
    Env_Define(in, in->globals, "repr", RValue_Builtin(in, Builtin_New(in, "repr", bi_repr)));
    Env_Define(in, in->globals, "chr", RValue_Builtin(in, Builtin_New(in, "chr", bi_chr)));
    Env_Define(in, in->globals, "ord", RValue_Builtin(in, Builtin_New(in, "ord", bi_ord)));
    Env_Define(in, in->globals, "_aster_now_ms",
               RValue_Builtin(in, Builtin_New(in, "_aster_now_ms", bi_now_ms)));
    Env_Define(in, in->globals, "_aster_sleep_ms",
               RValue_Builtin(in, Builtin_New(in, "_aster_sleep_ms", bi_sleep_ms)));
    Env_Define(in, in->globals, "_aster_cwd",
               RValue_Builtin(in, Builtin_New(in, "_aster_cwd", bi_cwd)));
    Env_Define(in, in->globals, "_aster_pid",
               RValue_Builtin(in, Builtin_New(in, "_aster_pid", bi_pid)));
    Env_Define(in, in->globals, "_aster_platform",
               RValue_Builtin(in, Builtin_New(in, "_aster_platform", bi_platform)));
    Env_Define(in, in->globals, "_aster_setenv",
               RValue_Builtin(in, Builtin_New(in, "_aster_setenv", bi_setenv)));

    for (i = 0; i < sizeof exceptions / sizeof exceptions[0]; i++)
        Env_Define(in, in->globals, exceptions[i], RValue_ExcClass(in, exceptions[i]));

    /* 표준 출력/에러 */
    stdout_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    stdout_cls->name = "Stdout";
    stdout_cls->nmethods = 2;
    stdout_cls->methods = Arena_Alloc(in->arena, 2 * sizeof(MethodDesc));
    stdout_cls->methods[0].name = "print";
    stdout_cls->methods[0].fn = make_builtin_method(in, "print", bi_print);
    stdout_cls->methods[1].name = "println";
    stdout_cls->methods[1].fn = make_builtin_method(in, "println", bi_println);
    stdout_cls->env = in->globals;
    stdout_obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    stdout_obj->cls = stdout_cls;

    stderr_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    stderr_cls->name = "Stderr";
    stderr_cls->nmethods = 2;
    stderr_cls->methods = Arena_Alloc(in->arena, 2 * sizeof(MethodDesc));
    stderr_cls->methods[0].name = "print";
    stderr_cls->methods[0].fn = make_builtin_method(in, "print", bi_eprint);
    stderr_cls->methods[1].name = "println";
    stderr_cls->methods[1].fn = make_builtin_method(in, "println", bi_eprintln);
    stderr_cls->env = in->globals;
    stderr_obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    stderr_obj->cls = stderr_cls;

    stdin_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    stdin_cls->name = "Stdin";
    stdin_cls->env = in->globals;
    stdin_obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    stdin_obj->cls = stdin_cls;

    /* Scanner: Java 스타일 입력 */
    scanner_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    scanner_cls->name = "Scanner";
    scanner_cls->env = in->globals;
    scanner_cls->nfields = 3;
    scanner_cls->fields = Arena_Alloc(in->arena, 3 * sizeof(FieldDesc));
    scanner_cls->fields[0].name = "tokens";
    scanner_cls->fields[1].name = "pos";
    scanner_cls->fields[2].name = "pending";
    scanner_cls->nmethods = 7;
    scanner_cls->methods = Arena_Alloc(in->arena, 7 * sizeof(MethodDesc));
    scanner_cls->methods[0].name = "nextLine";
    scanner_cls->methods[0].fn = make_builtin_method(in, "nextLine", scanner_nextline);
    scanner_cls->methods[1].name = "next";
    scanner_cls->methods[1].fn = make_builtin_method(in, "next", scanner_next);
    scanner_cls->methods[2].name = "nextInt";
    scanner_cls->methods[2].fn = make_builtin_method(in, "nextInt", scanner_nextint);
    scanner_cls->methods[3].name = "nextFloat";
    scanner_cls->methods[3].fn = make_builtin_method(in, "nextFloat", scanner_nextfloat);
    scanner_cls->methods[4].name = "hasNextLine";
    scanner_cls->methods[4].fn = make_builtin_method(in, "hasNextLine", scanner_hasnextline);
    scanner_cls->methods[5].name = "hasNext";
    scanner_cls->methods[5].fn = make_builtin_method(in, "hasNext", scanner_hasnextline);
    scanner_cls->methods[6].name = "close";
    scanner_cls->methods[6].fn = make_builtin_method(in, "close", scanner_close);
    scanner_cls->ctor = make_builtin_method(in, "Scanner", scanner_ctor);
    {
        AstClass *cls = Arena_Alloc(in->arena, sizeof(AstClass));
        *cls = *scanner_cls;
        Env_Define(in, in->globals, "Scanner", RValue_Class(in, cls));
    }
    scanner_obj = Arena_Alloc(in->arena, sizeof(AstInstance));   /* 미사용, 경고 방지 */
    (void)scanner_obj;

    /* Files: Java 스타일 파일 입출력 */
    files_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    files_cls->name = "Files";
    files_cls->env = in->globals;
    files_cls->nmethods = 5;
    files_cls->methods = Arena_Alloc(in->arena, 5 * sizeof(MethodDesc));
    files_cls->methods[0].name = "readString";
    files_cls->methods[0].fn = make_builtin_method(in, "readString", files_readstring);
    files_cls->methods[1].name = "writeString";
    files_cls->methods[1].fn = make_builtin_method(in, "writeString", files_writestring);
    files_cls->methods[2].name = "readLines";
    files_cls->methods[2].fn = make_builtin_method(in, "readLines", files_readlines);
    files_cls->methods[3].name = "exists";
    files_cls->methods[3].fn = make_builtin_method(in, "exists", files_exists);
    files_cls->methods[4].name = "delete";
    files_cls->methods[4].fn = make_builtin_method(in, "delete", files_delete);
    files_obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    files_obj->cls = files_cls;
    Env_Define(in, in->globals, "Files", RValue_Object(in, files_obj));

    arglist = List_New(in);
    for (i = 0; in->argv && (int)i < in->argc; i++)
        List_Append(in, arglist, RValue_Str(in, AsterString_FromCStr(in->argv[i])));

    /* System은 import해야만 쓸 수 있는 내장 모듈이다. */
    {
        AstModule *sys_mod = register_builtin_module(in, "System");
        AstModule *out_mod = register_builtin_module(in, "System.stdout");
        AstModule *err_mod = register_builtin_module(in, "System.stderr");
        AstModule *in_mod = register_builtin_module(in, "System.stdin");

        Env_Define(in, out_mod->env, "print",
                   RValue_Builtin(in, Builtin_New(in, "print", bi_print)));
        Env_Define(in, out_mod->env, "println",
                   RValue_Builtin(in, Builtin_New(in, "println", bi_println)));
        Env_Define(in, err_mod->env, "print",
                   RValue_Builtin(in, Builtin_New(in, "print", bi_eprint)));
        Env_Define(in, err_mod->env, "println",
                   RValue_Builtin(in, Builtin_New(in, "println", bi_eprintln)));

        Env_Define(in, sys_mod->env, "stdout", RValue_Object(in, stdout_obj));
        Env_Define(in, sys_mod->env, "out", RValue_Object(in, stdout_obj));
        Env_Define(in, sys_mod->env, "stderr", RValue_Object(in, stderr_obj));
        Env_Define(in, sys_mod->env, "err", RValue_Object(in, stderr_obj));
        Env_Define(in, sys_mod->env, "stdin", RValue_Object(in, stdin_obj));
        Env_Define(in, sys_mod->env, "in", RValue_Object(in, stdin_obj));
        Env_Define(in, sys_mod->env, "args", RValue_List(in, arglist));
        Env_Define(in, sys_mod->env, "exit",
                   RValue_Builtin(in, Builtin_New(in, "exit", system_exit)));
        Env_Define(in, sys_mod->env, "setRecursionLimit",
                   RValue_Builtin(in, Builtin_New(in, "setRecursionLimit",
                                                  system_set_recursion_limit)));
        Env_Define(in, sys_mod->env, "getRecursionLimit",
                   RValue_Builtin(in, Builtin_New(in, "getRecursionLimit",
                                                  system_get_recursion_limit)));
        (void)in_mod;
    }
}
