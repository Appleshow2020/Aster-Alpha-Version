#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/interp.h"
#include "internal/parser.h"

#define MAX_CALL_DEPTH 512

static void *xmalloc_fallback(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("메모리가 부족합니다\n", stderr);
        abort();
    }
    return p;
}

static RValue *eval(Interp *in, expr_ty e, Env *env);
static Signal exec_block(Interp *in, asdl_seq *body, Env *env);
static Signal exec_stmt(Interp *in, stmt_ty s, Env *env);
static Signal exec_decl(Interp *in, stmt_ty s, Env *env);
static void predeclare(Interp *in, Env *env, asdl_seq *body);
static RValue *call_fn(Interp *in, AstFn *fn, RValue **args, size_t nargs);
static RValue *apply_binop(Interp *in, operator_ty op, RValue *left, RValue *right);
static RValue *get_attr(Interp *in, RValue *base, const char *name);
static RValue *do_cast(Interp *in, typeref_ty ty, RValue *value);
static int value_is_type(Interp *in, RValue *value, typeref_ty ty);
static RValue *make_exception(Interp *in, RValue *value, const char *exc_type);
static AstStructType *struct_type_from_typeref(Interp *in, Env *env, typeref_ty ty);
static RValue *property_read(Interp *in, PropDesc *prop, RValue *self);
static AstFn *find_ctor(AstClass *cls);

/* ---- 공통 ---- */

static RValue *null_value(Interp *in)
{
    return RValue_Null(in);
}

static const char *typeref_name(typeref_ty ty)
{
    if (!ty)
        return "Exception";
    switch (ty->kind) {
    case Named_kind:
        return ty->v.Named.name;
    case StructRef_kind:
        return ty->v.StructRef.tag;
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT:
            return "int";
        case FloatT:
            return "float";
        case BoolT:
            return "bool";
        case StrT:
            return "str";
        case NullT:
            return "null";
        }
        return "int";
    default:
        return "Exception";
    }
}

/* 본문에 yield가 있는지 훑는다. 중첩 함수/클래스의 yield는 세지 않는다. */
static int body_has_yield(asdl_seq *body);

static int stmt_has_yield(stmt_ty s)
{
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
        return stmt_has_yield(s->v.For.init) || stmt_has_yield(s->v.For.step) ||
               body_has_yield(s->v.For.body);
    case ForRange_kind:
        return body_has_yield(s->v.ForRange.body);
    case ForIn_kind:
        return body_has_yield(s->v.ForIn.body);
    case Try_kind:
        if (body_has_yield(s->v.Try.body) || body_has_yield(s->v.Try.finalbody))
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

static int body_has_yield(asdl_seq *body)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (stmt_has_yield(asdl_seq_GET(body, i)))
            return 1;
    }
    return 0;
}

static AstFn *make_fn(Interp *in, Env *env, const char *name, asdl_seq *params,
                      int lambda_params, ret_ty ret, asdl_seq *body, expr_ty body_expr)
{
    AstFn *fn = Arena_Alloc(in->arena, sizeof(AstFn));
    fn->kind = lambda_params ? FN_LAMBDA : FN_USER;
    fn->name = name;
    fn->params = params;
    fn->params_are_lambda = lambda_params;
    fn->ret = ret;
    fn->body = body;
    fn->body_expr = body_expr;
    fn->env = env;
    fn->is_generator = body ? body_has_yield(body) : 0;
    return fn;
}

static AstFn *bind_method(Interp *in, AstFn *fn, RValue *self)
{
    AstFn *bound = Arena_Alloc(in->arena, sizeof(AstFn));
    *bound = *fn;
    bound->self = self;
    return bound;
}

/* 오버로드 묶음의 각 함수를 self에 묶는다. self가 NULL이면 정적 호출이다. */
static RValue *bind_overload(Interp *in, AstOverload *overload, RValue *self,
                             AstClass *owner)
{
    AstOverload *bound = Arena_Alloc(in->arena, sizeof(AstOverload));
    size_t i;
    bound->count = bound->cap = overload->count;
    bound->fns = Arena_Alloc(in->arena, overload->count * sizeof(AstFn *));
    for (i = 0; i < overload->count; i++) {
        AstFn *fn = self ? bind_method(in, overload->fns[i], self) : overload->fns[i];
        if (owner)
            fn->owner = owner;
        bound->fns[i] = fn;
    }
    return RValue_Overload(in, bound);
}

static void overload_append(Interp *in, AstOverload *overload, AstFn *fn)
{
    if (overload->count == overload->cap) {
        size_t cap = overload->cap ? overload->cap * 2 : 2;
        AstFn **grown = Arena_Alloc(in->arena, cap * sizeof(AstFn *));
        if (overload->fns)
            memcpy(grown, overload->fns, overload->count * sizeof(AstFn *));
        overload->fns = grown;
        overload->cap = cap;
    }
    overload->fns[overload->count++] = fn;
}

/* ---- 함수 호출 ---- */

static int bind_params(Interp *in, AstFn *fn, Env *env, RValue **args, size_t nargs)
{
    size_t nparams = asdl_seq_LEN(fn->params);
    size_t i;

    if (nparams != nargs) {
        RT_Throw(in, "TypeError", "'%s'에는 인자가 %zu개 필요하지만 %zu개가 넘어왔습니다",
                 fn->name ? fn->name : "lambda", nparams, nargs);
        return 0;
    }
    for (i = 0; i < nparams; i++) {
        const char *name;
        if (fn->params_are_lambda) {
            lparam_ty p = asdl_seq_GET(fn->params, i);
            name = p->name;
        }
        else {
            param_ty p = asdl_seq_GET(fn->params, i);
            name = p->name;
        }
        Env_Define(in, env, name, RValue_Copy(in, args ? args[i] : NULL));
    }
    return 1;
}

static RValue *call_fn(Interp *in, AstFn *fn, RValue **args, size_t nargs)
{
    Env *env;
    RValue *result;

    if (fn->kind == FN_BUILTIN)
        return fn->builtin.fn(in, fn->self ? fn->self : fn->builtin.self, args, nargs);
    if (in->sig != SIG_OK)
        return NULL;

    if (in->depth >= MAX_CALL_DEPTH) {
        RT_Throw(in, "RecursionError", "호출 깊이가 너무 깊습니다 (무한 재귀일 수 있습니다)");
        return NULL;
    }
    in->depth++;

    env = Env_New(in, fn->env);
    if (fn->self) {
        Env_Define(in, env, "self", fn->self);
        Env_Define(in, env, "super",
                   RValue_Super(in, fn->owner, fn->self));
    }
    if (!bind_params(in, fn, env, args, nargs)) {
        in->depth--;
        return NULL;
    }

    if (fn->kind == FN_LAMBDA) {
        result = eval(in, fn->body_expr, env);
        in->depth--;
        return result;
    }

    if (fn->is_generator) {
        AsterList *saved = in->yield_buf;
        AsterList *items = List_New(in);
        in->yield_buf = items;
        exec_block(in, fn->body, env);
        in->yield_buf = saved;
        if (in->sig == SIG_RETURN) {
            in->sig = SIG_OK;
            in->ret = NULL;
        }
        else if (in->sig == SIG_BREAK || in->sig == SIG_CONTINUE) {
            in->sig = SIG_OK;
            in->depth--;
            RT_Throw(in, "RuntimeError", "'break'와 'continue'는 반복문 안에서만 쓸 수 있습니다");
            return NULL;
        }
        else if (in->sig == SIG_THROW) {
            in->depth--;
            return NULL;
        }
        in->depth--;
        return RValue_Generator(in, items);
    }

    exec_block(in, fn->body, env);
    if (in->sig == SIG_RETURN) {
        RValue *ret = in->ret ? in->ret : null_value(in);
        in->sig = SIG_OK;
        in->ret = NULL;
        in->depth--;
        return ret;
    }
    if (in->sig == SIG_BREAK || in->sig == SIG_CONTINUE) {
        in->sig = SIG_OK;
        in->depth--;
        RT_Throw(in, "RuntimeError", "'break'와 'continue'는 반복문 안에서만 쓸 수 있습니다");
        return NULL;
    }
    if (in->sig == SIG_THROW) {
        in->depth--;
        return NULL;
    }
    in->ret = NULL;
    in->depth--;
    return null_value(in);
}

static int fn_accepts(Interp *in, AstFn *fn, RValue **args, size_t nargs)
{
    size_t i, nparams = asdl_seq_LEN(fn->params);
    if (nparams != nargs)
        return 0;
    if (fn->params_are_lambda)
        return 1;
    for (i = 0; i < nparams; i++) {
        param_ty p = asdl_seq_GET(fn->params, i);
        if (!p->ty)
            continue;
        if (value_is_type(in, args[i], p->ty))
            continue;
        /* int 인자는 float 매개변수에 맞는다. */
        if (p->ty->kind == Primitive_kind && p->ty->v.Primitive.kind == FloatT &&
            args[i] && args[i]->kind == RV_INT)
            continue;
        return 0;
    }
    return 1;
}

static RValue *call_value(Interp *in, RValue *callee, RValue **args, size_t nargs)
{
    if (!callee) {
        return RT_ThrowValue(in, "TypeError", "호출할 수 없는 값입니다");
    }
    switch (callee->kind) {
    case RV_FUNC:
        return call_fn(in, callee->u.fn, args, nargs);
    case RV_BUILTIN:
        return callee->u.builtin->fn(in, callee->u.builtin->self, args, nargs);
    case RV_OVERLOAD: {
        AstOverload *ov = callee->u.overload;
        AstFn *fallback = NULL;
        size_t i;
        for (i = 0; i < ov->count; i++) {
            AstFn *fn = ov->fns[i];
            if (asdl_seq_LEN(fn->params) == nargs) {
                if (!fallback)
                    fallback = fn;
                if (fn_accepts(in, fn, args, nargs))
                    return call_fn(in, fn, args, nargs);
            }
        }
        if (fallback)
            return call_fn(in, fallback, args, nargs);
        return RT_ThrowValue(in, "TypeError", "'%s'에 인자 %zu개에 맞는 정의가 없습니다",
                             ov->count ? ov->fns[0]->name : "overload", nargs);
    }
    case RV_SUPER: {
        AstClass *base = callee->u.super->owner ? callee->u.super->owner->base : NULL;
        AstFn *ctor = find_ctor(base);
        if (!ctor)
            return RT_ThrowValue(in, "TypeError", "상위 클래스에 생성자가 없습니다");
        call_fn(in, bind_method(in, ctor, callee->u.super->self), args, nargs);
        return in->sig == SIG_OK ? null_value(in) : NULL;
    }
    case RV_CLASS:
        return RT_ThrowValue(in, "TypeError", "'%s'을(를) 만들 때는 new를 사용하세요",
                             callee->u.cls->name);
    case RV_EXC_CLASS: {
        RValue *message = nargs > 0
            ? RValue_Str(in, RValue_ToString(in, args[0]))
            : RValue_Str(in, AsterString_FromCStr(""));
        return RValue_Exception(in, callee->u.exc_name, message);
    }
    default:
        return RT_ThrowValue(in, "TypeError", "값을 함수처럼 호출할 수 없습니다");
    }
}

/* ---- 연산 ---- */

static int to_int_pair(Interp *in, RValue *l, RValue *r, int64_t *a, int64_t *b)
{
    int ok;
    *a = RValue_AsInt(in, l, &ok);
    if (!ok)
        return 0;
    *b = RValue_AsInt(in, r, &ok);
    return ok;
}

static int64_t ipow(Interp *in, int64_t base, int64_t exp)
{
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        if (exp)
            base *= base;
    }
    (void)in;
    return result;
}

static int repeat_count(Interp *in, RValue *v, int64_t *out)
{
    if (v && v->kind == RV_INT) {
        *out = v->u.i;
        return 1;
    }
    if (v && v->kind == RV_BOOL) {
        *out = v->u.b;
        return 1;
    }
    RT_Throw(in, "TypeError", "시퀀스 반복 횟수는 정수여야 합니다");
    return 0;
}

static RValue *seq_repeat(Interp *in, RValue *seq, int64_t times)
{
    size_t n, total, i, k;
    size_t unit;
    RValue **src;

    if (seq->kind == RV_LIST) {
        src = seq->u.list->items;
        n = seq->u.list->len;
    }
    else {
        src = seq->u.tuple->items;
        n = seq->u.tuple->len;
    }
    if (times < 0)
        times = 0;
    unit = n;
    if (times > 0 && unit && (uint64_t)unit > (uint64_t)SIZE_MAX / (uint64_t)times)
        return RT_ThrowValue(in, "OverflowError", "시퀀스가 너무 커집니다");
    total = unit * (size_t)times;

    if (seq->kind == RV_LIST) {
        AsterList *list = List_New(in);
        for (i = 0; i < total; i++)
            List_Append(in, list, RValue_Copy(in, src[i % n]));
        return RValue_List(in, list);
    }
    else {
        AsterTuple *tuple = Tuple_New(in, total);
        k = 0;
        for (i = 0; i < total; i++)
            tuple->items[k++] = RValue_Copy(in, src[i % n]);
        return RValue_Tuple(in, tuple);
    }
}

