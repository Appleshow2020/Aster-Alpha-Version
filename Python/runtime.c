#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/runtime.h"

#define MAX_DEPTH 64

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fputs("메모리가 부족합니다\n", stderr);
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

RValue *RValue_Generator(Interp *in, AsterList *items)
{
    AstGenerator *gen = Arena_Alloc(in->arena, sizeof(AstGenerator));
    RValue *v = RValue_New(in, RV_GENERATOR);
    gen->items = items;
    gen->index = 0;
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

RValue *Dict_Lookup(const AsterDict *dict, const RValue *key)
{
    size_t i;
    for (i = 0; i < dict->len; i++) {
        if (RValue_Equal(dict->entries[i].key, key))
            return dict->entries[i].value;
    }
    return NULL;
}

int Dict_Set(Interp *in, AsterDict *dict, RValue *key, RValue *value, int *ok)
{
    size_t i;

    *ok = 1;
    if (!RValue_Hashable(key)) {
        *ok = 0;
        RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(key));
        return 0;
    }
    for (i = 0; i < dict->len; i++) {
        if (RValue_Equal(dict->entries[i].key, key)) {
            dict->entries[i].value = value;
            return 1;
        }
    }
    if (dict->len == dict->cap) {
        size_t ncap = dict->cap ? dict->cap * 2 : 8;
        dict->entries = xrealloc(dict->entries, ncap * sizeof(AsterDictEntry));
        dict->cap = ncap;
    }
    dict->entries[dict->len].key = key;
    dict->entries[dict->len].value = value;
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
}

AsterSet *Set_New(Interp *in)
{
    return Arena_Alloc(in->arena, sizeof(AsterSet));
}

int Set_Has(const AsterSet *set, const RValue *value)
{
    size_t i;
    for (i = 0; i < set->len; i++) {
        if (RValue_Equal(set->items[i], value))
            return 1;
    }
    return 0;
}

int Set_Add(Interp *in, AsterSet *set, RValue *value, int *ok)
{
    if (ok)
        *ok = 1;
    if (!RValue_Hashable(value)) {
        if (ok)
            *ok = 0;
        RT_Throw(in, "TypeError", "unhashable type: '%s'", RValue_TypeName(value));
        return 0;
    }
    if (Set_Has(set, value))
        return 1;
    if (set->len == set->cap)
        grow_items(in, &set->items, &set->cap, set->len + 1);
    set->items[set->len++] = value;
    return 1;
}

void Set_RemoveAt(AsterSet *set, size_t index)
{
    if (index >= set->len)
        return;
    memmove(set->items + index, set->items + index + 1,
            (set->len - index - 1) * sizeof(RValue *));
    set->len--;
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
        RT_Throw(in, "TypeError", "null 값에는 len()을 쓸 수 없습니다");
        return 0;
    }
    switch (v->kind) {
    case RV_STR:
        return (int64_t)v->u.s->length;
    case RV_LIST:
        return (int64_t)v->u.list->len;
    case RV_TUPLE:
        return (int64_t)v->u.tuple->len;
    case RV_DICT:
        return (int64_t)v->u.dict->len;
    case RV_SET:
    case RV_FROZENSET:
        return (int64_t)v->u.set->len;
    case RV_GENERATOR:
        return (int64_t)(v->u.gen->items->len - v->u.gen->index);
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s'에는 len()을 쓸 수 없습니다", RValue_TypeName(v));
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
        return v->u.s->length;
    case RV_LIST:
        return v->u.list->len;
    case RV_TUPLE:
        return v->u.tuple->len;
    case RV_DICT:
        return v->u.dict->len;
    case RV_SET:
    case RV_FROZENSET:
        return v->u.set->len;
    case RV_GENERATOR:
        return v->u.gen->items->len - v->u.gen->index;
    default:
        break;
    }
bad:
    *ok = 0;
    RT_Throw(in, "TypeError", "'%s'은(는) 순회할 수 없습니다", RValue_TypeName(v));
    return 0;
}

