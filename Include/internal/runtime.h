#ifndef ASTER_RUNTIME_H
#define ASTER_RUNTIME_H

/*
 * 실행 시 값과 환경. AST가 Arena에 살아 있는 동안 함께 쓰는 Arena 할당기를 쓴다.
 * (자동 해제는 프로세스가 끝날 때 Arena_Free로 한 번에 한다.)
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "internal/arena.h"
#include "internal/ast.h"
#include "internal/object.h"

typedef struct RValue RValue;
typedef struct Env Env;
typedef struct Binding Binding;
typedef struct Interp Interp;
typedef struct AstFn AstFn;
typedef struct AstBuiltin AstBuiltin;
typedef struct AstClass AstClass;
typedef struct AstInstance AstInstance;
typedef struct AstStructType AstStructType;
typedef struct AstStructValue AstStructValue;
typedef struct AstException AstException;
typedef struct AstModule AstModule;
typedef struct AstOverload AstOverload;
typedef struct AstEnumType AstEnumType;
typedef struct AsterSuper AsterSuper;

typedef enum {
    RV_NULL = 0,
    RV_INT,
    RV_FLOAT,
    RV_BOOL,
    RV_STR,
    RV_OBJECT,
    RV_STRUCT,
    RV_FUNC,
    RV_BUILTIN,
    RV_PTR,
    RV_EXC,
    RV_EXC_CLASS,
    RV_CLASS,
    RV_STRUCTTYPE,
    RV_LIST,
    RV_TUPLE,
    RV_DICT,
    RV_SET,
    RV_FROZENSET,
    RV_MODULE,
    RV_SUPER,
    RV_ENUM,
    RV_ENUMTYPE,
    RV_OVERLOAD,
    RV_GENERATOR
} RKind;

typedef struct AsterList {
    RValue **items;
    size_t len;
    size_t cap;
} AsterList;

typedef struct AsterTuple {
    RValue **items;
    size_t len;
} AsterTuple;

typedef struct AsterDictEntry {
    RValue *key;
    RValue *value;
} AsterDictEntry;

typedef struct AsterDict {
    AsterDictEntry *entries;
    size_t len;
    size_t cap;
} AsterDict;

typedef struct AsterSet {
    RValue **items;
    size_t len;
    size_t cap;
} AsterSet;

/* yield로 모은 값들과 다음 위치. (열성 평가 제너레이터) */
typedef struct AstGenerator {
    AsterList *items;
    size_t index;
} AstGenerator;

struct RValue {
    RKind kind;
    union {
        int64_t i;
        double f;
        int b;
        AsterString *s;
        AstInstance *obj;
        AstStructValue *stv;
        AstFn *fn;
        AstBuiltin *builtin;
        struct { RValue **slot; typeref_ty ty; } ptr;
        AstException *exc;
        const char *exc_name;
        AstClass *cls;
        AstStructType *stype;
        AsterList *list;
        AsterTuple *tuple;
        AsterDict *dict;
        AsterSet *set;
        AstModule *module;
        AsterSuper *super;
        AstEnumType *etype;
        struct { AstEnumType *type; const char *name; size_t index; } en;
        AstOverload *overload;
        AstGenerator *gen;
    } u;
};

struct Env {
    Env *parent;
    Binding *vars;
};

struct Binding {
    const char *name;
    RValue *cell;
    int is_const;
    Binding *next;
};

typedef struct {
    const char *name;
    typeref_ty ty;
    expr_ty init;
    int is_static;
} FieldDesc;

typedef struct {
    const char *name;
    RValue *cell;
} InstField;

typedef enum { FN_USER, FN_LAMBDA, FN_BUILTIN } FnKind;

typedef RValue *(*BuiltinFn)(Interp *in, RValue *self, RValue **args, size_t nargs);

struct AstBuiltin {
    const char *name;
    BuiltinFn fn;
    RValue *self;            /* 컬렉션 메서드처럼 인스턴스에 묶인 경우 */
};

struct AstFn {
    FnKind kind;
    const char *name;
    asdl_seq *params;        /* param_ty (함수) 또는 lparam_ty (람다) */
    int params_are_lambda;
    ret_ty ret;
    asdl_seq *body;          /* FN_USER */
    expr_ty body_expr;       /* FN_LAMBDA */
    Env *env;                /* 정의 시점의 환경 (클로저) */
    RValue *self;            /* 바인딩된 self 또는 NULL */
    AstClass *owner;         /* 메서드가 속한 클래스 (super 해석용) */
    int is_static;
    int is_generator;        /* 본문에 yield가 있다 */
    AstBuiltin builtin;      /* FN_BUILTIN */
};

struct AstModule {
    const char *name;
    const char *path;
    Env *env;
    const char **exports;
    size_t nexports;
    int loading;
};

struct AstOverload {
    AstFn **fns;
    size_t count, cap;
};

typedef struct {
    const char *name;
    RValue *value;
} AstEnumMember;