static RValue *apply_binop(Interp *in, operator_ty op, RValue *left, RValue *right)
{
    int use_float = 0;
    int ok;

    if (op == Add && (left->kind == RV_STR || right->kind == RV_STR))
        return RValue_Str(in, RString_Concat(in, RValue_ToString(in, left),
                                            RValue_ToString(in, right)));

    if (op == Add && left->kind == RV_LIST && right->kind == RV_LIST) {
        AsterList *list = List_New(in);
        size_t i;
        for (i = 0; i < left->u.list->len; i++)
            List_Append(in, list, RValue_Copy(in, left->u.list->items[i]));
        for (i = 0; i < right->u.list->len; i++)
            List_Append(in, list, RValue_Copy(in, right->u.list->items[i]));
        return RValue_List(in, list);
    }

    if (op == Add && left->kind == RV_TUPLE && right->kind == RV_TUPLE) {
        AsterTuple *tuple = Tuple_New(in, left->u.tuple->len + right->u.tuple->len);
        size_t i;
        size_t k = 0;
        for (i = 0; i < left->u.tuple->len; i++)
            tuple->items[k++] = RValue_Copy(in, left->u.tuple->items[i]);
        for (i = 0; i < right->u.tuple->len; i++)
            tuple->items[k++] = RValue_Copy(in, right->u.tuple->items[i]);
        return RValue_Tuple(in, tuple);
    }

    if (op == Mult) {
        int64_t times;
        if (left->kind == RV_STR) {
            if (!repeat_count(in, right, &times))
                return NULL;
            return RValue_Str(in, RString_Repeat(in, left->u.s, times));
        }
        if (right->kind == RV_STR) {
            if (!repeat_count(in, left, &times))
                return NULL;
            return RValue_Str(in, RString_Repeat(in, right->u.s, times));
        }
        if (left->kind == RV_LIST || left->kind == RV_TUPLE) {
            if (!repeat_count(in, right, &times))
                return NULL;
            return seq_repeat(in, left, times);
        }
        if (right->kind == RV_LIST || right->kind == RV_TUPLE) {
            if (!repeat_count(in, left, &times))
                return NULL;
            return seq_repeat(in, right, times);
        }
    }

    if (!RValue_IsNumber(left) || !RValue_IsNumber(right)) {
        return RT_ThrowValue(in, "TypeError", "산술 연산에는 숫자가 필요합니다");
    }
    use_float = left->kind == RV_FLOAT || right->kind == RV_FLOAT;

    if (use_float) {
        double a = RValue_AsDouble(in, left, &ok);
        double b;
        if (!ok)
            return NULL;
        b = RValue_AsDouble(in, right, &ok);
        if (!ok)
            return NULL;
        switch (op) {
        case Add:
            return RValue_Float(in, a + b);
        case Sub:
            return RValue_Float(in, a - b);
        case Mult:
            return RValue_Float(in, a * b);
        case Div:
            if (b == 0.0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            return RValue_Float(in, a / b);
        case FloorDiv:
            if (b == 0.0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            return RValue_Float(in, __builtin_floor(a / b));
        case Mod: {
            double r;
            if (b == 0.0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            r = a - __builtin_floor(a / b) * b;
            return RValue_Float(in, r);
        }
        case Pow:
            return RValue_Float(in, __builtin_pow(a, b));
        }
        return null_value(in);
    }
    else {
        int64_t a;
        int64_t b;
        if (!to_int_pair(in, left, right, &a, &b))
            return NULL;
        switch (op) {
        case Add:
            return RValue_Int(in, a + b);
        case Sub:
            return RValue_Int(in, a - b);
        case Mult:
            return RValue_Int(in, a * b);
        case Div:
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            return RValue_Int(in, a / b);
        case FloorDiv: {
            int64_t q;
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                q -= 1;
            return RValue_Int(in, q);
        }
        case Mod: {
            int64_t r;
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "0으로 나눌 수 없습니다");
            r = a % b;
            if (r != 0 && ((r < 0) != (b < 0)))
                r += b;
            return RValue_Int(in, r);
        }
        case Pow:
            if (b < 0)
                return RValue_Float(in, __builtin_pow((double)a, (double)b));
            return RValue_Int(in, ipow(in, a, b));
        }
        return null_value(in);
    }
}

static operator_ty assign_op_to_operator(assign_op_ty op)
{
    switch (op) {
    case AssignAdd:
        return Add;
    case AssignSub:
        return Sub;
    case AssignMult:
        return Mult;
    case AssignDiv:
        return Div;
    case AssignFloorDiv:
        return FloorDiv;
    case AssignMod:
        return Mod;
    case AssignPow:
        return Pow;
    default:
        return Add;
    }
}

/* ---- 어트리뷰트 ---- */

static RValue *get_attr(Interp *in, RValue *base, const char *name)
{
    if (!base || base->kind == RV_NULL)
        return RT_ThrowValue(in, "NullPointerError", "null 값에서 '%s'에 접근했습니다", name);

    switch (base->kind) {
    case RV_OBJECT: {
        AstInstance *obj = base->u.obj;
        PropDesc *prop = Class_FindProperty(obj->cls, name);
        InstField *field;
        MethodDesc *method;

        if (prop) {
            if (!prop->getter)
                return RT_ThrowValue(in, "AttributeError", "프로퍼티 '%s'에 getter가 없습니다", name);
            return call_fn(in, bind_method(in, prop->getter, base), NULL, 0);
        }
        field = Instance_FindField(obj, name);
        if (field)
            return RValue_Copy(in, field->cell);
        method = Class_FindMethod(obj->cls, name);
        if (method) {
            if (method->overload)
                return bind_overload(in, method->overload, base, NULL);
            return RValue_Func(in, bind_method(in, method->fn, base));
        }
        return RT_ThrowValue(in, "AttributeError", "'%s'에는 '%s' 속성이 없습니다",
                             obj->cls->name, name);
    }
    case RV_PTR: {
        RValue *inner = base->u.ptr.slot ? *base->u.ptr.slot : NULL;
        if (!inner || inner->kind == RV_NULL)
            return RT_ThrowValue(in, "NullPointerError", "null 포인터에서 '%s'에 접근했습니다", name);
        return get_attr(in, inner, name);
    }
    case RV_STRUCT: {
        int index = StructValue_FieldIndex(base->u.stv->type, name);
        if (index < 0)
            return RT_ThrowValue(in, "AttributeError", "'%s' 구조체에 '%s' 필드가 없습니다",
                                 base->u.stv->type->tag, name);
        return RValue_Copy(in, base->u.stv->cells[index]);
    }
    case RV_LIST:
    case RV_TUPLE:
    case RV_DICT:
    case RV_SET:
    case RV_FROZENSET: {
        AstBuiltin *method = Collection_Method(in, base, name);
        if (method)
            return RValue_Builtin(in, method);
        return RT_ThrowValue(in, "AttributeError", "'%s' 객체에는 '%s' 속성이 없습니다",
                             RValue_TypeName(base), name);
    }
    case RV_STR: {
        AstBuiltin *method = String_Method(in, base, name);
        if (method)
            return RValue_Builtin(in, method);
        return RT_ThrowValue(in, "AttributeError", "str 객체에는 '%s' 속성이 없습니다", name);
    }
    case RV_MODULE: {
        RValue *exported = Module_GetExported(in, base->u.module, name);
        if (exported)
            return exported;
        return RT_ThrowValue(in, "AttributeError", "모듈 '%s'에 '%s'이(가) 없습니다",
                             base->u.module->name, name);
    }
    case RV_SUPER: {
        AstClass *owner = base->u.super->owner;
        AstClass *base_cls = owner ? owner->base : NULL;
        MethodDesc *method = base_cls ? Class_FindMethod(base_cls, name) : NULL;
        AstFn *bound;
        if (!method)
            return RT_ThrowValue(in, "AttributeError", "상위 클래스에 '%s' 메서드가 없습니다",
                                 name);
        if (method->overload)
            return bind_overload(in, method->overload, base->u.super->self, base_cls);
        bound = bind_method(in, method->fn, base->u.super->self);
        bound->owner = base_cls;
        return RValue_Func(in, bound);
    }
    case RV_CLASS: {
        AstClass *cls;
        MethodDesc *method;
        for (cls = base->u.cls; cls; cls = cls->base) {
            size_t i;
            for (i = 0; i < cls->nfields; i++) {
                if (cls->fields[i].is_static && !strcmp(cls->fields[i].name, name))
                    return cls->static_values[i] ? cls->static_values[i] : null_value(in);
            }
        }
        method = Class_FindMethod(base->u.cls, name);
        if (method && (method->overload ? method->overload->fns[0]->is_static
                                        : method->fn->is_static)) {
            if (method->overload)
                return bind_overload(in, method->overload, NULL, NULL);
            return RValue_Func(in, method->fn);
        }
        return RT_ThrowValue(in, "AttributeError", "클래스 '%s'에는 정적 멤버 '%s'이(가) 없습니다",
                             base->u.cls->name, name);
    }
    case RV_ENUM:
        if (!strcmp(name, "name"))
            return RValue_Str(in, AsterString_FromCStr(base->u.en.name));
        if (!strcmp(name, "ordinal"))
            return RValue_Int(in, (int64_t)base->u.en.index);
        if (!strcmp(name, "value")) {
            AstEnumMember *m = &base->u.en.type->members[base->u.en.index];
            return m->value ? m->value : RValue_Int(in, (int64_t)base->u.en.index);
        }
        return RT_ThrowValue(in, "AttributeError", "enum 값에는 '%s' 속성이 없습니다", name);
    case RV_ENUMTYPE: {
        size_t i;
        for (i = 0; i < base->u.etype->nmembers; i++) {
            if (!strcmp(base->u.etype->members[i].name, name))
                return RValue_Enum(in, base->u.etype, name, i);
        }
        return RT_ThrowValue(in, "AttributeError", "enum '%s'에 '%s'이(가) 없습니다",
                             base->u.etype->name, name);
    }
    case RV_EXC:
        if (!strcmp(name, "message"))
            return base->u.exc->message;
        if (!strcmp(name, "name"))
            return RValue_Str(in, AsterString_FromCStr(base->u.exc->name));
        return RT_ThrowValue(in, "AttributeError", "예외 값에는 '%s' 속성이 없습니다", name);
    default:
        return RT_ThrowValue(in, "AttributeError", "이 값에는 '%s' 속성이 없습니다", name);
    }
}

/* ---- 타입 검사/캐스트 ---- */

static int class_name_matches(AstClass *cls, const char *name)
{
    for (; cls; cls = cls->base) {
        if (!strcmp(cls->name, name))
            return 1;
    }
    return 0;
}

static int value_is_type(Interp *in, RValue *v, typeref_ty ty)
{
    (void)in;
    if (!v || !ty)
        return 0;
    switch (ty->kind) {
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT:
            return v->kind == RV_INT;
        case FloatT:
            return v->kind == RV_FLOAT;
        case BoolT:
            return v->kind == RV_BOOL;
        case StrT:
            return v->kind == RV_STR;
        case NullT:
            return v->kind == RV_NULL;
        }
        return 0;
    case Nullable_kind:
        return v->kind == RV_NULL || value_is_type(in, v, ty->v.Nullable.base);
    case Pointer_kind:
        if (v->kind == RV_NULL)
            return 1;
        if (v->kind == RV_PTR) {
            RValue *inner = v->u.ptr.slot ? *v->u.ptr.slot : NULL;
            return inner && value_is_type(in, inner, ty->v.Pointer.base);
        }
        return value_is_type(in, v, ty->v.Pointer.base);
    case StructRef_kind:
        if (v->kind == RV_STRUCT) {
            AstStructType *t = v->u.stv->type;
            return !strcmp(t->tag, ty->v.StructRef.tag) ||
                   (t->alias && !strcmp(t->alias, ty->v.StructRef.tag));
        }
        if (v->kind == RV_PTR && v->u.ptr.slot && *v->u.ptr.slot)
            return value_is_type(in, *v->u.ptr.slot, ty);
        return 0;
    case Named_kind: {
        const char *name = ty->v.Named.name;
        if (v->kind == RV_OBJECT)
            return class_name_matches(v->u.obj->cls, name);
        if (v->kind == RV_EXC)
            return !strcmp(v->u.exc->name, name);
        if (v->kind == RV_EXC_CLASS)
            return !strcmp(v->u.exc_name, name);
        if (v->kind == RV_STRUCT) {
            AstStructType *t = v->u.stv->type;
            return !strcmp(t->tag, name) || (t->alias && !strcmp(t->alias, name));
        }
        if (v->kind == RV_PTR && v->u.ptr.slot && *v->u.ptr.slot)
            return value_is_type(in, *v->u.ptr.slot, ty);
        return 0;
    }
    }
    return 0;
}

static RValue *do_cast(Interp *in, typeref_ty ty, RValue *value)
{
    int ok;

    if (!ty)
        return value;
    switch (ty->kind) {
    case Primitive_kind:
        switch (ty->v.Primitive.kind) {
        case IntT: {
            int64_t i = RValue_AsInt(in, value, &ok);
            return ok ? RValue_Int(in, i) : NULL;
        }
        case FloatT: {
            double f = RValue_AsDouble(in, value, &ok);
            return ok ? RValue_Float(in, f) : NULL;
        }
        case BoolT:
            return RValue_Bool(in, RValue_Truthy(value));
        case StrT:
            return RValue_Str(in, RValue_ToString(in, value));
        case NullT:
            return null_value(in);
        }
        return value;
    case Pointer_kind:
    case Nullable_kind:
    case StructRef_kind:
    case Named_kind:
        return value;
    }
    return value;
}

/* ---- 초기화 ---- */

static RValue *eval_initializer(Interp *in, Env *env, typeref_ty ty, initializer_ty init)
{
    if (!init || init->kind == NoInit_kind)
        return DefaultForType(in, env, ty);
    if (init->kind == ExprInit_kind)
        return eval(in, init->v.ExprInit.value, env);
    if (init->kind == PositionalInit_kind || init->kind == DesignatedInit_kind) {
        AstStructType *type = struct_type_from_typeref(in, env, ty);
        RValue *base;
        size_t i;

        if (!type)
            return RT_ThrowValue(in, "TypeError", "구조체 초기화는 구조체 타입에만 쓸 수 있습니다");
        base = DefaultForType(in, env, ty);
        if (!base || base->kind != RV_STRUCT)
            return base;
        if (init->kind == PositionalInit_kind) {
            asdl_seq *values = init->v.PositionalInit.values;
            size_t n = asdl_seq_LEN(values);
            if (n > type->nfields)
                return RT_ThrowValue(in, "TypeError", "'%s' 초기화 값이 너무 많습니다", type->tag);
            for (i = 0; i < n; i++) {
                RValue *v = eval(in, asdl_seq_GET(values, i), env);
                if (in->sig != SIG_OK)
                    return NULL;
                base->u.stv->cells[i] = RValue_Copy(in, v);
            }
        }
        else {
            asdl_seq *items = init->v.DesignatedInit.items;
            size_t n = asdl_seq_LEN(items);
            for (i = 0; i < n; i++) {
                designation_ty d = asdl_seq_GET(items, i);
                int index = StructValue_FieldIndex(type, d->field);
                RValue *v;
                if (index < 0)
                    return RT_ThrowValue(in, "TypeError", "'%s'에 '%s' 필드가 없습니다",
                                         type->tag, d->field);
                v = eval(in, d->value, env);
                if (in->sig != SIG_OK)
                    return NULL;
                base->u.stv->cells[index] = RValue_Copy(in, v);
            }
        }
        return base;
    }
    return null_value(in);
}

/* ---- 클래스/구조체 생성 ---- */

static AstStructType *struct_type_from_typeref(Interp *in, Env *env, typeref_ty ty)
{
    (void)in;
    if (!ty)
        return NULL;
    if (ty->kind == StructRef_kind)
        return Struct_FindType(env, ty->v.StructRef.tag);
    if (ty->kind == Named_kind) {
        RValue **cell = Env_LookupCell(env, ty->v.Named.name);
        if (cell && *cell && (*cell)->kind == RV_STRUCTTYPE)
            return (*cell)->u.stype;
    }
    return NULL;
}

static void resolve_bases(Interp *in, AstClass *cls)
{
    if (!cls->base_name || cls->base)
        return;
    {
        RValue **cell = Env_LookupCell(cls->env, cls->base_name);
        if (!cell || !*cell || (*cell)->kind != RV_CLASS) {
            RT_Throw(in, "TypeError", "기반 클래스 '%s'을(를) 찾을 수 없습니다", cls->base_name);
            return;
        }
        cls->base = (*cell)->u.cls;
        resolve_bases(in, cls->base);
    }
}

static void init_instance_fields(Interp *in, AstClass *cls, AstInstance *obj, Env *self_env)
{
    size_t i;

    if (cls->base)
        init_instance_fields(in, cls->base, obj, self_env);
    for (i = 0; i < cls->nfields; i++) {
        FieldDesc *desc = &cls->fields[i];
        InstField *field;
        RValue *value;
        if (desc->is_static)
            continue;
        field = Instance_FindField(obj, desc->name);
        if (!field)
            continue;
        value = DefaultForType(in, cls->env, desc->ty);
        if (in->sig != SIG_OK)
            return;
        if (desc->init) {
            value = eval(in, desc->init, self_env);
            if (in->sig != SIG_OK)
                return;
        }
        field->cell = RValue_Copy(in, value);
    }
}

static AstFn *find_ctor(AstClass *cls)
{
    for (; cls; cls = cls->base) {
        if (cls->ctor)
            return cls->ctor;
    }
    return NULL;
}

static RValue *instantiate_class(Interp *in, AstClass *cls, RValue **args, size_t nargs)
{
    AstInstance *obj;
    Env *self_env;
    AstFn *ctor;
    RValue *self;

    resolve_bases(in, cls);
    if (in->sig != SIG_OK)
        return NULL;

    obj = Instance_New(in, cls);
    self = RValue_Object(in, obj);
    self_env = Env_New(in, cls->env);
    Env_Define(in, self_env, "self", self);
    init_instance_fields(in, cls, obj, self_env);
    if (in->sig != SIG_OK)
        return NULL;

    ctor = find_ctor(cls);
    if (ctor) {
        call_fn(in, bind_method(in, ctor, self), args, nargs);
        if (in->sig != SIG_OK)
            return NULL;
    }
    else if (nargs != 0) {
        return RT_ThrowValue(in, "TypeError", "'%s'에는 생성자가 없습니다", cls->name);
    }
    return self;
}

static StructCtorDesc *find_struct_ctor(AstStructType *type, size_t nargs)
{
    size_t i;
    for (i = 0; i < type->nctors; i++) {
        if (asdl_seq_LEN(type->ctors[i].params) == nargs)
            return &type->ctors[i];
    }
    return NULL;
}

static int bind_struct_params(Interp *in, asdl_seq *params, Env *env, RValue **args, size_t nargs)
{
    size_t i;
    for (i = 0; i < nargs; i++) {
        param_ty p = asdl_seq_GET(params, i);
        Env_Define(in, env, p->name, RValue_Copy(in, args[i]));
    }
    return 1;
}

static RValue *instantiate_struct(Interp *in, AstStructType *type, RValue **args, size_t nargs)
{
    typeref_ty tref;
    RValue *base;
    StructCtorDesc *desc;
    RValue **slot;
    RValue *self;

    tref = Arena_Alloc(in->arena, sizeof(struct _typeref));
    tref->kind = StructRef_kind;
    tref->v.StructRef.tag = type->tag;
    base = DefaultForType(in, type->env, tref);
    if (in->sig != SIG_OK)
        return NULL;

    slot = Arena_Alloc(in->arena, sizeof(RValue *));
    *slot = base;
    self = RValue_Ptr(in, slot, NULL);

    desc = find_struct_ctor(type, nargs);
    if (!desc) {
        if (nargs == 0 && type->nctors == 0)
            return self;
        return RT_ThrowValue(in, "TypeError", "'%s'에 맞는 생성자가 없습니다", type->tag);
    }

    {
        Env *env = Env_New(in, type->env);
        size_t i;
        asdl_seq *inits = desc->inits;

        Env_Define(in, env, "self", self);
        bind_struct_params(in, desc->params, env, args, nargs);
        for (i = 0; i < asdl_seq_LEN(inits); i++) {
            member_init_ty mi = asdl_seq_GET(inits, i);
            int index = StructValue_FieldIndex(type, mi->field);
            RValue *v;
            if (index < 0)
                return RT_ThrowValue(in, "TypeError", "'%s'에 '%s' 필드가 없습니다",
                                     type->tag, mi->field);
            v = eval(in, mi->value, env);
            if (in->sig != SIG_OK)
                return NULL;
            base->u.stv->cells[index] = RValue_Copy(in, v);
        }
        exec_block(in, desc->body, env);
        if (in->sig != SIG_OK)
            return NULL;
    }
    return self;
}

/* ---- 선언 ---- */

static PropDesc *class_property_slot(AstClass *cls, const char *name)
{
    size_t k;
    for (k = 0; k < cls->nprops; k++) {
        if (!strcmp(cls->props[k].name, name))
            return &cls->props[k];
    }
    return NULL;
}

static MethodDesc *find_method_slot(MethodDesc *methods, size_t count, const char *name)
{
    size_t i;
    for (i = 0; i < count; i++) {
        if (!strcmp(methods[i].name, name))
            return &methods[i];
    }
    return NULL;
}

static AstClass *build_class(Interp *in, stmt_ty s, Env *env)
{
    AstClass *cls = Arena_Alloc(in->arena, sizeof(AstClass));
    asdl_seq *members = s->v.ClassDef.members;
    size_t n = asdl_seq_LEN(members);
    size_t nfields = 0, nmethods = 0, nprops = 0;
    const char **prop_names;
    size_t i;
    size_t fi = 0, mi = 0;

    cls->name = s->v.ClassDef.name;
    cls->env = env;
    if (s->v.ClassDef.base)
        cls->base_name = typeref_name(s->v.ClassDef.base);

    /* Property/Getter/Setter가 같은 이름을 공유하므로 이름 기준으로 먼저 모은다. */
    prop_names = n ? Arena_Alloc(in->arena, n * sizeof(const char *)) : NULL;
    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(members, i);
        const char *name = NULL;
        size_t k;
        int dup = 0;

        if (m->kind == Property_kind)
            name = m->v.Property.name;
        else if (m->kind == Getter_kind)
            name = m->v.Getter.name;
        else if (m->kind == Setter_kind)
            name = m->v.Setter.name;
        if (!name)
            continue;
        for (k = 0; k < nprops; k++) {
            if (!strcmp(prop_names[k], name)) {
                dup = 1;
                break;
            }
        }
        if (!dup)
            prop_names[nprops++] = name;
    }
    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(members, i);
        size_t k;
        int dup = 0;
        if (m->kind == Field_kind) {
            nfields++;
            continue;
        }
        if (m->kind != Method_kind)
            continue;
        /* 같은 이름의 메서드는 하나의 오버로드 묶음으로 센다. */
        for (k = i + 1; k < n; k++) {
            member_ty later = asdl_seq_GET(members, k);
            if (later->kind == Method_kind &&
                !strcmp(later->v.Method.name, m->v.Method.name)) {
                dup = 1;
                break;
            }
        }
        if (!dup)
            nmethods++;
    }

    cls->nfields = nfields;
    cls->fields = nfields ? Arena_Alloc(in->arena, nfields * sizeof(FieldDesc)) : NULL;
    cls->static_values = nfields ? Arena_Alloc(in->arena, nfields * sizeof(RValue *)) : NULL;
    cls->nmethods = nmethods;
    cls->methods = nmethods ? Arena_Alloc(in->arena, nmethods * sizeof(MethodDesc)) : NULL;
    cls->nprops = nprops;
    cls->props = nprops ? Arena_Alloc(in->arena, nprops * sizeof(PropDesc)) : NULL;
    for (i = 0; i < nprops; i++)
        cls->props[i].name = prop_names[i];

    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(members, i);
        if (m->kind == Field_kind) {
            cls->fields[fi].name = m->v.Field.name;
            cls->fields[fi].ty = m->v.Field.ty;
            cls->fields[fi].init = m->v.Field.value;
            cls->fields[fi].is_static = m->v.Field.is_static;
            cls->static_values[fi] = NULL;
            fi++;
        }
    }

    for (i = 0; i < n; i++) {
        member_ty m = asdl_seq_GET(members, i);
        switch (m->kind) {
        case Method_kind: {
            AstFn *fn = make_fn(in, env, m->v.Method.name, m->v.Method.params, 0,
                                m->v.Method.returns, m->v.Method.body, NULL);
            MethodDesc *slot;
            fn->owner = cls;
            fn->is_static = m->v.Method.is_static;
            slot = find_method_slot(cls->methods, mi, m->v.Method.name);
            if (!slot) {
                cls->methods[mi].name = m->v.Method.name;
                cls->methods[mi].fn = fn;
                cls->methods[mi].overload = NULL;
                mi++;
            }
            else if (!slot->overload) {
                AstOverload *ov = Arena_Alloc(in->arena, sizeof(AstOverload));
                ov->count = ov->cap = 0;
                ov->fns = NULL;
                overload_append(in, ov, slot->fn);
                overload_append(in, ov, fn);
                slot->overload = ov;
            }
            else {
                overload_append(in, slot->overload, fn);
            }
            break;
        }
        case Constructor_kind:
            cls->ctor = make_fn(in, env, cls->name, m->v.Constructor.params, 0, NULL,
                                m->v.Constructor.body, NULL);
            cls->ctor->owner = cls;
            break;
        case Getter_kind: {
            PropDesc *prop = class_property_slot(cls, m->v.Getter.name);
            if (prop) {
                prop->getter = make_fn(in, env, m->v.Getter.name, NULL, 0, NULL,
                                       m->v.Getter.body, NULL);
                prop->getter->owner = cls;
            }
            break;
        }
        case Setter_kind: {
            PropDesc *prop = class_property_slot(cls, m->v.Setter.name);
            if (prop) {
                asdl_seq *params = _Aster_seq_new(1, in->arena);
                asdl_seq_SET(params, 0, m->v.Setter.param);
                prop->setter = make_fn(in, env, m->v.Setter.name, params, 0, NULL,
                                       m->v.Setter.body, NULL);
                prop->setter->owner = cls;
            }
            break;
        }
        default:
            break;
        }
    }

    for (i = 0; i < nfields; i++) {
        FieldDesc *desc = &cls->fields[i];
        RValue *value;
        if (!desc->is_static)
            continue;
        value = DefaultForType(in, env, desc->ty);
        if (in->sig != SIG_OK)
            return cls;
        if (desc->init) {
            value = eval(in, desc->init, env);
            if (in->sig != SIG_OK)
                return cls;
        }
        cls->static_values[i] = RValue_Copy(in, value);
    }
    return cls;
}