RValue *RValue_IterAt(Interp *in, const RValue *v, size_t index, int *ok)
{
    *ok = 1;
    switch (v->kind) {
    case RV_STR:
        return RValue_Str(in, AsterString_FromUTF8(v->u.s->data + index, 1));
    case RV_LIST:
        return v->u.list->items[index];
    case RV_TUPLE:
        return v->u.tuple->items[index];
    case RV_DICT:
        return v->u.dict->entries[index].key;
    case RV_SET:
    case RV_FROZENSET:
        return v->u.set->items[index];
    case RV_GENERATOR:
        return v->u.gen->items->items[v->u.gen->index + index];
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s'은(는) 순회할 수 없습니다", RValue_TypeName(v));
        return NULL;
    }
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
        RT_Throw(in, "RuntimeError", "구조체 기본값이 너무 깊습니다");
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
        RT_Throw(in, "RuntimeError", "타입 기본값이 너무 깊습니다");
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
        return RValue_Null(in);
    case StructRef_kind: {
        AstStructType *type = Struct_FindType(env, ty->v.StructRef.tag);
        if (!type) {
            RT_Throw(in, "TypeError", "구조체 타입 '%s'을(를) 찾을 수 없습니다", ty->v.StructRef.tag);
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
        RT_Throw(in, "RuntimeError", "구조체 크기 계산이 너무 깊습니다");
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
        RT_Throw(in, "RuntimeError", "타입 크기 계산이 너무 깊습니다");
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
    RT_Throw(in, "TypeError", "크기를 비교할 수 없는 값입니다");
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
    RT_Throw(in, "TypeError", "인덱스는 정수여야 합니다 (받은 값: '%s')",
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
        return RT_ThrowValue(in, "TypeError", "null 값은 인덱싱할 수 없습니다");
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
    case RV_STR:
        if (!seq_index(in, index, &i, ok))
            return NULL;
        if (i < 0)
            i += (int64_t)container->u.s->length;
        if (i < 0 || (size_t)i >= container->u.s->length) {
            *ok = 0;
            return RT_ThrowValue(in, "IndexError", "string index out of range");
        }
        return RValue_Str(in, AsterString_FromUTF8(container->u.s->data + i, 1));
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
    default:
        *ok = 0;
        return RT_ThrowValue(in, "TypeError", "'%s' 객체는 인덱싱할 수 없습니다",
                             RValue_TypeName(container));
    }
}

int RValue_SetItem(Interp *in, RValue *container, RValue *index, RValue *value, int *ok)
{
    int64_t i;

    *ok = 1;
    if (!container) {
        *ok = 0;
        RT_Throw(in, "TypeError", "null 값은 인덱싱할 수 없습니다");
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
        RT_Throw(in, "TypeError", "'%s' 객체는 값을 바꿀 수 없습니다",
                 container->kind == RV_TUPLE ? "tuple" : "frozenset");
        return 0;
    case RV_SET:
        *ok = 0;
        RT_Throw(in, "TypeError", "'set' 객체는 인덱스 대입을 지원하지 않습니다");
        return 0;
    case RV_STR:
        *ok = 0;
        RT_Throw(in, "TypeError", "'str' 객체는 값을 바꿀 수 없습니다");
        return 0;
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' 객체는 인덱스 대입을 지원하지 않습니다",
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
        RT_Throw(in, "TypeError", "null 값에는 'in'을 쓸 수 없습니다");
        return 0;
    }
    switch (container->kind) {
    case RV_STR:
        if (!item || item->kind != RV_STR) {
            *ok = 0;
            RT_Throw(in, "TypeError", "'in <string>'의 왼쪽은 문자열이어야 합니다");
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
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "'%s' 객체는 iterable이 아닙니다",
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
        RT_Throw(in, "OverflowError", "문자열이 너무 커집니다");
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
            RT_Throw(in, "ValueError", "문자열을 float로 바꿀 수 없습니다: '%s'", v->u.s->data);
            return 0;
        }
        return d;
    }
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "숫자가 아닌 값을 float로 바꿀 수 없습니다");
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
            RT_Throw(in, "OverflowError", "float 값을 int 범위로 바꿀 수 없습니다");
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
            RT_Throw(in, "ValueError", "문자열을 int로 바꿀 수 없습니다: '%s'", v->u.s->data);
            return 0;
        }
        if (errno == ERANGE) {
            *ok = 0;
            RT_Throw(in, "OverflowError", "정수 범위를 넘었습니다");
            return 0;
        }
        return n;
    }
    default:
        *ok = 0;
        RT_Throw(in, "TypeError", "숫자가 아닌 값을 int로 바꿀 수 없습니다");
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
    render_value(&b, v, 0, 0);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    (void)in;
    return s;
}