struct AstEnumType {
    const char *name;
    AstEnumMember *members;
    size_t nmembers;
    Env *env;
};

struct AsterSuper {
    AstClass *owner;
    RValue *self;
};

typedef struct {
    const char *name;
    AstFn *fn;
    AstOverload *overload;   /* 같은 이름의 메서드가 여럿이면 NULL이 아니다 */
} MethodDesc;

typedef struct {
    const char *name;
    AstFn *getter;
    AstFn *setter;
} PropDesc;

struct AstClass {
    const char *name;
    const char *base_name;
    AstClass *base;
    FieldDesc *fields;
    size_t nfields;
    RValue **static_values;   /* fields와 같은 인덱스. 정적 필드만 값이 있다. */
    MethodDesc *methods;
    size_t nmethods;
    PropDesc *props;
    size_t nprops;
    AstFn *ctor;
    Env *env;
};

struct AstInstance {
    AstClass *cls;
    InstField *fields;
    size_t nfields;
};

typedef struct {
    const char *name;
    typeref_ty ty;
} StructFieldDesc;

typedef struct {
    asdl_seq *params;
    asdl_seq *inits;
    asdl_seq *body;
} StructCtorDesc;

struct AstStructType {
    const char *tag;
    const char *alias;
    StructFieldDesc *fields;
    size_t nfields;
    StructCtorDesc *ctors;
    size_t nctors;
    Env *env;
};

struct AstStructValue {
    AstStructType *type;
    RValue **cells;          /* 필드 수만큼의 값 슬롯 */
};

struct AstException {
    const char *name;
    RValue *message;
};

typedef enum {
    SIG_OK = 0,
    SIG_BREAK,
    SIG_CONTINUE,
    SIG_RETURN,
    SIG_THROW
} Signal;

struct Interp {
    Arena *arena;
    Env *globals;
    Signal sig;
    RValue *ret;
    RValue *exc;
    int line;                /* 오류 위치 추적용 현재 문장 줄 */
    const char *base_dir;    /* 주 프로그램이 있는 디렉터리 (모듈 찾기) */
    const char *stdlib_dir;  /* 표준 라이브러리 루트 (Lib) */
    const char **module_dirs;/* 추가 모듈 검색 루트 (패키지 의존성) */
    size_t nmodule_dirs, cap_module_dirs;
    char **argv;
    int argc;
    AstModule **modules;
    size_t nmodules, cap_modules;
    struct {
        const char *name;
        const char *base;
    } *exc_bases;
    size_t nexc_bases, cap_exc_bases;
    AsterList *yield_buf;    /* yield를 모으는 중이면 NULL이 아니다 */
    int predeclaring;        /* 선언 수집 단계 (오버로드 병합 허용) */
    int depth;               /* 호출 깊이 (재귀 제한) */
};

/* ---- 값 ---- */

RValue *RValue_New(Interp *in, RKind kind);
RValue *RValue_Null(Interp *in);
RValue *RValue_Int(Interp *in, int64_t i);
RValue *RValue_Float(Interp *in, double f);
RValue *RValue_Bool(Interp *in, int b);
RValue *RValue_Str(Interp *in, AsterString *s);
RValue *RValue_Object(Interp *in, AstInstance *obj);
RValue *RValue_Struct(Interp *in, AstStructValue *stv);
RValue *RValue_Func(Interp *in, AstFn *fn);
RValue *RValue_Builtin(Interp *in, AstBuiltin *builtin);
RValue *RValue_Ptr(Interp *in, RValue **slot, typeref_ty ty);
RValue *RValue_Exception(Interp *in, const char *name, RValue *message);
RValue *RValue_ExcClass(Interp *in, const char *name);
RValue *RValue_Class(Interp *in, AstClass *cls);
RValue *RValue_Generator(Interp *in, AsterList *items);
RValue *RValue_StructType(Interp *in, AstStructType *type);
RValue *RValue_List(Interp *in, AsterList *list);
RValue *RValue_Tuple(Interp *in, AsterTuple *tuple);
RValue *RValue_Dict(Interp *in, AsterDict *dict);
RValue *RValue_Set(Interp *in, AsterSet *set);
RValue *RValue_FrozenSet(Interp *in, AsterSet *set);
RValue *RValue_Module(Interp *in, AstModule *module);
RValue *RValue_Super(Interp *in, AstClass *owner, RValue *self);
RValue *RValue_Enum(Interp *in, AstEnumType *type, const char *name, size_t index);
RValue *RValue_EnumType(Interp *in, AstEnumType *type);
RValue *RValue_Overload(Interp *in, AstOverload *overload);

/* ---- 컬렉션 ---- */