static AstStructType *build_struct(Interp *in, stmt_ty s, Env *env)
{
    AstStructType *type = Arena_Alloc(in->arena, sizeof(AstStructType));
    asdl_seq *members = s->v.StructDef.members;
    size_t n = asdl_seq_LEN(members);
    size_t nfields = 0, nctors = 0;
    size_t i, fi = 0, ci = 0;

    type->tag = s->v.StructDef.tag;
    type->alias = s->v.StructDef.alias;
    type->env = env;

    for (i = 0; i < n; i++) {
        struct_member_ty m = asdl_seq_GET(members, i);
        if (m->kind == StructField_kind)
            nfields++;
        else
            nctors++;
    }
    type->nfields = nfields;
    type->fields = nfields ? Arena_Alloc(in->arena, nfields * sizeof(StructFieldDesc)) : NULL;
    type->nctors = nctors;
    type->ctors = nctors ? Arena_Alloc(in->arena, nctors * sizeof(StructCtorDesc)) : NULL;

    for (i = 0; i < n; i++) {
        struct_member_ty m = asdl_seq_GET(members, i);
        if (m->kind == StructField_kind) {
            type->fields[fi].name = m->v.StructField.name;
            type->fields[fi].ty = m->v.StructField.ty;
            fi++;
        }
        else {
            type->ctors[ci].params = m->v.StructCtor.params;
            type->ctors[ci].inits = m->v.StructCtor.inits;
            type->ctors[ci].body = m->v.StructCtor.body;
            ci++;
        }
    }
    return type;
}