AsterString *RValue_Repr(Interp *in, const RValue *v)
{
    Buf b = {0};
    AsterString *s;

    render_value(&b, v, 1, 0);
    s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
    free(b.data);
    (void)in;
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
        RValue_Print(stdout, args[i], 0);
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
        return RT_ThrowValue(in, "ValueError", "이 값은 memset으로 초기화할 수 없습니다");
    }
}

static RValue *bi_memset(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t value;

    (void)self;
    if (nargs != 3)
        return RT_ThrowValue(in, "TypeError", "memset은 인자 3개가 필요합니다");
    if (args[0]->kind != RV_PTR || !args[0]->u.ptr.slot)
        return RT_ThrowValue(in, "TypeError", "memset의 첫 인자는 포인터여야 합니다");
    value = RValue_AsInt(in, args[1], &ok);
    if (!ok)
        return NULL;
    if (value != 0)
        return RT_ThrowValue(in, "ValueError", "memset은 지금 0으로만 초기화할 수 있습니다");
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
        RT_Throw(in, "TypeError", "null 값은 iterable이 아닙니다");
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
    case RV_STR:
        n = src->u.s->length;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = RValue_Str(in, AsterString_FromUTF8(src->u.s->data + i, 1));
        break;
    case RV_GENERATOR:
        n = src->u.gen->items->len - src->u.gen->index;
        items = xrealloc(NULL, (n ? n : 1) * sizeof(RValue *));
        for (i = 0; i < n; i++)
            items[i] = src->u.gen->items->items[src->u.gen->index + i];
        break;
    default:
        RT_Throw(in, "TypeError", "'%s' 객체는 iterable이 아닙니다", RValue_TypeName(src));
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
        return RT_ThrowValue(in, "TypeError", "인자가 너무 많습니다");
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
        return RT_ThrowValue(in, "TypeError", "len은 인자 1개가 필요합니다");
    n = RValue_Length(in, args[0], &ok);
    if (!ok)
        return NULL;
    return RValue_Int(in, n);
}

static RValue *bi_next(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    (void)self;
    if (nargs != 1 || !args[0] || args[0]->kind != RV_GENERATOR)
        return RT_ThrowValue(in, "TypeError", "next는 제너레이터 인자 1개가 필요합니다");
    if (args[0]->u.gen->index >= args[0]->u.gen->items->len) {
        RT_Throw(in, "StopIteration", "제너레이터가 끝났습니다");
        return NULL;
    }
    return args[0]->u.gen->items->items[args[0]->u.gen->index++];
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
        return RT_ThrowValue(in, "TypeError", "인자가 너무 많습니다");
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
        return RT_ThrowValue(in, "TypeError", "인자가 너무 많습니다");
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
                                         "dict는 (키, 값) 쌍의 iterable이 필요합니다");
                }
                if ((pair->kind == RV_LIST ? pair->u.list->len : pair->u.tuple->len) != 2) {
                    free(items);
                    return RT_ThrowValue(in, "ValueError",
                                         "dict 원소의 길이가 2가 아닙니다");
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
    RT_Throw(in, "TypeError", "인덱스는 정수여야 합니다");
    return 0;
}

static size_t dict_index(const AsterDict *dict, const RValue *key, int *found)
{
    size_t i;
    for (i = 0; i < dict->len; i++) {
        if (RValue_Equal(dict->entries[i].key, key)) {
            *found = 1;
            return i;
        }
    }
    *found = 0;
    return 0;
}

/* ---- list 메서드 ---- */

static RValue *list_m_append(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "append()는 인자 1개가 필요합니다");
    List_Append(in, self->u.list, RValue_Copy(in, args[0]));
    return RValue_Null(in);
}