AsterList *List_New(Interp *in);
void List_Append(Interp *in, AsterList *list, RValue *value);
void List_Insert(Interp *in, AsterList *list, size_t index, RValue *value);
void List_RemoveAt(AsterList *list, size_t index);
AsterTuple *Tuple_New(Interp *in, size_t len);
AsterDict *Dict_New(Interp *in);
RValue *Dict_Lookup(const AsterDict *dict, const RValue *key);
int Dict_Set(Interp *in, AsterDict *dict, RValue *key, RValue *value, int *ok);
void Dict_RemoveAt(AsterDict *dict, size_t index);
AsterSet *Set_New(Interp *in);
int Set_Has(const AsterSet *set, const RValue *value);
int Set_Add(Interp *in, AsterSet *set, RValue *value, int *ok);
void Set_RemoveAt(AsterSet *set, size_t index);
int RValue_Hashable(const RValue *v);
const char *RValue_TypeName(const RValue *v);
int64_t RValue_Length(Interp *in, const RValue *v, int *ok);
int RValue_Contains(Interp *in, const RValue *container, const RValue *item, int *ok);
RValue *RValue_GetItem(Interp *in, RValue *container, RValue *index, int *ok);
int RValue_SetItem(Interp *in, RValue *container, RValue *index, RValue *value, int *ok);
AsterString *RString_Repeat(Interp *in, AsterString *s, int64_t times);

/* 순회: list/tuple/set/frozenset/str/dict(키). 순회할 수 없으면 0. */
int RValue_Iterable(const RValue *v);
size_t RValue_IterLen(Interp *in, const RValue *v, int *ok);
RValue *RValue_IterAt(Interp *in, const RValue *v, size_t index, int *ok);

/* 구조체는 값 의미이므로 복사하고, 나머지는 같은 객체를 공유한다. */
RValue *RValue_Copy(Interp *in, RValue *v);
int RValue_Truthy(const RValue *v);
int RValue_Equal(const RValue *a, const RValue *b);
int RValue_Compare(Interp *in, const RValue *a, const RValue *b, int *out);
double RValue_AsDouble(Interp *in, const RValue *v, int *ok);
int64_t RValue_AsInt(Interp *in, const RValue *v, int *ok);
int RValue_IsNumber(const RValue *v);

/* repr이 0이면 print용(문자열은 따옴표 없이), 1이면 repr용. */
void RValue_Print(FILE *out, const RValue *v, int repr);
AsterString *RValue_ToString(Interp *in, const RValue *v);
AsterString *RValue_Repr(Interp *in, const RValue *v);
AsterString *RString_Concat(Interp *in, AsterString *a, AsterString *b);

/* ---- 환경 ---- */

Env *Env_New(Interp *in, Env *parent);
RValue **Env_LookupCell(Env *env, const char *name);
void Env_Define(Interp *in, Env *env, const char *name, RValue *value);
void Env_DefineConst(Interp *in, Env *env, const char *name, RValue *value);
int Env_IsConst(Env *env, const char *name);
int Env_Assign(Env *env, const char *name, RValue *value);
int Env_HasLocal(Env *env, const char *name);

/* ---- 타입/객체 ---- */

AstInstance *Instance_New(Interp *in, AstClass *cls);
InstField *Instance_FindField(AstInstance *obj, const char *name);
AstClass *Class_FindBase(AstClass *cls, const char *name);
MethodDesc *Class_FindMethod(AstClass *cls, const char *name);
PropDesc *Class_FindProperty(AstClass *cls, const char *name);
size_t Class_FieldCount(AstClass *cls);

AstStructValue *StructValue_New(Interp *in, AstStructType *type);
int StructValue_FieldIndex(AstStructType *type, const char *name);
AstStructType *Struct_FindType(Env *env, const char *name);

/* 기본값: int 0, float 0.0, bool false, str "", 포인터/nullable/클래스 null, 구조체는 재귀. */
RValue *DefaultForType(Interp *in, Env *env, typeref_ty ty);
size_t Type_SizeOf(Interp *in, Env *env, typeref_ty ty);
int Type_IsNullable(typeref_ty ty);

/* ---- 예외 ---- */

void RT_Throw(Interp *in, const char *name, const char *fmt, ...);
RValue *RT_ThrowValue(Interp *in, const char *name, const char *fmt, ...);
int Exception_Matches(Interp *in, const char *thrown, const char *caught);
RValue *Exception_NameValue(Interp *in, const char *name);
const char *Exception_Parent(Interp *in, const char *name);
void Exception_Register(Interp *in, const char *name, const char *base);

/* ---- 내장 ---- */

void Interp_Init(Interp *in);
void Interp_AddModuleDir(Interp *in, const char *dir);
AstBuiltin *Builtin_New(Interp *in, const char *name, BuiltinFn fn);
AstBuiltin *Collection_Method(Interp *in, RValue *self, const char *name);
AstBuiltin *String_Method(Interp *in, RValue *self, const char *name);
AstModule *Module_New(Interp *in, const char *name, const char *path);
void Module_AddExport(AstModule *module, Interp *in, const char *name);
RValue *Module_GetExported(Interp *in, AstModule *module, const char *name);

#endif