static RValue *bi_extern_call(Interp *in, RValue *self, RValue **args, size_t nargs)
{
    const char *name = self && self->kind == RV_STR ? self->u.s->data : "?";
    RValue **cell = Env_LookupCell(in->globals, name);
    if (cell && *cell && (*cell)->kind == RV_BUILTIN && (*cell)->u.builtin->fn != bi_extern_call)
        return (*cell)->u.builtin->fn(in, (*cell)->u.builtin->self, args, nargs);
    return RT_ThrowValue(in, "NotImplementedError",
                         "extern 함수 '%s'에 연결된 네이티브 구현이 없습니다", name);
}

static Signal exec_decl(Interp *in, stmt_ty s, Env *env)
{
    switch (s->kind) {
    case FunctionDef_kind:
        if (!Env_HasLocal(env, s->v.FunctionDef.name)) {
            if (s->v.FunctionDef.is_extern) {
                AstBuiltin *b = Builtin_New(in, s->v.FunctionDef.name, bi_extern_call);
                b->self = RValue_Str(in, AsterString_FromCStr(s->v.FunctionDef.name));
                Env_Define(in, env, s->v.FunctionDef.name, RValue_Builtin(in, b));
                return SIG_OK;
            }
            AstFn *fn = make_fn(in, env, s->v.FunctionDef.name, s->v.FunctionDef.params, 0,
                                s->v.FunctionDef.returns, s->v.FunctionDef.body, NULL);
            Env_Define(in, env, s->v.FunctionDef.name, RValue_Func(in, fn));
        }
        else if (in->predeclaring) {
            /* 같은 이름의 선언은 오버로드 묶음이 된다. */
            RValue **cell = Env_LookupCell(env, s->v.FunctionDef.name);
            if (cell && *cell && (*cell)->kind == RV_FUNC) {
                AstOverload *ov = Arena_Alloc(in->arena, sizeof(AstOverload));
                AstFn *fn = make_fn(in, env, s->v.FunctionDef.name,
                                    s->v.FunctionDef.params, 0,
                                    s->v.FunctionDef.returns, s->v.FunctionDef.body, NULL);
                ov->count = ov->cap = 0;
                ov->fns = NULL;
                overload_append(in, ov, (*cell)->u.fn);
                overload_append(in, ov, fn);
                *cell = RValue_Overload(in, ov);
            }
            else if (cell && *cell && (*cell)->kind == RV_OVERLOAD) {
                AstFn *fn = make_fn(in, env, s->v.FunctionDef.name,
                                    s->v.FunctionDef.params, 0,
                                    s->v.FunctionDef.returns, s->v.FunctionDef.body, NULL);
                overload_append(in, (*cell)->u.overload, fn);
            }
        }
        return SIG_OK;
    case ClassDef_kind:
        if (!Env_HasLocal(env, s->v.ClassDef.name)) {
            AstClass *cls = build_class(in, s, env);
            Env_Define(in, env, s->v.ClassDef.name, RValue_Class(in, cls));
        }
        return SIG_OK;
    case StructDef_kind:
        if (!Env_HasLocal(env, s->v.StructDef.tag)) {
            AstStructType *type = build_struct(in, s, env);
            Env_Define(in, env, type->tag, RValue_StructType(in, type));
            if (type->alias && strcmp(type->alias, type->tag) != 0)
                Env_Define(in, env, type->alias, RValue_StructType(in, type));
        }
        return SIG_OK;
    case InterfaceDef_kind:
        if (!Env_HasLocal(env, s->v.InterfaceDef.name)) {
            AstClass *cls = Arena_Alloc(in->arena, sizeof(AstClass));
            cls->name = s->v.InterfaceDef.name;
            cls->env = env;
            Env_Define(in, env, cls->name, RValue_Class(in, cls));
        }
        return SIG_OK;
    case ExceptionDef_kind:
        if (!Env_HasLocal(env, s->v.ExceptionDef.name)) {
            const char *base = s->v.ExceptionDef.base;
            Exception_Register(in, s->v.ExceptionDef.name, base);
            Env_Define(in, env, s->v.ExceptionDef.name,
                       RValue_ExcClass(in, s->v.ExceptionDef.name));
        }
        return SIG_OK;
    case EnumDef_kind:
        if (!Env_HasLocal(env, s->v.EnumDef.name)) {
            AstEnumType *et = Arena_Alloc(in->arena, sizeof(AstEnumType));
            size_t n = asdl_seq_LEN(s->v.EnumDef.members);
            size_t i;
            et->name = s->v.EnumDef.name;
            et->env = env;
            et->nmembers = n;
            et->members = n ? Arena_Alloc(in->arena, n * sizeof(AstEnumMember)) : NULL;
            for (i = 0; i < n; i++) {
                enum_member_ty em = asdl_seq_GET(s->v.EnumDef.members, i);
                et->members[i].name = em->name;
                if (em->value) {
                    et->members[i].value = eval(in, em->value, env);
                    if (in->sig != SIG_OK)
                        return in->sig;
                }
                else {
                    et->members[i].value = RValue_Int(in, (int64_t)i);
                }
            }
            Env_Define(in, env, et->name, RValue_EnumType(in, et));
        }
        return SIG_OK;
    default:
        return SIG_OK;
    }
}

static void predeclare_one(Interp *in, Env *env, stmt_ty s)
{
    switch (s->kind) {
    case FunctionDef_kind:
    case ClassDef_kind:
    case StructDef_kind:
    case InterfaceDef_kind:
    case ExceptionDef_kind:
    case EnumDef_kind:
        exec_decl(in, s, env);
        break;
    case Export_kind:
        predeclare_one(in, env, s->v.Export.body);
        break;
    default:
        break;
    }
}

static void predeclare(Interp *in, Env *env, asdl_seq *body)
{
    size_t i;
    in->predeclaring++;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        predeclare_one(in, env, asdl_seq_GET(body, i));
        if (in->sig != SIG_OK)
            break;
    }
    in->predeclaring--;
}

/* ---- 문장 ---- */

static Signal exec_block(Interp *in, asdl_seq *body, Env *env)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        Signal sig = exec_stmt(in, asdl_seq_GET(body, i), env);
        if (sig != SIG_OK)
            return sig;
    }
    return SIG_OK;
}

static int declared_type_known(typeref_ty ty)
{
    if (!ty)
        return 0;
    switch (ty->kind) {
    case Primitive_kind:
    case Pointer_kind:
    case Nullable_kind:
    case StructRef_kind:
        return 1;
    case Named_kind: {
        const char *name = ty->v.Named.name;
        return strcmp(name, "var") && strcmp(name, "any") && strcmp(name, "list") &&
               strcmp(name, "dict") && strcmp(name, "set") && strcmp(name, "tuple") &&
               strcmp(name, "frozenset") && strcmp(name, "map");
    }
    }
    return 0;
}

static int check_declared_value(Interp *in, typeref_ty ty, RValue *value)
{
    if (!declared_type_known(ty) || !value)
        return 1;
    if (ty->kind == Named_kind) {
        RValue **cell = Env_LookupCell(in->globals, ty->v.Named.name);
        if (!cell || !*cell ||
            ((*cell)->kind != RV_CLASS && (*cell)->kind != RV_STRUCTTYPE &&
             (*cell)->kind != RV_EXC_CLASS))
            return 1;
    }
    if (!value_is_type(in, value, ty)) {
        RT_Throw(in, "TypeError", "%s 타입 변수에 %s 값을 넣을 수 없습니다",
                 typeref_name(ty), RValue_TypeName(value));
        return 0;
    }
    return 1;
}

static Signal exec_var_decl(Interp *in, stmt_ty s, Env *env)
{
    RValue *value = eval_initializer(in, env, s->v.VarDecl.ty, s->v.VarDecl.init);
    if (in->sig != SIG_OK)
        return in->sig;
    if (!check_declared_value(in, s->v.VarDecl.ty, value))
        return in->sig;
    if (s->v.VarDecl.is_const)
        Env_DefineConst(in, env, s->v.VarDecl.name, RValue_Copy(in, value));
    else
        Env_Define(in, env, s->v.VarDecl.name, RValue_Copy(in, value));
    return SIG_OK;
}

static Signal exec_destructure(Interp *in, stmt_ty s, Env *env)
{
    RValue *value = eval(in, s->v.Destructure.value, env);
    asdl_seq *names = s->v.Destructure.names;
    size_t n = asdl_seq_LEN(names);
    size_t len, i;
    int ok = 1;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!RValue_Iterable(value)) {
        RT_Throw(in, "TypeError", "%s 값은 구조 분해할 수 없습니다", RValue_TypeName(value));
        return SIG_THROW;
    }
    len = RValue_IterLen(in, value, &ok);
    if (!ok)
        return in->sig;
    if (len != n) {
        RT_Throw(in, "ValueError", "값 %zu개를 이름 %zu개로 분해할 수 없습니다", len, n);
        return SIG_THROW;
    }
    for (i = 0; i < n; i++) {
        RValue *item = RValue_IterAt(in, value, i, &ok);
        if (!ok)
            return in->sig;
        Env_Define(in, env, (identifier)asdl_seq_GET(names, i), RValue_Copy(in, item));
    }
    return SIG_OK;
}

static Signal exec_assert(Interp *in, stmt_ty s, Env *env)
{
    RValue *test = eval(in, s->v.Assert.test, env);
    if (in->sig != SIG_OK)
        return in->sig;
    if (RValue_Truthy(test))
        return SIG_OK;
    if (s->v.Assert.message) {
        RValue *msg = eval(in, s->v.Assert.message, env);
        AsterString *text;
        if (in->sig != SIG_OK)
            return in->sig;
        text = RValue_ToString(in, msg);
        RT_Throw(in, "AssertionError", "%.*s", (int)text->length, text->data);
    }
    else {
        RT_Throw(in, "AssertionError", "단언이 실패했습니다");
    }
    return SIG_THROW;
}

typedef struct {
    RValue **slot;
    PropDesc *prop;
    RValue *self;
    RValue *container;       /* subscript 대상이면 NULL이 아니다 */
    RValue *index;
    int is_subscript;
} Target;

/* lvalue 체인/& 연산용: 필드를 복사하지 않고 실제 셀을 찾는다. */
static int attr_target(Interp *in, RValue *base, const char *name, Target *out)
{
    out->slot = NULL;
    out->prop = NULL;
    out->self = NULL;
    out->container = NULL;
    out->index = NULL;
    out->is_subscript = 0;

    if (base && base->kind == RV_PTR)
        base = base->u.ptr.slot ? *base->u.ptr.slot : NULL;
    if (!base || base->kind == RV_NULL) {
        RT_Throw(in, "NullPointerError", "null 값의 '%s'에 접근했습니다", name);
        return 0;
    }
    if (base->kind == RV_CLASS) {
        AstClass *cls;
        for (cls = base->u.cls; cls; cls = cls->base) {
            size_t i;
            for (i = 0; i < cls->nfields; i++) {
                if (cls->fields[i].is_static && !strcmp(cls->fields[i].name, name)) {
                    out->slot = &cls->static_values[i];
                    return 1;
                }
            }
        }
        RT_Throw(in, "AttributeError", "클래스 '%s'에는 정적 필드 '%s'이(가) 없습니다",
                 base->u.cls->name, name);
        return 0;
    }
    if (base->kind == RV_OBJECT) {
        PropDesc *prop = Class_FindProperty(base->u.obj->cls, name);
        InstField *field;
        if (prop) {
            out->prop = prop;
            out->self = base;
            return 1;
        }
        field = Instance_FindField(base->u.obj, name);
        if (!field) {
            RT_Throw(in, "AttributeError", "'%s'에는 '%s' 필드가 없습니다",
                     base->u.obj->cls->name, name);
            return 0;
        }
        out->slot = &field->cell;
        return 1;
    }
    if (base->kind == RV_STRUCT) {
        int index = StructValue_FieldIndex(base->u.stv->type, name);
        if (index < 0) {
            RT_Throw(in, "AttributeError", "'%s' 구조체에 '%s' 필드가 없습니다",
                     base->u.stv->type->tag, name);
            return 0;
        }
        out->slot = &base->u.stv->cells[index];
        return 1;
    }
    RT_Throw(in, "AttributeError", "이 값에는 '%s'에 접근할 수 없습니다", name);
    return 0;
}

/* 변수/필드/포인터 대상/프로퍼티를 복사 없이 평가한다. */
static RValue *eval_ref(Interp *in, expr_ty e, Env *env)
{
    switch (e->kind) {
    case Name_kind: {
        RValue **cell = Env_LookupCell(env, e->v.Name.id);
        if (!cell || !*cell) {
            RT_Throw(in, "NameError", "이름 '%s'이(가) 정의되지 않았습니다", e->v.Name.id);
            return NULL;
        }
        return *cell;
    }
    case Deref_kind: {
        RValue *p = eval_ref(in, e->v.Deref.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (p && p->kind == RV_OBJECT)
            return p;
        if (!p || p->kind == RV_NULL) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return NULL;
        }
        if (p->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "역참조할 수 없는 값입니다");
            return NULL;
        }
        if (!p->u.ptr.slot || !*p->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return NULL;
        }
        return *p->u.ptr.slot;
    }
    case Attribute_kind:
    case PtrAttribute_kind: {
        expr_ty base_expr = e->kind == Attribute_kind
                                ? e->v.Attribute.value
                                : e->v.PtrAttribute.value;
        const char *name = e->kind == Attribute_kind
                               ? e->v.Attribute.attr
                               : e->v.PtrAttribute.attr;
        RValue *base = eval_ref(in, base_expr, env);
        Target t;
        if (in->sig != SIG_OK)
            return NULL;
        if (!attr_target(in, base, name, &t))
            return NULL;
        if (t.prop)
            return property_read(in, t.prop, t.self);
        return *t.slot;
    }
    default:
        return eval(in, e, env);
    }
}