static RValue *list_m_extend(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    RValue **items;
    size_t n;
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "extend()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "insert()는 인자 2개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "pop()은 인자를 최대 1개 받습니다");
    if (nargs == 1 && !method_index(in, args[0], &index))
        return NULL;
    if (self->u.list->len == 0)
        return RT_ThrowValue(in, "IndexError", "빈 리스트에서 pop할 수 없습니다");
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
        return RT_ThrowValue(in, "TypeError", "remove()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "index()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "count()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "index()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "count()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "get()은 인자를 1개 또는 2개 받습니다");
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
        return RT_ThrowValue(in, "TypeError", "pop()은 인자를 1개 또는 2개 받습니다");
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
        return RT_ThrowValue(in, "TypeError", "setdefault()는 인자를 1개 또는 2개 받습니다");
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
        return RT_ThrowValue(in, "TypeError", "update()는 인자 1개가 필요합니다");
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
                                     "update()에는 (키, 값) 쌍이 필요합니다");
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
    self->u.dict->len = 0;
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
        return RT_ThrowValue(in, "TypeError", "add()는 인자 1개가 필요합니다");
    if (!Set_Add(in, self->u.set, RValue_Copy(in, args[0]), &ok))
        return NULL;
    return RValue_Null(in);
}

static RValue *set_m_remove(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    size_t i;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "remove()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "discard()는 인자 1개가 필요합니다");
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
    self->u.set->len = 0;
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
        return RT_ThrowValue(in, "TypeError", "union()은 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "intersection()은 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "difference()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "issubset()은 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "issuperset()은 인자 1개가 필요합니다");
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
        RT_Throw(in, "TypeError", "%s은(는) 문자열이어야 합니다", what);
        return 0;
    }
    *out = v->u.s;
    return 1;
}

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int ascii_upper(int c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
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

static RValue *str_m_upper(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    Buf b = {0};
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.s->length; i++)
        buf_putc(&b, (char)ascii_upper((unsigned char)self->u.s->data[i]));
    {
        AsterString *s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
        free(b.data);
        return RValue_Str(in, s);
    }
}

static RValue *str_m_lower(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    Buf b = {0};
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.s->length; i++)
        buf_putc(&b, (char)ascii_lower((unsigned char)self->u.s->data[i]));
    {
        AsterString *s = AsterString_FromUTF8(b.data ? b.data : "", b.len);
        free(b.data);
        return RValue_Str(in, s);
    }
}

static RValue *str_m_strip(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    const char *d = self->u.s->data;
    size_t n = self->u.s->length, start = 0, end = n;
    (void)args;
    (void)nargs;
    while (start < end && is_space_byte((unsigned char)d[start]))
        start++;
    while (end > start && is_space_byte((unsigned char)d[end - 1]))
        end--;
    return RValue_Str(in, AsterString_FromUTF8(d + start, end - start));
}

static RValue *str_m_split(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    const char *d = self->u.s->data;
    size_t n = self->u.s->length;

    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "split()은 인자를 최대 1개 받습니다");
    if (nargs == 1 && args[0]->kind != RV_NULL) {
        AsterString *sep;
        size_t i = 0;
        if (!str_arg(in, args[0], "split 구분자", &sep))
            return NULL;
        if (sep->length == 0)
            return RT_ThrowValue(in, "ValueError", "빈 구분자로는 나눌 수 없습니다");
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
            size_t start;
            while (i < n && is_space_byte((unsigned char)d[i]))
                i++;
            start = i;
            while (i < n && !is_space_byte((unsigned char)d[i]))
                i++;
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
        return RT_ThrowValue(in, "TypeError", "join()은 인자 1개가 필요합니다");
    if (!iter_values(in, args[0], &items, &n))
        return NULL;
    for (i = 0; i < n; i++) {
        if (!items[i] || items[i]->kind != RV_STR) {
            free(items);
            free(b.data);
            return RT_ThrowValue(in, "TypeError", "join()의 원소는 문자열이어야 합니다");
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
        return RT_ThrowValue(in, "TypeError", "replace()는 인자 2개 또는 3개가 필요합니다");
    if (!str_arg(in, args[0], "replace의 old", &old) ||
        !str_arg(in, args[1], "replace의 new", &new_s))
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
        return RT_ThrowValue(in, "TypeError", "startswith/endswith는 인자 1개가 필요합니다");
    if (!str_arg(in, args[0], "인자", &sub))
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
        return RT_ThrowValue(in, "TypeError", "find()는 인자 1개가 필요합니다");
    if (!str_arg(in, args[0], "find의 인자", &sub))
        return NULL;
    found = find_sub(self->u.s->data, self->u.s->length, sub->data, sub->length, 0);
    return RValue_Int(in, found);
}

static RValue *str_m_count(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterString *sub;
    size_t i = 0;
    int64_t count = 0;

    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "count()는 인자 1개가 필요합니다");
    if (!str_arg(in, args[0], "count의 인자", &sub))
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
    return RValue_Int(in, (int64_t)self->u.s->length);
}

static RValue *str_m_charat(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t index;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "charAt()는 인자 1개가 필요합니다");
    if (!method_index(in, args[0], &index))
        return NULL;
    if (index < 0)
        index += (int64_t)self->u.s->length;
    if (index < 0 || (size_t)index >= self->u.s->length)
        return RT_ThrowValue(in, "IndexError", "문자열 인덱스가 범위를 벗어났습니다");
    return RValue_Str(in, AsterString_FromUTF8(self->u.s->data + index, 1));
}