static int resolve_target(Interp *in, expr_ty target, Env *env, Target *out)
{
    out->slot = NULL;
    out->prop = NULL;
    out->self = NULL;
    out->container = NULL;
    out->index = NULL;
    out->is_subscript = 0;

    switch (target->kind) {
    case Name_kind: {
        RValue **cell = Env_LookupCell(env, target->v.Name.id);
        if (!cell) {
            RT_Throw(in, "NameError", "이름 '%s'이(가) 정의되지 않았습니다", target->v.Name.id);
            return 0;
        }
        out->slot = cell;
        return 1;
    }
    case Deref_kind: {
        RValue *p = eval_ref(in, target->v.Deref.value, env);
        if (in->sig != SIG_OK)
            return 0;
        if (!p || p->kind == RV_NULL) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return 0;
        }
        if (p->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "역참조할 수 없는 값에 대입했습니다");
            return 0;
        }
        if (!p->u.ptr.slot || !*p->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return 0;
        }
        out->slot = p->u.ptr.slot;
        return 1;
    }
    case Attribute_kind:
    case PtrAttribute_kind: {
        expr_ty base_expr = target->kind == Attribute_kind
                                ? target->v.Attribute.value
                                : target->v.PtrAttribute.value;
        const char *name = target->kind == Attribute_kind
                               ? target->v.Attribute.attr
                               : target->v.PtrAttribute.attr;
        RValue *base = eval_ref(in, base_expr, env);
        if (in->sig != SIG_OK)
            return 0;
        return attr_target(in, base, name, out);
    }
    case Subscript_kind: {
        RValue *container = eval_ref(in, target->v.Subscript.value, env);
        RValue *index;
        if (in->sig != SIG_OK)
            return 0;
        if (target->v.Subscript.index->kind == Slice_kind) {
            RT_Throw(in, "TypeError", "슬라이스 대입은 아직 지원하지 않습니다");
            return 0;
        }
        index = eval(in, target->v.Subscript.index, env);
        if (in->sig != SIG_OK)
            return 0;
        out->container = container;
        out->index = index;
        out->is_subscript = 1;
        return 1;
    }
    default:
        RT_Throw(in, "TypeError", "대입할 수 없는 대상입니다");
        return 0;
    }
}

static RValue *property_read(Interp *in, PropDesc *prop, RValue *self)
{
    if (!prop->getter)
        return RT_ThrowValue(in, "AttributeError", "프로퍼티 '%s'에 getter가 없습니다", prop->name);
    return call_fn(in, bind_method(in, prop->getter, self), NULL, 0);
}

static void property_write(Interp *in, PropDesc *prop, RValue *self, RValue *value)
{
    RValue *args[1];
    if (!prop->setter) {
        RT_Throw(in, "AttributeError", "프로퍼티 '%s'에 setter가 없습니다", prop->name);
        return;
    }
    args[0] = value;
    call_fn(in, bind_method(in, prop->setter, self), args, 1);
}

static int is_collection_value(const RValue *v)
{
    return v && (v->kind == RV_LIST || v->kind == RV_TUPLE ||
                 v->kind == RV_DICT || v->kind == RV_SET ||
                 v->kind == RV_FROZENSET);
}

static int eval_slice_index_bound(Interp *in, expr_ty e, Env *env, size_t fallback, int *out)
{
    RValue *v;
    int64_t n;
    if (!e) {
        *out = (int)fallback;
        return 1;
    }
    v = eval(in, e, env);
    if (in->sig != SIG_OK)
        return 0;
    if (!v || v->kind != RV_INT) {
        RT_Throw(in, "TypeError", "슬라이스 경계는 int여야 합니다");
        return 0;
    }
    n = v->u.i;
    if (n < 0)
        n += (int64_t)fallback;
    if (n < 0)
        n = 0;
    if (n > (int64_t)fallback)
        n = (int64_t)fallback;
    *out = (int)n;
    return 1;
}

/* list[lo:hi] = 값들. step은 1만 지원한다. */
static Signal exec_slice_assign(Interp *in, expr_ty target, RValue *value, Env *env)
{
    RValue *container = eval_ref(in, target->v.Subscript.value, env);
    expr_ty slice = target->v.Subscript.index;
    int lo, hi;
    size_t len, i, count;
    int ok = 1;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!container || container->kind != RV_LIST) {
        RT_Throw(in, "TypeError", "슬라이스 대입은 리스트에만 쓸 수 있습니다");
        return SIG_THROW;
    }
    len = container->u.list->len;
    if (slice->v.Slice.step) {
        RValue *step = eval(in, slice->v.Slice.step, env);
        if (in->sig != SIG_OK)
            return in->sig;
        if (!step || step->kind != RV_INT || step->u.i != 1) {
            RT_Throw(in, "ValueError", "슬라이스 대입의 step은 1이어야 합니다");
            return SIG_THROW;
        }
    }
    if (!eval_slice_index_bound(in, slice->v.Slice.lower, env, len, &lo) ||
        !eval_slice_index_bound(in, slice->v.Slice.upper, env, len, &hi))
        return in->sig;
    if (hi < lo)
        hi = lo;
    if (!RValue_Iterable(value)) {
        RT_Throw(in, "TypeError", "슬라이스 대입에는 순회 가능한 값이 필요합니다");
        return SIG_THROW;
    }
    count = RValue_IterLen(in, value, &ok);
    if (!ok)
        return in->sig;
    for (i = 0; i < (size_t)(hi - lo); i++)
        List_RemoveAt(container->u.list, (size_t)lo);
    for (i = 0; i < count; i++) {
        RValue *item = RValue_IterAt(in, value, i, &ok);
        if (!ok)
            return in->sig;
        List_Insert(in, container->u.list, (size_t)lo + i, RValue_Copy(in, item));
    }
    return SIG_OK;
}

static Signal exec_assign(Interp *in, stmt_ty s, Env *env)
{
    expr_ty target = s->v.Assign.target;
    RValue *value = eval(in, s->v.Assign.value, env);
    Target tgt;

    if (in->sig != SIG_OK)
        return in->sig;

    if (target->kind == Subscript_kind &&
        target->v.Subscript.index->kind == Slice_kind) {
        if (s->v.Assign.op != AssignEq) {
            RT_Throw(in, "TypeError", "슬라이스 대입에는 '='만 쓸 수 있습니다");
            return SIG_THROW;
        }
        return exec_slice_assign(in, target, value, env);
    }

    if (target->kind == Tuple_kind) {
        asdl_seq *elems = target->v.Tuple.elements;
        size_t n = asdl_seq_LEN(elems);
        size_t len, i;
        int ok = 1;
        if (s->v.Assign.op != AssignEq) {
            RT_Throw(in, "TypeError", "구조 분해에는 '='만 쓸 수 있습니다");
            return SIG_THROW;
        }
        if (!RValue_Iterable(value)) {
            RT_Throw(in, "TypeError", "%s 값은 구조 분해할 수 없습니다", RValue_TypeName(value));
            return SIG_THROW;
        }
        len = RValue_IterLen(in, value, &ok);
        if (!ok)
            return in->sig;
        if (len != n) {
            RT_Throw(in, "ValueError", "값 %zu개를 이름 %zu개로 분해할 수 없습니다", len, n);
            return SIG_THROW;
        }
        for (i = 0; i < n; i++) {
            expr_ty sub = asdl_seq_GET(elems, i);
            RValue *item = RValue_IterAt(in, value, i, &ok);
            if (!ok)
                return in->sig;
            if (sub->kind != Name_kind) {
                RT_Throw(in, "TypeError", "구조 분해 대상은 이름이어야 합니다");
                return SIG_THROW;
            }
            if (Env_IsConst(env, sub->v.Name.id)) {
                RT_Throw(in, "TypeError", "const 변수 '%s'에는 대입할 수 없습니다",
                         sub->v.Name.id);
                return SIG_THROW;
            }
            Env_Define(in, env, sub->v.Name.id, RValue_Copy(in, item));
        }
        return SIG_OK;
    }

    if (target->kind == Name_kind && Env_IsConst(env, target->v.Name.id)) {
        RT_Throw(in, "TypeError", "const 변수 '%s'에는 대입할 수 없습니다",
                 target->v.Name.id);
        return SIG_THROW;
    }

    if (target->kind == Name_kind && !Env_LookupCell(env, target->v.Name.id)) {
        /* 컬렉션에는 타입 이름이 없으므로 함수/람다처럼 첫 대입에서 정의한다. */
        if (value->kind == RV_FUNC || value->kind == RV_BUILTIN ||
            is_collection_value(value)) {
            Env_Define(in, env, target->v.Name.id, RValue_Copy(in, value));
            return SIG_OK;
        }
        RT_Throw(in, "NameError", "이름 '%s'이(가) 정의되지 않았습니다", target->v.Name.id);
        return SIG_THROW;
    }

    if (!resolve_target(in, target, env, &tgt))
        return in->sig;

    if (tgt.prop) {
        if (s->v.Assign.op != AssignEq) {
            RValue *current = property_read(in, tgt.prop, tgt.self);
            if (in->sig != SIG_OK)
                return in->sig;
            value = apply_binop(in, assign_op_to_operator(s->v.Assign.op), current, value);
            if (in->sig != SIG_OK)
                return in->sig;
        }
        property_write(in, tgt.prop, tgt.self, value);
        return in->sig;
    }

    if (tgt.is_subscript) {
        int ok;
        if (s->v.Assign.op != AssignEq) {
            RValue *current = RValue_GetItem(in, tgt.container, tgt.index, &ok);
            if (!ok)
                return in->sig;
            value = apply_binop(in, assign_op_to_operator(s->v.Assign.op), current, value);
            if (in->sig != SIG_OK)
                return in->sig;
        }
        if (!RValue_SetItem(in, tgt.container, tgt.index, value, &ok))
            return in->sig;
        return SIG_OK;
    }

    if (s->v.Assign.op != AssignEq) {
        value = apply_binop(in, assign_op_to_operator(s->v.Assign.op), *tgt.slot, value);
        if (in->sig != SIG_OK)
            return in->sig;
    }
    *tgt.slot = RValue_Copy(in, value);
    return SIG_OK;
}

static Signal exec_if(Interp *in, stmt_ty s, Env *env)
{
    RValue *test = eval(in, s->v.If.test, env);
    if (in->sig != SIG_OK)
        return in->sig;
    if (RValue_Truthy(test))
        return exec_block(in, s->v.If.body, Env_New(in, env));
    return exec_block(in, s->v.If.orelse, Env_New(in, env));
}

static Signal exec_while(Interp *in, stmt_ty s, Env *env)
{
    for (;;) {
        RValue *test = eval(in, s->v.While.test, env);
        Signal sig;
        if (in->sig != SIG_OK)
            return in->sig;
        if (!RValue_Truthy(test))
            break;
        sig = exec_block(in, s->v.While.body, Env_New(in, env));
        if (sig == SIG_BREAK) {
            in->sig = SIG_OK;
            break;
        }
        if (sig == SIG_CONTINUE) {
            in->sig = SIG_OK;
            continue;
        }
        if (sig != SIG_OK)
            return sig;
    }
    return SIG_OK;
}

static Signal exec_for(Interp *in, stmt_ty s, Env *env)
{
    Env *loop_env = Env_New(in, env);

    exec_stmt(in, s->v.For.init, loop_env);
    if (in->sig != SIG_OK)
        return in->sig;
    for (;;) {
        RValue *test = eval(in, s->v.For.test, loop_env);
        Signal sig;
        if (in->sig != SIG_OK)
            return in->sig;
        if (!RValue_Truthy(test))
            break;
        sig = exec_block(in, s->v.For.body, Env_New(in, loop_env));
        if (sig == SIG_BREAK) {
            in->sig = SIG_OK;
            break;
        }
        if (sig != SIG_OK && sig != SIG_CONTINUE)
            return sig;
        in->sig = SIG_OK;
        exec_stmt(in, s->v.For.step, loop_env);
        if (in->sig != SIG_OK)
            return in->sig;
    }
    return SIG_OK;
}

static Signal exec_for_range(Interp *in, stmt_ty s, Env *env)
{
    Env *loop_env = Env_New(in, env);
    RValue *start = eval(in, s->v.ForRange.iter->start, env);
    RValue *stop;
    RValue *step_value;
    int ok;
    int64_t from, to, step;
    RValue **cell;
    int64_t i;

    if (in->sig != SIG_OK)
        return in->sig;
    stop = eval(in, s->v.ForRange.iter->stop, env);
    if (in->sig != SIG_OK)
        return in->sig;
    if (s->v.ForRange.iter->step) {
        step_value = eval(in, s->v.ForRange.iter->step, env);
        if (in->sig != SIG_OK)
            return in->sig;
    }
    else {
        step_value = RValue_Int(in, 1);
    }

    from = RValue_AsInt(in, start, &ok);
    if (!ok)
        return in->sig;
    to = RValue_AsInt(in, stop, &ok);
    if (!ok)
        return in->sig;
    step = RValue_AsInt(in, step_value, &ok);
    if (!ok)
        return in->sig;
    if (step == 0) {
        RT_Throw(in, "ValueError", "범위 for의 간격은 0일 수 없습니다");
        return SIG_THROW;
    }

    Env_Define(in, loop_env, s->v.ForRange.name, RValue_Int(in, from));
    cell = Env_LookupCell(loop_env, s->v.ForRange.name);
    for (i = from; step > 0 ? i < to : i > to; i += step) {
        Signal sig;
        *cell = RValue_Int(in, i);
        sig = exec_block(in, s->v.ForRange.body, Env_New(in, loop_env));
        if (sig == SIG_BREAK) {
            in->sig = SIG_OK;
            break;
        }
        if (sig == SIG_CONTINUE) {
            in->sig = SIG_OK;
            continue;
        }
        if (sig != SIG_OK)
            return sig;
    }
    return SIG_OK;
}

static Signal exec_for_in(Interp *in, stmt_ty s, Env *env)
{
    Env *loop_env = Env_New(in, env);
    RValue *iterable = eval(in, s->v.ForIn.iter, env);
    RValue **cell;
    size_t len, i;
    int ok = 1;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!RValue_Iterable(iterable)) {
        RT_Throw(in, "TypeError", "%s 값은 for-in으로 순회할 수 없습니다",
                 RValue_TypeName(iterable));
        return SIG_THROW;
    }
    len = RValue_IterLen(in, iterable, &ok);
    if (!ok)
        return in->sig;

    Env_Define(in, loop_env, s->v.ForIn.name, null_value(in));
    cell = Env_LookupCell(loop_env, s->v.ForIn.name);
    for (i = 0; i < len; i++) {
        RValue *item = RValue_IterAt(in, iterable, i, &ok);
        Signal sig;
        if (!ok)
            return in->sig;
        if (!check_declared_value(in, s->v.ForIn.ty, item))
            return in->sig;
        *cell = item;
        sig = exec_block(in, s->v.ForIn.body, Env_New(in, loop_env));
        if (sig == SIG_BREAK) {
            in->sig = SIG_OK;
            break;
        }
        if (sig == SIG_CONTINUE) {
            in->sig = SIG_OK;
            continue;
        }
        if (sig != SIG_OK)
            return sig;
    }
    return SIG_OK;
}

static Signal exec_try(Interp *in, stmt_ty s, Env *env)
{
    Signal sig = exec_block(in, s->v.Try.body, Env_New(in, env));

    if (sig == SIG_THROW) {
        asdl_seq *handlers = s->v.Try.handlers;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(handlers); i++) {
            handler_ty h = asdl_seq_GET(handlers, i);
            const char *name = typeref_name(h->ty);
            if (Exception_Matches(in, in->exc->u.exc->name, name)) {
                Env *handler_env = Env_New(in, env);
                RValue *exc = in->exc;
                in->exc = NULL;
                in->sig = SIG_OK;
                Env_Define(in, handler_env, h->name, exc);
                sig = exec_block(in, h->body, handler_env);
                break;
            }
        }
    }

    if (asdl_seq_LEN(s->v.Try.finalbody) > 0) {
        Signal pending = sig;
        Signal fsig;
        /* finally는 보류 중인 신호와 무관하게 정상 상태에서 실행한다. */
        in->sig = SIG_OK;
        fsig = exec_block(in, s->v.Try.finalbody, Env_New(in, env));
        if (fsig != SIG_OK)
            sig = fsig;
        else
            in->sig = sig = pending;
    }
    return sig;
}

static Signal exec_return(Interp *in, stmt_ty s, Env *env)
{
    if (s->v.Return.value)
        in->ret = eval(in, s->v.Return.value, env);
    else
        in->ret = null_value(in);
    if (in->sig != SIG_OK)
        return in->sig;
    in->sig = SIG_RETURN;
    return SIG_RETURN;
}

static Signal exec_throw(Interp *in, stmt_ty s, Env *env)
{
    RValue *value = eval(in, s->v.Throw.value, env);
    RValue *exc;

    if (in->sig != SIG_OK)
        return in->sig;
    exc = make_exception(in, value, s->v.Throw.exc_type);
    if (in->sig != SIG_OK)
        return in->sig;
    in->exc = exc;
    in->sig = SIG_THROW;
    return SIG_THROW;
}

/* ---- 모듈 로더 ---- */

static char *read_source_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    size_t cap = 4096, len = 0, n;
    char *buf;

    if (!f)
        return NULL;
    buf = malloc(cap + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
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
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[len] = 0;
    *size = len;
    return buf;
}

static char *module_name_from_path(Interp *in, asdl_seq *path)
{
    size_t total = 1, i, o = 0;
    char *name;

    for (i = 0; i < asdl_seq_LEN(path); i++) {
        const char *part = asdl_seq_GET(path, i);
        total += strlen(part) + 1;
    }
    name = Arena_Alloc(in->arena, total);
    for (i = 0; i < asdl_seq_LEN(path); i++) {
        const char *part = asdl_seq_GET(path, i);
        size_t n = strlen(part);
        if (i > 0)
            name[o++] = '.';
        memcpy(name + o, part, n);
        o += n;
    }
    name[o] = 0;
    return name;
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

static int resolve_module_file(Interp *in, const char *name, char *out, size_t cap)
{
    char rel[512], with_ast[600], with_init[620];
    size_t i, o = 0;

    for (i = 0; name[i] && o + 1 < sizeof(rel); i++)
        rel[o++] = name[i] == '.' ? '/' : name[i];
    rel[o] = 0;
    snprintf(with_ast, sizeof with_ast, "%s.ast", rel);
    snprintf(with_init, sizeof with_init, "%s/__init__.ast", rel);

    if (try_module_file(in->base_dir, with_ast, out, cap))
        return 1;
    if (try_module_file(in->base_dir, with_init, out, cap))
        return 1;
    if (try_module_file(in->stdlib_dir, with_ast, out, cap))
        return 1;
    if (try_module_file(in->stdlib_dir, with_init, out, cap))
        return 1;
    for (i = 0; i < in->nmodule_dirs; i++) {
        if (try_module_file(in->module_dirs[i], with_ast, out, cap))
            return 1;
        if (try_module_file(in->module_dirs[i], with_init, out, cap))
            return 1;
    }
    return 0;
}

static AstModule *module_cache_find(Interp *in, const char *name)
{
    size_t i;
    for (i = 0; i < in->nmodules; i++) {
        if (!strcmp(in->modules[i]->name, name))
            return in->modules[i];
    }
    return NULL;
}

static void module_cache_add(Interp *in, AstModule *module)
{
    if (in->nmodules == in->cap_modules) {
        size_t cap = in->cap_modules ? in->cap_modules * 2 : 8;
        AstModule **grown = realloc(in->modules, cap * sizeof(AstModule *));
        if (!grown)
            return;
        in->modules = grown;
        in->cap_modules = cap;
    }
    in->modules[in->nmodules++] = module;
}

static const char *exported_stmt_name(stmt_ty s)
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

static void collect_module_exports(Interp *in, AstModule *module, asdl_seq *body)
{
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        stmt_ty s = asdl_seq_GET(body, i);
        if (s->kind == Export_kind) {
            const char *name = exported_stmt_name(s->v.Export.body);
            if (name)
                Module_AddExport(module, in, name);
        }
    }
}

static AstModule *load_module(Interp *in, asdl_seq *path)
{
    char *name = module_name_from_path(in, path);
    AstModule *module = module_cache_find(in, name);
    char file[1024];
    char *source;
    size_t size = 0;
    AsterError err;
    mod_ty mod;
    Signal sig;

    if (module)
        return module;
    if (!resolve_module_file(in, name, file, sizeof file)) {
        RT_Throw(in, "ModuleNotFoundError", "모듈 '%s'을(를) 찾을 수 없습니다", name);
        return NULL;
    }
    source = read_source_file(file, &size);
    if (!source) {
        RT_Throw(in, "ImportError", "모듈 '%s'을(를) 읽을 수 없습니다: %s", name, file);
        return NULL;
    }
    mod = Aster_ParseFile(source, size, in->arena, &err);
    if (!mod) {
        RT_Throw(in, "ImportError", "%s:%d:%d: %s", file, err.line, err.col + 1,
                 err.message);
        free(source);
        return NULL;
    }
    module = Module_New(in, name, Arena_Strndup(in->arena, file, strlen(file)));
    module->loading = 1;
    module_cache_add(in, module);
    predeclare(in, module->env, mod->v.Module.body);
    collect_module_exports(in, module, mod->v.Module.body);
    if (in->sig == SIG_OK) {
        free(source);
        sig = exec_block(in, mod->v.Module.body, module->env);
        if (sig == SIG_RETURN) {
            in->sig = SIG_OK;
            in->ret = NULL;
        }
    }
    else {
        free(source);
    }
    module->loading = 0;
    if (in->sig != SIG_OK)
        return NULL;
    return module;
}

static void import_public_names(Interp *in, Env *env, AstModule *module)
{
    size_t i;
    if (module->nexports > 0) {
        for (i = 0; i < module->nexports; i++) {
            RValue *value = Module_GetExported(in, module, module->exports[i]);
            if (value)
                Env_Define(in, env, module->exports[i], RValue_Copy(in, value));
        }
        return;
    }
    {
        Binding *b;
        for (b = module->env->vars; b; b = b->next) {
            if (b->name[0] != '_' && b->cell)
                Env_Define(in, env, b->name, RValue_Copy(in, b->cell));
        }
    }
}

static Signal exec_import(Interp *in, stmt_ty s, Env *env)
{
    AstModule *module = load_module(in, s->v.Import.path);
    const char *bind_name;

    if (!module)
        return in->sig;
    bind_name = s->v.Import.alias;
    if (!bind_name) {
        size_t n = asdl_seq_LEN(s->v.Import.path);
        bind_name = asdl_seq_GET(s->v.Import.path, n - 1);
    }
    Env_Define(in, env, bind_name, RValue_Module(in, module));
    if (s->v.Import.wildcard)
        import_public_names(in, env, module);
    return SIG_OK;
}

static Signal exec_import_from(Interp *in, stmt_ty s, Env *env)
{
    AstModule *module = load_module(in, s->v.ImportFrom.module);

    if (!module)
        return in->sig;
    if (s->v.ImportFrom.name) {
        const char *name = s->v.ImportFrom.name;
        RValue *value = Module_GetExported(in, module, name);
        if (!value) {
            RT_Throw(in, "ImportError", "모듈 '%s'에 '%s'이(가) 없습니다",
                     module->name, name);
            return SIG_THROW;
        }
        Env_Define(in, env, s->v.ImportFrom.alias ? s->v.ImportFrom.alias : name,
                   RValue_Copy(in, value));
    }
    else {
        import_public_names(in, env, module);
    }
    return SIG_OK;
}

static Signal exec_stmt(Interp *in, stmt_ty s, Env *env)
{
    in->line = s->lineno;

    switch (s->kind) {
    case FunctionDef_kind:
    case ClassDef_kind:
    case StructDef_kind:
    case InterfaceDef_kind:
    case ExceptionDef_kind:
    case EnumDef_kind:
        return exec_decl(in, s, env);
    case VarDecl_kind:
        return exec_var_decl(in, s, env);
    case Destructure_kind:
        return exec_destructure(in, s, env);
    case Assert_kind:
        return exec_assert(in, s, env);
    case Assign_kind:
        return exec_assign(in, s, env);
    case If_kind:
        return exec_if(in, s, env);
    case While_kind:
        return exec_while(in, s, env);
    case For_kind:
        return exec_for(in, s, env);
    case ForRange_kind:
        return exec_for_range(in, s, env);
    case ForIn_kind:
        return exec_for_in(in, s, env);
    case Try_kind:
        return exec_try(in, s, env);
    case Return_kind:
        return exec_return(in, s, env);
    case Yield_kind: {
        RValue *value = s->v.Yield.value ? eval(in, s->v.Yield.value, env) : null_value(in);
        if (in->sig != SIG_OK)
            return in->sig;
        if (!in->yield_buf) {
            RT_Throw(in, "RuntimeError", "yield는 제너레이터 함수 안에서만 쓸 수 있습니다");
            return SIG_THROW;
        }
        List_Append(in, in->yield_buf, value);
        return SIG_OK;
    }
    case Throw_kind:
        return exec_throw(in, s, env);
    case Break_kind:
        in->sig = SIG_BREAK;
        return SIG_BREAK;
    case Continue_kind:
        in->sig = SIG_CONTINUE;
        return SIG_CONTINUE;
    case Import_kind:
        return exec_import(in, s, env);
    case ImportFrom_kind:
        return exec_import_from(in, s, env);
    case Export_kind:
        return exec_stmt(in, s->v.Export.body, env);
    case ExprStmt_kind:
        eval(in, s->v.ExprStmt.value, env);
        return in->sig;
    }
    return SIG_OK;
}

/* ---- 식 ---- */

static RValue *eval_args(Interp *in, asdl_seq *args, Env *env, RValue ***out, size_t *nout)
{
    size_t n = asdl_seq_LEN(args);
    RValue **values = n ? Arena_Alloc(in->arena, n * sizeof(RValue *)) : NULL;
    size_t i;

    for (i = 0; i < n; i++) {
        values[i] = eval(in, asdl_seq_GET(args, i), env);
        if (in->sig != SIG_OK)
            return NULL;
    }
    *out = values;
    *nout = n;
    return RValue_Null(in);
}

static RValue *make_exception(Interp *in, RValue *value, const char *exc_type)
{
    if (exc_type) {
        AsterString *msg = RValue_ToString(in, value);
        return RValue_Exception(in, exc_type, RValue_Str(in, msg));
    }
    switch (value->kind) {
    case RV_EXC:
        return value;
    case RV_EXC_CLASS:
        return RValue_Exception(in, value->u.exc_name, RValue_Str(in, AsterString_FromCStr("")));
    case RV_STR:
        return RValue_Exception(in, "Exception", value);
    default:
        RT_Throw(in, "TypeError", "throw에는 문자열이나 예외 값이 필요합니다");
        return NULL;
    }
}