static RValue *str_m_substring(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int64_t start = 0, end = (int64_t)self->u.s->length;
    if (nargs < 1 || nargs > 2)
        return RT_ThrowValue(in, "TypeError", "substring()은 인자 1개 또는 2개가 필요합니다");
    if (!method_index(in, args[0], &start))
        return NULL;
    if (nargs == 2 && !method_index(in, args[1], &end))
        return NULL;
    if (start < 0)
        start = 0;
    if (end > (int64_t)self->u.s->length)
        end = (int64_t)self->u.s->length;
    if (start > end || (size_t)start > self->u.s->length)
        return RT_ThrowValue(in, "IndexError", "substring 범위가 잘못되었습니다");
    return RValue_Str(in, AsterString_FromUTF8(self->u.s->data + start, (size_t)(end - start)));
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
    size_t i;
    (void)args;
    (void)nargs;
    if (self->u.s->length == 0)
        return RValue_Bool(in, 0);
    for (i = 0; i < self->u.s->length; i++)
        if (!is_space_byte((unsigned char)self->u.s->data[i]))
            return RValue_Bool(in, 0);
    return RValue_Bool(in, 1);
}

static RValue *str_m_chars(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    AsterList *list = List_New(in);
    size_t i;
    (void)args;
    (void)nargs;
    for (i = 0; i < self->u.s->length; i++)
        List_Append(in, list, RValue_Str(in, AsterString_FromUTF8(self->u.s->data + i, 1)));
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
        return RT_ThrowValue(in, "TypeError", "range()는 인자 1개에서 3개가 필요합니다");
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
        return RT_ThrowValue(in, "ValueError", "range()의 간격은 0일 수 없습니다");
    if (step > 0 && stop < start)
        return RValue_List(in, List_New(in));
    if (step < 0 && stop > start)
        return RValue_List(in, List_New(in));
    {
        uint64_t count = step > 0 ? (uint64_t)((stop - start + step - 1) / step)
                                  : (uint64_t)((start - stop - step - 1) / (-step));
        if (count > 10000000ULL)
            return RT_ThrowValue(in, "OverflowError", "range() 결과가 너무 큽니다");
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
        RT_Throw(in, "TypeError", "파일 경로는 문자열이어야 합니다");
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
            RT_Throw(in, "EOFError", "입력이 끝났습니다");
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
        return RT_ThrowValue(in, "EOFError", "입력이 끝났습니다");
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
            return RT_ThrowValue(in, "NumberFormatError", "정수로 읽을 수 없습니다: %s", s);
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
            return RT_ThrowValue(in, "NumberFormatError", "수로 읽을 수 없습니다: %s", s);
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
        return RT_ThrowValue(in, "TypeError", "Files.readString()은 인자 1개가 필요합니다");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
        return RT_ThrowValue(in, "OSError", "파일을 열 수 없습니다: %s", path);
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
                             "Files.writeString()은 인자 2개가 필요합니다");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (!str_arg(in, args[1], "파일 내용", &content))
        return NULL;
    f = fopen(path, "wb");
    if (!f)
        return RT_ThrowValue(in, "OSError", "파일을 쓸 수 없습니다: %s", path);
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
        return RT_ThrowValue(in, "TypeError", "Files.exists()는 인자 1개가 필요합니다");
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
        return RT_ThrowValue(in, "TypeError", "Files.delete()는 인자 1개가 필요합니다");
    path = path_bytes(in, args[0], &ok);
    if (!ok)
        return NULL;
    if (remove(path) != 0)
        return RT_ThrowValue(in, "OSError", "파일을 지울 수 없습니다: %s", path);
    return RValue_Null(in);
}