static RValue *eval_new(Interp *in, expr_ty e, Env *env)
{
    RValue **cell = Env_LookupCell(env, e->v.New.cls);
    RValue **args;
    size_t nargs;
    RValue *type_value;

    if (!cell || !*cell)
        return RT_ThrowValue(in, "NameError", "타입 '%s'을(를) 찾을 수 없습니다", e->v.New.cls);
    type_value = *cell;
    if (!eval_args(in, e->v.New.args, env, &args, &nargs))
        return NULL;

    if (type_value->kind == RV_CLASS)
        return instantiate_class(in, type_value->u.cls, args, nargs);
    if (type_value->kind == RV_STRUCTTYPE)
        return instantiate_struct(in, type_value->u.stype, args, nargs);
    return RT_ThrowValue(in, "TypeError", "'%s'은(는) new로 만들 수 있는 타입이 아닙니다",
                         e->v.New.cls);
}

/* ---- 컬렉션 ---- */

static int is_setlike(const RValue *v)
{
    return v && (v->kind == RV_SET || v->kind == RV_FROZENSET);
}

static int set_subset(const RValue *a, const RValue *b)
{
    size_t i;
    for (i = 0; i < a->u.set->len; i++) {
        if (!Set_Has(b->u.set, a->u.set->items[i]))
            return 0;
    }
    return 1;
}

static int eval_slice_bound(Interp *in, expr_ty e, Env *env, int *present, int64_t *out)
{
    RValue *v;

    if (!e) {
        *present = 0;
        return 1;
    }
    v = eval(in, e, env);
    if (in->sig != SIG_OK)
        return 0;
    if (!v || (v->kind != RV_INT && v->kind != RV_BOOL)) {
        RT_Throw(in, "TypeError", "슬라이스 인덱스는 정수여야 합니다");
        return 0;
    }
    *present = 1;
    *out = v->kind == RV_INT ? v->u.i : v->u.b;
    return 1;
}

static int64_t clamp_slice(int64_t v, int64_t len, int64_t step)
{
    if (v < 0) {
        if (v < -len)
            return step > 0 ? 0 : -1;
        return len + v;
    }
    if (v < len)
        return v;
    if (step > 0)
        return len;
    return len - 1;
}

/* slice_expr는 Slice_kind 노드다. */
static RValue *eval_slice(Interp *in, RValue *container, expr_ty slice_expr, Env *env)
{
    int has_lower, has_upper, has_step;
    int64_t lower = 0, upper = 0, step = 1;
    int64_t len;
    int64_t start, stop, i;
    RValue **items = NULL;
    size_t n = 0;

    if (!eval_slice_bound(in, slice_expr->v.Slice.step, env, &has_step, &step))
        return NULL;
    if (has_step && step == 0)
        return RT_ThrowValue(in, "ValueError", "slice step cannot be zero");
    if (!has_step)
        step = 1;
    if (!eval_slice_bound(in, slice_expr->v.Slice.lower, env, &has_lower, &lower))
        return NULL;
    if (!eval_slice_bound(in, slice_expr->v.Slice.upper, env, &has_upper, &upper))
        return NULL;

    if (container->kind == RV_LIST)
        len = (int64_t)container->u.list->len;
    else if (container->kind == RV_TUPLE)
        len = (int64_t)container->u.tuple->len;
    else if (container->kind == RV_STR)
        len = (int64_t)container->u.s->length;
    else
        return RT_ThrowValue(in, "TypeError", "'%s' 객체는 슬라이스를 지원하지 않습니다",
                             RValue_TypeName(container));

    if (has_lower)
        start = clamp_slice(lower, len, step);
    else
        start = step > 0 ? 0 : len - 1;
    if (has_upper)
        stop = clamp_slice(upper, len, step);
    else
        stop = step > 0 ? len : -1;

    if (container->kind == RV_STR) {
        char *buf = xmalloc_fallback(len + 1);
        for (i = start; step > 0 ? i < stop : i > stop; ) {
            buf[n++] = container->u.s->data[i];
            if (step > 0) {
                if (i > INT64_MAX - step)
                    break;
            }
            else {
                if (i < INT64_MIN - step)
                    break;
            }
            i += step;
        }
        buf[n] = 0;
        {
            AsterString *s = AsterString_FromUTF8(buf, n);
            free(buf);
            return RValue_Str(in, s);
        }
    }

    items = xmalloc_fallback((len > 0 ? (size_t)len : 1) * sizeof(RValue *));
    for (i = start; step > 0 ? i < stop : i > stop; ) {
        RValue *item = container->kind == RV_LIST
                           ? container->u.list->items[i]
                           : container->u.tuple->items[i];
        items[n++] = RValue_Copy(in, item);
        if (step > 0) {
            if (i > INT64_MAX - step)
                break;
        }
        else {
            if (i < INT64_MIN - step)
                break;
        }
        i += step;
    }
    if (container->kind == RV_LIST) {
        AsterList *list = List_New(in);
        size_t k;
        for (k = 0; k < n; k++)
            List_Append(in, list, items[k]);
        free(items);
        return RValue_List(in, list);
    }
    else {
        AsterTuple *tuple = Tuple_New(in, n);
        size_t k;
        for (k = 0; k < n; k++)
            tuple->items[k] = items[k];
        free(items);
        return RValue_Tuple(in, tuple);
    }
}