static RValue *system_exit(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    int ok;
    int64_t code = 0;
    (void)self;
    if (nargs > 1)
        return RT_ThrowValue(in, "TypeError", "System.exit()는 인자를 최대 1개 받습니다");
    if (nargs == 1) {
        code = RValue_AsInt(in, args[0], &ok);
        if (!ok)
            return NULL;
    }
    fflush(stdout);
    exit((int)code);
    return RValue_Null(in);
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

AstModule *Module_New(Interp *in, const char *name, const char *path)
{
    AstModule *m = Arena_Alloc(in->arena, sizeof(AstModule));
    m->name = name;
    m->path = path;
    m->env = Env_New(in, in->globals);
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
    RValue **cell = Env_LookupCell(module->env, name);
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
    AstClass *sys_cls, *stdout_cls, *stderr_cls, *stdin_cls, *scanner_cls, *files_cls;
    AstInstance *sys_obj, *stdout_obj, *stderr_obj, *stdin_obj, *scanner_obj;
    AstInstance *files_obj;
    AsterList *arglist;
    size_t i;

    in->globals = Env_New(in, NULL);
    in->sig = SIG_OK;

    Env_Define(in, in->globals, "print", RValue_Builtin(in, Builtin_New(in, "print", bi_print)));
    Env_Define(in, in->globals, "println", RValue_Builtin(in, Builtin_New(in, "println", bi_println)));
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

    /* System */
    sys_cls = Arena_Alloc(in->arena, sizeof(AstClass));
    sys_cls->name = "System";
    sys_cls->env = in->globals;
    sys_cls->nfields = 5;
    sys_cls->fields = Arena_Alloc(in->arena, 5 * sizeof(FieldDesc));
    sys_cls->fields[0].name = "stdout";
    sys_cls->fields[1].name = "out";
    sys_cls->fields[2].name = "err";
    sys_cls->fields[3].name = "in";
    sys_cls->fields[4].name = "args";
    sys_cls->nmethods = 1;
    sys_cls->methods = Arena_Alloc(in->arena, sizeof(MethodDesc));
    sys_cls->methods[0].name = "exit";
    sys_cls->methods[0].fn = make_builtin_method(in, "exit", system_exit);

    arglist = List_New(in);
    for (i = 0; in->argv && (int)i < in->argc; i++)
        List_Append(in, arglist, RValue_Str(in, AsterString_FromCStr(in->argv[i])));

    sys_obj = Arena_Alloc(in->arena, sizeof(AstInstance));
    sys_obj->cls = sys_cls;
    sys_obj->nfields = 5;
    sys_obj->fields = Arena_Alloc(in->arena, 5 * sizeof(InstField));
    sys_obj->fields[0].name = "stdout";
    sys_obj->fields[0].cell = RValue_Object(in, stdout_obj);
    sys_obj->fields[1].name = "out";
    sys_obj->fields[1].cell = RValue_Object(in, stdout_obj);
    sys_obj->fields[2].name = "err";
    sys_obj->fields[2].cell = RValue_Object(in, stderr_obj);
    sys_obj->fields[3].name = "in";
    sys_obj->fields[3].cell = RValue_Object(in, stdin_obj);
    sys_obj->fields[4].name = "args";
    sys_obj->fields[4].cell = RValue_List(in, arglist);

    Env_Define(in, in->globals, "System", RValue_Object(in, sys_obj));
}