static RValue *eval(Interp *in, expr_ty e, Env *env)
{
    in->line = e->lineno;

    switch (e->kind) {
    case Constant_kind: {
        Value v = e->v.Constant.value;
        switch (v.tag) {
        case VT_NULL:
            return null_value(in);
        case VT_BOOL:
            return RValue_Bool(in, v.u.i != 0);
        case VT_INT:
            return RValue_Int(in, v.u.i);
        case VT_FLOAT:
            return RValue_Float(in, v.u.f);
        case VT_OBJ:
            if (v.u.o && v.u.o->kind == ASTER_OBJ_STRING)
                return RValue_Str(in, (AsterString *)v.u.o);
            return null_value(in);
        }
        return null_value(in);
    }
    case Name_kind: {
        RValue **cell = Env_LookupCell(env, e->v.Name.id);
        if (!cell) {
            RT_Throw(in, "NameError", "이름 '%s'이(가) 정의되지 않았습니다", e->v.Name.id);
            return NULL;
        }
        return *cell;
    }
    case Self_kind: {
        RValue **cell = Env_LookupCell(env, "self");
        if (!cell) {
            RT_Throw(in, "RuntimeError", "'self'는 메서드 안에서만 쓸 수 있습니다");
            return NULL;
        }
        return *cell;
    }
    case List_kind: {
        AsterList *list = List_New(in);
        asdl_seq *elements = e->v.List.elements;
        size_t i;
        for (i = 0; i < asdl_seq_LEN(elements); i++) {
            expr_ty item_expr = asdl_seq_GET(elements, i);
            if (item_expr->kind == Spread_kind) {
                RValue *inner = eval(in, item_expr->v.Spread.value, env);
                size_t k, n;
                int ok = 1;
                if (in->sig != SIG_OK)
                    return NULL;
                if (!RValue_Iterable(inner)) {
                    RT_Throw(in, "TypeError", "%s 값은 펼칠 수 없습니다",
                             RValue_TypeName(inner));
                    return NULL;
                }
                n = RValue_IterLen(in, inner, &ok);
                if (!ok)
                    return NULL;
                for (k = 0; k < n; k++) {
                    RValue *v = RValue_IterAt(in, inner, k, &ok);
                    if (!ok)
                        return NULL;
                    List_Append(in, list, RValue_Copy(in, v));
                }
            }
            else {
                RValue *item = eval(in, item_expr, env);
                if (in->sig != SIG_OK)
                    return NULL;
                List_Append(in, list, RValue_Copy(in, item));
            }
        }
        return RValue_List(in, list);
    }
    case Tuple_kind: {
        asdl_seq *elements = e->v.Tuple.elements;
        AsterTuple *tuple = Tuple_New(in, asdl_seq_LEN(elements));
        size_t i;
        for (i = 0; i < tuple->len; i++) {
            RValue *item = eval(in, asdl_seq_GET(elements, i), env);
            if (in->sig != SIG_OK)
                return NULL;
            tuple->items[i] = RValue_Copy(in, item);
        }
        return RValue_Tuple(in, tuple);
    }
    case Dict_kind: {
        AsterDict *dict = Dict_New(in);
        size_t n = asdl_seq_LEN(e->v.Dict.keys);
        size_t i;
        int ok;
        for (i = 0; i < n; i++) {
            expr_ty key_expr = asdl_seq_GET(e->v.Dict.keys, i);
            RValue *key;
            RValue *value;
            if (key_expr->kind == Spread_kind) {
                RValue *src = eval(in, key_expr->v.Spread.value, env);
                size_t k;
                if (in->sig != SIG_OK)
                    return NULL;
                if (!src || src->kind != RV_DICT) {
                    RT_Throw(in, "TypeError", "딕셔너리는 딕셔너리로만 펼칠 수 있습니다");
                    return NULL;
                }
                for (k = 0; k < src->u.dict->len; k++) {
                    if (!Dict_Set(in, dict, src->u.dict->entries[k].key,
                                  RValue_Copy(in, src->u.dict->entries[k].value), &ok))
                        return NULL;
                }
                continue;
            }
            key = eval(in, key_expr, env);
            if (in->sig != SIG_OK)
                return NULL;
            value = eval(in, asdl_seq_GET(e->v.Dict.values, i), env);
            if (in->sig != SIG_OK)
                return NULL;
            if (!Dict_Set(in, dict, key, RValue_Copy(in, value), &ok))
                return NULL;
        }
        return RValue_Dict(in, dict);
    }
    case Set_kind: {
        AsterSet *set = Set_New(in);
        asdl_seq *elements = e->v.Set.elements;
        size_t i;
        int ok;
        for (i = 0; i < asdl_seq_LEN(elements); i++) {
            expr_ty item_expr = asdl_seq_GET(elements, i);
            if (item_expr->kind == Spread_kind) {
                RValue *inner = eval(in, item_expr->v.Spread.value, env);
                size_t k, n;
                if (in->sig != SIG_OK)
                    return NULL;
                if (!RValue_Iterable(inner)) {
                    RT_Throw(in, "TypeError", "%s 값은 펼칠 수 없습니다",
                             RValue_TypeName(inner));
                    return NULL;
                }
                n = RValue_IterLen(in, inner, &ok);
                if (!ok)
                    return NULL;
                for (k = 0; k < n; k++) {
                    RValue *v = RValue_IterAt(in, inner, k, &ok);
                    if (!ok)
                        return NULL;
                    if (!Set_Add(in, set, RValue_Copy(in, v), &ok))
                        return NULL;
                }
            }
            else {
                RValue *item = eval(in, item_expr, env);
                if (in->sig != SIG_OK)
                    return NULL;
                if (!Set_Add(in, set, RValue_Copy(in, item), &ok))
                    return NULL;
            }
        }
        return RValue_Set(in, set);
    }
    case ListComp_kind:
    case SetComp_kind: {
        int is_set = e->kind == SetComp_kind;
        expr_ty elt = is_set ? e->v.SetComp.elt : e->v.ListComp.elt;
        identifier name = is_set ? e->v.SetComp.name : e->v.ListComp.name;
        expr_ty iter = is_set ? e->v.SetComp.iter : e->v.ListComp.iter;
        expr_ty cond = is_set ? e->v.SetComp.cond : e->v.ListComp.cond;
        RValue *source = eval(in, iter, env);
        Env *loop_env;
        AsterList *list = NULL;
        AsterSet *set = NULL;
        size_t len, i;
        int ok = 1;
        if (in->sig != SIG_OK)
            return NULL;
        if (!RValue_Iterable(source)) {
            RT_Throw(in, "TypeError", "%s 값은 순회할 수 없습니다", RValue_TypeName(source));
            return NULL;
        }
        len = RValue_IterLen(in, source, &ok);
        if (!ok)
            return NULL;
        loop_env = Env_New(in, env);
        if (is_set)
            set = Set_New(in);
        else
            list = List_New(in);
        Env_Define(in, loop_env, name, null_value(in));
        for (i = 0; i < len; i++) {
            RValue *item = RValue_IterAt(in, source, i, &ok);
            RValue *value;
            if (!ok)
                return NULL;
            Env_Assign(loop_env, name, RValue_Copy(in, item));
            if (cond) {
                RValue *keep = eval(in, cond, loop_env);
                if (in->sig != SIG_OK)
                    return NULL;
                if (!RValue_Truthy(keep))
                    continue;
            }
            value = eval(in, elt, loop_env);
            if (in->sig != SIG_OK)
                return NULL;
            if (is_set) {
                if (!Set_Add(in, set, RValue_Copy(in, value), &ok))
                    return NULL;
            }
            else {
                List_Append(in, list, RValue_Copy(in, value));
            }
        }
        return is_set ? RValue_Set(in, set) : RValue_List(in, list);
    }
    case DictComp_kind: {
        RValue *source = eval(in, e->v.DictComp.iter, env);
        Env *loop_env;
        AsterDict *dict;
        size_t len, i;
        int ok = 1;
        if (in->sig != SIG_OK)
            return NULL;
        if (!RValue_Iterable(source)) {
            RT_Throw(in, "TypeError", "%s 값은 순회할 수 없습니다", RValue_TypeName(source));
            return NULL;
        }
        len = RValue_IterLen(in, source, &ok);
        if (!ok)
            return NULL;
        loop_env = Env_New(in, env);
        dict = Dict_New(in);
        Env_Define(in, loop_env, e->v.DictComp.name, null_value(in));
        for (i = 0; i < len; i++) {
            RValue *item = RValue_IterAt(in, source, i, &ok);
            RValue *key, *value;
            if (!ok)
                return NULL;
            Env_Assign(loop_env, e->v.DictComp.name, RValue_Copy(in, item));
            if (e->v.DictComp.cond) {
                RValue *keep = eval(in, e->v.DictComp.cond, loop_env);
                if (in->sig != SIG_OK)
                    return NULL;
                if (!RValue_Truthy(keep))
                    continue;
            }
            key = eval(in, e->v.DictComp.key, loop_env);
            if (in->sig != SIG_OK)
                return NULL;
            value = eval(in, e->v.DictComp.value, loop_env);
            if (in->sig != SIG_OK)
                return NULL;
            if (!Dict_Set(in, dict, key, RValue_Copy(in, value), &ok))
                return NULL;
        }
        return RValue_Dict(in, dict);
    }
    case Spread_kind:
        return RT_ThrowValue(in, "SyntaxError", "이 위치에서는 펼치기를 쓸 수 없습니다");
    case Incr_kind: {
        Target tgt;
        RValue *current, *updated;
        int ok = 1;
        if (!resolve_target(in, e->v.Incr.target, env, &tgt))
            return NULL;
        if (tgt.prop)
            current = property_read(in, tgt.prop, tgt.self);
        else if (tgt.is_subscript)
            current = RValue_GetItem(in, tgt.container, tgt.index, &ok);
        else
            current = *tgt.slot;
        if (in->sig != SIG_OK)
            return NULL;
        if (!ok)
            return NULL;
        if (current->kind == RV_INT)
            updated = RValue_Int(in, current->u.i + (e->v.Incr.is_inc ? 1 : -1));
        else if (current->kind == RV_FLOAT)
            updated = RValue_Float(in, current->u.f + (e->v.Incr.is_inc ? 1.0 : -1.0));
        else
            return RT_ThrowValue(in, "TypeError", "%s 값은 증감할 수 없습니다",
                                 RValue_TypeName(current));
        if (tgt.prop)
            property_write(in, tgt.prop, tgt.self, updated);
        else if (tgt.is_subscript) {
            if (!RValue_SetItem(in, tgt.container, tgt.index, updated, &ok))
                return NULL;
        }
        else
            *tgt.slot = updated;
        return e->v.Incr.is_prefix ? updated : current;
    }
    case BoolOp_kind: {
        RValue *left = eval(in, e->v.BoolOp.left, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (e->v.BoolOp.op == And) {
            if (!RValue_Truthy(left))
                return left;
            return eval(in, e->v.BoolOp.right, env);
        }
        if (RValue_Truthy(left))
            return left;
        return eval(in, e->v.BoolOp.right, env);
    }
    case UnaryOp_kind: {
        RValue *operand = eval(in, e->v.UnaryOp.operand, env);
        int ok;
        if (in->sig != SIG_OK)
            return NULL;
        switch (e->v.UnaryOp.op) {
        case Not:
            return RValue_Bool(in, !RValue_Truthy(operand));
        case UAdd:
            if (operand->kind == RV_FLOAT)
                return RValue_Float(in, operand->u.f);
            return RValue_Int(in, RValue_AsInt(in, operand, &ok));
        case USub:
            if (operand->kind == RV_FLOAT)
                return RValue_Float(in, -operand->u.f);
            if (operand->kind == RV_INT || operand->kind == RV_BOOL)
                return RValue_Int(in, -RValue_AsInt(in, operand, &ok));
            RT_Throw(in, "TypeError", "단항 '-'에는 숫자가 필요합니다");
            return NULL;
        }
        return null_value(in);
    }
    case BinOp_kind: {
        RValue *left = eval(in, e->v.BinOp.left, env);
        RValue *right;
        if (in->sig != SIG_OK)
            return NULL;
        right = eval(in, e->v.BinOp.right, env);
        if (in->sig != SIG_OK)
            return NULL;
        return apply_binop(in, e->v.BinOp.op, left, right);
    }
    case Compare_kind: {
        RValue *left = eval(in, e->v.Compare.left, env);
        RValue *right;
        int cmp;
        if (in->sig != SIG_OK)
            return NULL;
        right = eval(in, e->v.Compare.right, env);
        if (in->sig != SIG_OK)
            return NULL;
        switch (e->v.Compare.op) {
        case Eq:
            return RValue_Bool(in, RValue_Equal(left, right));
        case NotEq:
            return RValue_Bool(in, !RValue_Equal(left, right));
        default:
            break;
        }
        if (is_setlike(left) && is_setlike(right)) {
            int eq = RValue_Equal(left, right);
            int sub = set_subset(left, right);
            int sup = set_subset(right, left);
            switch (e->v.Compare.op) {
            case Lt:
                return RValue_Bool(in, sub && !eq);
            case LtE:
                return RValue_Bool(in, sub);
            case Gt:
                return RValue_Bool(in, sup && !eq);
            case GtE:
                return RValue_Bool(in, sup);
            default:
                return RValue_Bool(in, 0);
            }
        }
        if (!RValue_Compare(in, left, right, &cmp))
            return NULL;
        switch (e->v.Compare.op) {
        case Lt:
            return RValue_Bool(in, cmp < 0);
        case LtE:
            return RValue_Bool(in, cmp <= 0);
        case Gt:
            return RValue_Bool(in, cmp > 0);
        case GtE:
            return RValue_Bool(in, cmp >= 0);
        default:
            return RValue_Bool(in, 0);
        }
    }
    case Is_kind: {
        RValue *value = eval(in, e->v.Is.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        return RValue_Bool(in, value_is_type(in, value, e->v.Is.ty));
    }
    case In_kind: {
        RValue *item = eval(in, e->v.In.item, env);
        RValue *container;
        int ok;
        int found;
        if (in->sig != SIG_OK)
            return NULL;
        container = eval(in, e->v.In.container, env);
        if (in->sig != SIG_OK)
            return NULL;
        found = RValue_Contains(in, container, item, &ok);
        if (!ok)
            return NULL;
        return RValue_Bool(in, found);
    }
    case Cast_kind: {
        RValue *value = eval(in, e->v.Cast.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        return do_cast(in, e->v.Cast.ty, value);
    }
    case AddressOf_kind: {
        Target target;
        if (!resolve_target(in, e->v.AddressOf.value, env, &target))
            return NULL;
        if (target.is_subscript) {
            RT_Throw(in, "TypeError", "컬렉션 원소의 주소는 구할 수 없습니다");
            return NULL;
        }
        if (target.prop) {
            RT_Throw(in, "TypeError", "프로퍼티의 주소는 구할 수 없습니다");
            return NULL;
        }
        return RValue_Ptr(in, target.slot, NULL);
    }
    case Deref_kind: {
        RValue *operand = eval(in, e->v.Deref.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (!operand || operand->kind == RV_NULL) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return NULL;
        }
        if (operand->kind == RV_OBJECT)
            return operand;
        if (operand->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "역참조할 수 없는 값입니다");
            return NULL;
        }
        if (!operand->u.ptr.slot || !*operand->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "null 포인터를 역참조했습니다");
            return NULL;
        }
        return RValue_Copy(in, *operand->u.ptr.slot);
    }
    case SizeOf_kind:
        return RValue_Int(in, (int64_t)Type_SizeOf(in, env, e->v.SizeOf.ty));
    case Call_kind: {
        RValue *callee;
        RValue **args;
        size_t nargs;
        if (e->v.Call.func->kind == OptionalAttribute_kind) {
            expr_ty attr = e->v.Call.func;
            RValue *base = eval(in, attr->v.OptionalAttribute.value, env);
            if (in->sig != SIG_OK)
                return NULL;
            if (!base || base->kind == RV_NULL)
                return null_value(in);
            if (base->kind == RV_PTR && (!base->u.ptr.slot || !*base->u.ptr.slot ||
                                         (*base->u.ptr.slot)->kind == RV_NULL))
                return null_value(in);
            callee = get_attr(in, base, attr->v.OptionalAttribute.attr);
        }
        else {
            callee = eval(in, e->v.Call.func, env);
        }
        if (in->sig != SIG_OK)
            return NULL;
        if (!eval_args(in, e->v.Call.args, env, &args, &nargs))
            return NULL;
        return call_value(in, callee, args, nargs);
    }
    case New_kind:
        return eval_new(in, e, env);
    case Attribute_kind: {
        RValue *base = eval(in, e->v.Attribute.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        return get_attr(in, base, e->v.Attribute.attr);
    }
    case OptionalAttribute_kind: {
        RValue *base = eval(in, e->v.OptionalAttribute.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (!base || base->kind == RV_NULL)
            return null_value(in);
        if (base->kind == RV_PTR && (!base->u.ptr.slot || !*base->u.ptr.slot ||
                                     (*base->u.ptr.slot)->kind == RV_NULL))
            return null_value(in);
        return get_attr(in, base, e->v.OptionalAttribute.attr);
    }
    case PtrAttribute_kind: {
        RValue *base = eval(in, e->v.PtrAttribute.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        return get_attr(in, base, e->v.PtrAttribute.attr);
    }
    case Pipe_kind: {
        RValue *value = eval(in, e->v.Pipe.value, env);
        RValue *func;
        RValue *args[1];
        if (in->sig != SIG_OK)
            return NULL;
        func = eval(in, e->v.Pipe.func, env);
        if (in->sig != SIG_OK)
            return NULL;
        args[0] = value;
        return call_value(in, func, args, 1);
    }
    case Coalesce_kind: {
        RValue *left = eval(in, e->v.Coalesce.left, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (left->kind == RV_NULL)
            return eval(in, e->v.Coalesce.right, env);
        return left;
    }
    case Lambda_kind: {
        AstFn *fn = make_fn(in, env, NULL, e->v.Lambda.params, 1, e->v.Lambda.returns,
                            NULL, e->v.Lambda.body);
        return RValue_Func(in, fn);
    }
    case Array_kind:
        return RT_ThrowValue(in, "TypeError", "배열 리터럴은 아직 지원하지 않습니다");
    case Subscript_kind: {
        RValue *value = eval(in, e->v.Subscript.value, env);
        RValue *index;
        int ok;
        if (in->sig != SIG_OK)
            return NULL;
        if (e->v.Subscript.index->kind == Slice_kind)
            return eval_slice(in, value, e->v.Subscript.index, env);
        index = eval(in, e->v.Subscript.index, env);
        if (in->sig != SIG_OK)
            return NULL;
        value = RValue_GetItem(in, value, index, &ok);
        if (!ok)
            return NULL;
        return value;
    }
    case Slice_kind:
        return RT_ThrowValue(in, "TypeError", "슬라이스는 대괄호 안에서만 쓸 수 있습니다");
    case FString_kind: {
        asdl_seq *values = e->v.FString.values;
        RValue *result = RValue_Str(in, AsterString_FromCStr(""));
        size_t i;
        for (i = 0; i < asdl_seq_LEN(values); i++) {
            RValue *part = eval(in, asdl_seq_GET(values, i), env);
            if (in->sig != SIG_OK)
                return NULL;
            result = apply_binop(in, Add, result, RValue_Str(in, RValue_ToString(in, part)));
            if (in->sig != SIG_OK)
                return NULL;
        }
        return result;
    }
    case FormattedValue_kind: {
        RValue *value = eval(in, e->v.FormattedValue.value, env);
        AsterString *text;
        expr_ty spec = e->v.FormattedValue.format_spec;
        if (in->sig != SIG_OK)
            return NULL;
        if (e->v.FormattedValue.conversion == 'r')
            text = RValue_Repr(in, value);
        else
            text = RValue_ToString(in, value);
        if (spec && spec->kind == Constant_kind &&
            spec->v.Constant.value.tag == VT_OBJ &&
            spec->v.Constant.value.u.o &&
            spec->v.Constant.value.u.o->kind == ASTER_OBJ_STRING &&
            ((AsterString *)spec->v.Constant.value.u.o)->length > 0) {
            AsterString *fs = (AsterString *)spec->v.Constant.value.u.o;
            char out[256];
            if (value && value->kind == RV_FLOAT &&
                (fs->data[fs->length - 1] == 'f')) {
                int prec = 6;
                const char *dot = memchr(fs->data, '.', fs->length);
                if (dot)
                    prec = atoi(dot + 1);
                if (prec < 0 || prec > 30)
                    prec = 6;
                snprintf(out, sizeof out, "%.*f", prec, value->u.f);
                text = AsterString_FromCStr(out);
            }
            else if (value && value->kind == RV_FLOAT &&
                     fs->data[fs->length - 1] == 'e') {
                snprintf(out, sizeof out, "%.*e", 6, value->u.f);
                text = AsterString_FromCStr(out);
            }
            else if (value && (value->kind == RV_INT || value->kind == RV_BOOL) &&
                     fs->data[fs->length - 1] == 'd') {
                snprintf(out, sizeof out, "%lld",
                         (long long)(value->kind == RV_BOOL ? value->u.b : value->u.i));
                text = AsterString_FromCStr(out);
            }
            else if (value && (value->kind == RV_INT || value->kind == RV_FLOAT ||
                               value->kind == RV_BOOL) &&
                     fs->data[fs->length - 1] == 'x') {
                snprintf(out, sizeof out, "%llx",
                         (unsigned long long)(value->kind == RV_FLOAT
                                                  ? (int64_t)value->u.f
                                                  : (value->kind == RV_BOOL ? value->u.b
                                                                            : value->u.i)));
                text = AsterString_FromCStr(out);
            }
        }
        return RValue_Str(in, text);
    }
    }
    return RT_ThrowValue(in, "RuntimeError", "알 수 없는 식 노드입니다");
}

/* ---- 진입점 ---- */

int Interp_RunModule(Interp *in, mod_ty mod)
{
    predeclare(in, in->globals, mod->v.Module.body);
    if (in->sig != SIG_OK)
        return 1;
    exec_block(in, mod->v.Module.body, in->globals);
    if (in->sig == SIG_RETURN) {
        in->sig = SIG_OK;
        in->ret = NULL;
        return 0;
    }
    if (in->sig == SIG_BREAK || in->sig == SIG_CONTINUE) {
        in->sig = SIG_OK;
        RT_Throw(in, "RuntimeError", "'break'와 'continue'는 반복문 안에서만 쓸 수 있습니다");
        return 1;
    }
    return in->sig == SIG_THROW ? 1 : 0;
}
