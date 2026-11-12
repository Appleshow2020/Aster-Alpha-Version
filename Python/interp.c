#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/interp.h"
#include "internal/parser.h"

#define ASTER_GEN_STACK (256 * 1024)

#if defined(_WIN32)
/* windows.h는 value.h의 VT_* 열거자와 충돌하므로 섬유 API만 직접 선언한다. */
typedef void (__stdcall *AsterFiberProc)(void *);
__declspec(dllimport) void *__stdcall ConvertThreadToFiber(void *param);
__declspec(dllimport) void *__stdcall CreateFiber(size_t stack_size,
                                                  AsterFiberProc proc, void *param);
__declspec(dllimport) void __stdcall SwitchToFiber(void *fiber);
__declspec(dllimport) void __stdcall DeleteFiber(void *fiber);
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
#else
#include <ucontext.h>
#include <dlfcn.h>
#endif

static void *xmalloc_fallback(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("out of memory\n", stderr);
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

static RValue *null_value(Interp *in) {
    return RValue_Null(in);
}

static const char *typeref_name(typeref_ty ty) {
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
    case UnionTy_kind:
        return "union";
    default:
        return "Exception";
    }
}

/* 본문에 yield가 있는지 훑는다. 중첩 함수/클래스의 yield는 세지 않는다. */
static int body_has_yield(asdl_seq *body);

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
        return stmt_has_yield(s->v.For.init) || stmt_has_yield(s->v.For.step) ||
               body_has_yield(s->v.For.body);
    case ForRange_kind:
        return body_has_yield(s->v.ForRange.body);
    case ForIn_kind:
        return body_has_yield(s->v.ForIn.body);
    case Switch_kind: {
        for (i = 0; i < asdl_seq_LEN(s->v.Switch.cases); i++) {
            match_case_ty mc = asdl_seq_GET(s->v.Switch.cases, i);
            if (body_has_yield(mc->body))
                return 1;
        }
        return 0;
    }
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

static int body_has_yield(asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (stmt_has_yield(asdl_seq_GET(body, i)))
            return 1;
    }
    return 0;
}

/*
 * 호출 환경이 함수 밖으로 새는지 훑는다. 새지 않으면 호출이 끝날 때 환경을
 * 풀에 되돌려 다음 호출이 재사용한다. 클로저(람다/중첩 정의), 제너레이터,
 * 지역 변수 주소(&x)는 환경을 붙잡으므로 재사용하면 안 된다.
 */
static int expr_escapes_env(expr_ty e);
static int stmt_escapes_env(stmt_ty s);

static int exprs_escape_env(asdl_seq *seq) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(seq); i++) {
        if (expr_escapes_env((expr_ty)asdl_seq_GET(seq, i)))
            return 1;
    }
    return 0;
}

static int initializer_escapes_env(initializer_ty init) {
    size_t i;
    if (!init)
        return 0;
    switch (init->kind) {
    case NoInit_kind:
        return 0;
    case ExprInit_kind:
        return expr_escapes_env(init->v.ExprInit.value);
    case PositionalInit_kind:
        return exprs_escape_env(init->v.PositionalInit.values);
    case DesignatedInit_kind:
        for (i = 0; i < asdl_seq_LEN(init->v.DesignatedInit.items); i++) {
            designation_ty d = asdl_seq_GET(init->v.DesignatedInit.items, i);
            if (expr_escapes_env(d->value))
                return 1;
        }
        return 0;
    }
    return 1;
}

static int pattern_escapes_env(match_pattern_ty p) {
    if (!p)
        return 0;
    switch (p->kind) {
    case PLiteral_kind:
        return expr_escapes_env(p->v.PLiteral.value);
    case PVariant_kind:
        return expr_escapes_env(p->v.PVariant.path) ||
               exprs_escape_env(p->v.PVariant.items);
    case PBind_kind:
        return 0;
    }
    return 1;
}

static int body_escapes_env(asdl_seq *body);

static int expr_escapes_env(expr_ty e) {
    size_t i;
    if (!e)
        return 0;
    switch (e->kind) {
    case Lambda_kind:
    case AddressOf_kind:
        return 1;
    case Pipe_kind:
        return expr_escapes_env(e->v.Pipe.value) || expr_escapes_env(e->v.Pipe.func);
    case Coalesce_kind:
        return expr_escapes_env(e->v.Coalesce.left) ||
               expr_escapes_env(e->v.Coalesce.right);
    case BoolOp_kind:
        return expr_escapes_env(e->v.BoolOp.left) || expr_escapes_env(e->v.BoolOp.right);
    case UnaryOp_kind:
        return expr_escapes_env(e->v.UnaryOp.operand);
    case BinOp_kind:
        return expr_escapes_env(e->v.BinOp.left) || expr_escapes_env(e->v.BinOp.right);
    case Compare_kind:
        return expr_escapes_env(e->v.Compare.left) ||
               expr_escapes_env(e->v.Compare.right);
    case Is_kind:
        return expr_escapes_env(e->v.Is.value);
    case In_kind:
        return expr_escapes_env(e->v.In.item) || expr_escapes_env(e->v.In.container);
    case Cast_kind:
        return expr_escapes_env(e->v.Cast.value);
    case Deref_kind:
        return expr_escapes_env(e->v.Deref.value);
    case SizeOf_kind:
        return 0;
    case Call_kind:
        return expr_escapes_env(e->v.Call.func) || exprs_escape_env(e->v.Call.args);
    case New_kind:
        return exprs_escape_env(e->v.New.args);
    case Attribute_kind:
        return expr_escapes_env(e->v.Attribute.value);
    case PtrAttribute_kind:
        return expr_escapes_env(e->v.PtrAttribute.value);
    case OptionalAttribute_kind:
        return expr_escapes_env(e->v.OptionalAttribute.value);
    case Name_kind:
    case Self_kind:
    case Constant_kind:
        return 0;
    case Array_kind:
        return exprs_escape_env(e->v.Array.elements);
    case Range_kind:
        return expr_escapes_env(e->v.Range.start) || expr_escapes_env(e->v.Range.stop) ||
               expr_escapes_env(e->v.Range.step);
    case Subscript_kind:
        return expr_escapes_env(e->v.Subscript.value) ||
               expr_escapes_env(e->v.Subscript.index);
    case Slice_kind:
        return expr_escapes_env(e->v.Slice.lower) ||
               expr_escapes_env(e->v.Slice.upper) ||
               expr_escapes_env(e->v.Slice.step);
    case FString_kind:
        return exprs_escape_env(e->v.FString.values);
    case FormattedValue_kind:
        return expr_escapes_env(e->v.FormattedValue.value) ||
               expr_escapes_env(e->v.FormattedValue.format_spec);
    case List_kind:
        return exprs_escape_env(e->v.List.elements);
    case Tuple_kind:
        return exprs_escape_env(e->v.Tuple.elements);
    case Dict_kind:
        if (exprs_escape_env(e->v.Dict.keys))
            return 1;
        for (i = 0; i < asdl_seq_LEN(e->v.Dict.values); i++) {
            if (expr_escapes_env((expr_ty)asdl_seq_GET(e->v.Dict.values, i)))
                return 1;
        }
        return 0;
    case Set_kind:
        return exprs_escape_env(e->v.Set.elements);
    case Spread_kind:
        return expr_escapes_env(e->v.Spread.value);
    case ListComp_kind:
        return expr_escapes_env(e->v.ListComp.elt) || expr_escapes_env(e->v.ListComp.iter) ||
               expr_escapes_env(e->v.ListComp.cond);
    case SetComp_kind:
        return expr_escapes_env(e->v.SetComp.elt) || expr_escapes_env(e->v.SetComp.iter) ||
               expr_escapes_env(e->v.SetComp.cond);
    case DictComp_kind:
        return expr_escapes_env(e->v.DictComp.key) ||
               expr_escapes_env(e->v.DictComp.value) ||
               expr_escapes_env(e->v.DictComp.iter) ||
               expr_escapes_env(e->v.DictComp.cond);
    case Incr_kind:
        return expr_escapes_env(e->v.Incr.target);
    }
    return 1;   /* 모르는 종류는 보수적으로 재사용하지 않는다 */
}

static int stmt_escapes_env(stmt_ty s) {
    size_t i;
    if (!s)
        return 0;
    switch (s->kind) {
    case FunctionDef_kind:
    case ClassDef_kind:
    case InterfaceDef_kind:
    case StructDef_kind:
    case EnumDef_kind:
    case ExceptionDef_kind:
    case Yield_kind:
        return 1;
    case VarDecl_kind:
        return initializer_escapes_env(s->v.VarDecl.init);
    case Destructure_kind:
        return expr_escapes_env(s->v.Destructure.value);
    case Assert_kind:
        return expr_escapes_env(s->v.Assert.test) ||
               expr_escapes_env(s->v.Assert.message);
    case Assign_kind:
        return expr_escapes_env(s->v.Assign.target) ||
               expr_escapes_env(s->v.Assign.value);
    case If_kind:
        return expr_escapes_env(s->v.If.test) ||
               body_escapes_env(s->v.If.body) || body_escapes_env(s->v.If.orelse);
    case While_kind:
        return expr_escapes_env(s->v.While.test) || body_escapes_env(s->v.While.body);
    case For_kind:
        return stmt_escapes_env(s->v.For.init) || expr_escapes_env(s->v.For.test) ||
               stmt_escapes_env(s->v.For.step) || body_escapes_env(s->v.For.body);
    case ForRange_kind:
        return expr_escapes_env(s->v.ForRange.iter->start) ||
               expr_escapes_env(s->v.ForRange.iter->stop) ||
               expr_escapes_env(s->v.ForRange.iter->step) ||
               body_escapes_env(s->v.ForRange.body);
    case ForIn_kind:
        return expr_escapes_env(s->v.ForIn.iter) || body_escapes_env(s->v.ForIn.body);
    case Switch_kind:
        if (expr_escapes_env(s->v.Switch.subject))
            return 1;
        for (i = 0; i < asdl_seq_LEN(s->v.Switch.cases); i++) {
            match_case_ty mc = asdl_seq_GET(s->v.Switch.cases, i);
            if (pattern_escapes_env(mc->pattern) || body_escapes_env(mc->body))
                return 1;
        }
        return 0;
    case Try_kind:
        if (body_escapes_env(s->v.Try.body) || body_escapes_env(s->v.Try.finalbody))
            return 1;
        for (i = 0; i < asdl_seq_LEN(s->v.Try.handlers); i++) {
            handler_ty h = asdl_seq_GET(s->v.Try.handlers, i);
            if (body_escapes_env(h->body))
                return 1;
        }
        return 0;
    case Return_kind:
        return expr_escapes_env(s->v.Return.value);
    case Throw_kind:
        return expr_escapes_env(s->v.Throw.value);
    case Break_kind:
    case Continue_kind:
    case Import_kind:
    case ImportFrom_kind:
        return 0;
    case Export_kind:
        return stmt_escapes_env(s->v.Export.body);
    case ExprStmt_kind:
        return expr_escapes_env(s->v.ExprStmt.value);
    }
    return 1;
}

static int body_escapes_env(asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        if (stmt_escapes_env((stmt_ty)asdl_seq_GET(body, i)))
            return 1;
    }
    return 0;
}

static AstFn *make_fn(Interp *in, Env *env, const char *name, asdl_seq *params,
                      int lambda_params, ret_ty ret, asdl_seq *body, expr_ty body_expr) {
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

static AstFn *bind_method(Interp *in, AstFn *fn, RValue *self) {
    AstFn *bound = Arena_Alloc(in->arena, sizeof(AstFn));
    *bound = *fn;
    bound->self = self;
    return bound;
}

/* 오버로드 묶음의 각 함수를 self에 묶는다. self가 NULL이면 정적 호출이다. */
static RValue *bind_overload(Interp *in, AstOverload *overload, RValue *self,
                             AstClass *owner) {
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

static void overload_append(Interp *in, AstOverload *overload, AstFn *fn) {
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

static int bind_params(Interp *in, AstFn *fn, Env *env, RValue **args, size_t nargs) {
    size_t nparams = asdl_seq_LEN(fn->params);
    size_t i;

    if (nparams != nargs) {
        RT_Throw(in, "TypeError", "'%s' takes %zu argument(s) but %zu were given",
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

/* 호출 환경이 새지 않으면 재사용해도 된다. 판정은 함수마다 한 번만 한다. */
static int fn_can_reuse_env(AstFn *fn) {
    if (fn->env_reuse == 0) {
        int escapes = fn->kind == FN_LAMBDA ? expr_escapes_env(fn->body_expr)
                                            : body_escapes_env(fn->body);
        fn->env_reuse = escapes ? -1 : 1;
    }
    return fn->env_reuse > 0;
}

/*
 * 함수 본문의 최상위 문장만 이 호출 환경에 바인딩을 만든다. 중첩 블록은
 * 자체 Env를 쓰므로 여기서 세지 않는다. 재사용 시 이 수만큼 슬롯을 준비하면
 * 호출마다 바인딩을 새로 할당하지 않는다.
 */
static int fn_env_bind_sites(AstFn *fn) {
    size_t i;
    int n = 0;
    if (fn->env_bind_sites)
        return fn->env_bind_sites - 1;
    for (i = 0; i < asdl_seq_LEN(fn->body); i++) {
        stmt_ty s = asdl_seq_GET(fn->body, i);
        if (!s)
            continue;
        switch (s->kind) {
        case VarDecl_kind:
            n += 1;
            break;
        case Destructure_kind:
            n += (int)asdl_seq_LEN(s->v.Destructure.names);
            break;
        case Import_kind:
        case ImportFrom_kind:
            n += 1;
            break;
        default:
            break;
        }
    }
    fn->env_bind_sites = n + 1;
    return n;
}

static RValue *call_fn(Interp *in, AstFn *fn, RValue **args, size_t nargs) {
    Env *env;
    RValue *result;
    size_t nspare;
    int reuse = 0;

    if (fn->kind == FN_BUILTIN)
        return fn->builtin.fn(in, fn->self ? fn->self : fn->builtin.self, args, nargs);
    if (in->sig != SIG_OK)
        return NULL;

    if (in->recursion_limit > 0 && in->depth >= in->recursion_limit) {
        RT_Throw(in, "RecursionError", "call depth exceeded (possible infinite recursion)");
        return NULL;
    }
    in->depth++;
    Interp_FramePush(in, fn->name ? fn->name : "lambda");

    nspare = asdl_seq_LEN(fn->params) + (fn->self ? 2 : 0);
    reuse = !fn->is_generator && fn_can_reuse_env(fn);
    if (reuse) {
        /* 지역 변수 바인딩까지 미리 잡아 두어야 호출마다 할당이 늘지 않는다. */
        nspare += (size_t)fn_env_bind_sites(fn) + 2;
        env = Env_AcquireCall(in, fn->env, nspare);
    }
    else
        env = Env_NewCall(in, fn->env, nspare);
    if (fn->self) {
        Env_Define(in, env, "self", fn->self);
        Env_Define(in, env, "super",
                   RValue_Super(in, fn->owner, fn->self));
    }
    if (!bind_params(in, fn, env, args, nargs)) {
        in->depth--;
        if (reuse)
            Env_ReleaseCall(in, env);
        return NULL;
    }

    if (fn->kind == FN_LAMBDA) {
        result = eval(in, fn->body_expr, env);
        in->depth--;
        if (reuse)
            Env_ReleaseCall(in, env);
        if (in->sig != SIG_THROW)
            Interp_FramePop(in);
        return result;
    }

    if (fn->is_generator) {
        RValue *g = RValue_Generator(in, fn, env, fn->self);
        in->depth--;
        Interp_FramePop(in);
        return g;
    }

    exec_block(in, fn->body, env);
    if (in->sig == SIG_RETURN) {
        RValue *ret = in->ret ? in->ret : null_value(in);
        in->sig = SIG_OK;
        in->ret = NULL;
        in->depth--;
        if (reuse)
            Env_ReleaseCall(in, env);
        Interp_FramePop(in);
        return ret;
    }
    if (in->sig == SIG_BREAK || in->sig == SIG_CONTINUE) {
        in->sig = SIG_OK;
        in->depth--;
        if (reuse)
            Env_ReleaseCall(in, env);
        Interp_FramePop(in);
        RT_Throw(in, "RuntimeError", "'break' and 'continue' are only allowed inside loops");
        return NULL;
    }
    if (in->sig == SIG_THROW) {
        /* 예외가 풀려 나가는 동안에는 프레임을 남겨 트레이스백에 쓴다. */
        in->depth--;
        if (reuse)
            Env_ReleaseCall(in, env);
        return NULL;
    }
    in->ret = NULL;
    in->depth--;
    if (reuse)
        Env_ReleaseCall(in, env);
    Interp_FramePop(in);
    return null_value(in);
}

static int fn_accepts(Interp *in, AstFn *fn, RValue **args, size_t nargs) {
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

static RValue *call_value(Interp *in, RValue *callee, RValue **args, size_t nargs) {
    if (!callee) {
        return RT_ThrowValue(in, "TypeError", "value is not callable");
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
        return RT_ThrowValue(in, "TypeError", "no definition of '%s' accepts %zu argument(s)",
                             ov->count ? ov->fns[0]->name : "overload", nargs);
    }
    case RV_SUPER: {
        AstClass *base = callee->u.super->owner ? callee->u.super->owner->base : NULL;
        AstFn *ctor = find_ctor(base);
        if (!ctor)
            return RT_ThrowValue(in, "TypeError", "base class has no constructor");
        call_fn(in, bind_method(in, ctor, callee->u.super->self), args, nargs);
        return in->sig == SIG_OK ? null_value(in) : NULL;
    }
    case RV_CLASS:
        return RT_ThrowValue(in, "TypeError", "use 'new' to create '%s'",
                             callee->u.cls->name);
    case RV_EXC_CLASS: {
        RValue *message = nargs > 0
            ? RValue_Str(in, RValue_ToString(in, args[0]))
            : RValue_Str(in, AsterString_FromCStr(""));
        return RValue_Exception(in, callee->u.exc_name, message);
    }
    case RV_VARIANT_CTOR: {
        AstEnumType *et = callee->u.vc.type;
        size_t index = callee->u.vc.index;
        AstEnumMember *m = &et->members[index];
        if (nargs != m->nparams) {
            return RT_ThrowValue(in, "TypeError",
                                 "variant '%s.%s' takes %zu argument(s) but %zu were given",
                                 et->name, m->name, m->nparams, nargs);
        }
        return RValue_EnumPayload(in, et, m->name, index, args, nargs);
    }
    default:
        return RT_ThrowValue(in, "TypeError", "value is not callable");
    }
}

RValue *Interp_CallValue(Interp *in, RValue *callee, RValue **args, size_t nargs) {
    return call_value(in, callee, args, nargs);
}

RValue *Interp_CallMethod(Interp *in, RValue *self, const char *name,
                          RValue **args, size_t nargs) {
    RValue *fn;
    if (!RValue_HasMethod(in, self, name))
        return RT_ThrowValue(in, "AttributeError", "'%s' object has no method '%s'",
                             RValue_TypeName(self), name);
    fn = get_attr(in, self, name);
    if (in->sig != SIG_OK)
        return NULL;
    return call_value(in, fn, args, nargs);
}

int RValue_HasMethod(Interp *in, const RValue *v, const char *name) {
    (void)in;
    if (!v)
        return 0;
    if (v->kind == RV_OBJECT)
        return Class_FindMethod(v->u.obj->cls, name) != NULL;
    return 0;
}

/* ---- 제너레이터 실행 컨텍스트 ---- */

static AstGenerator *g_current_gen;   /* 지금 실행 중인 제너레이터 (없으면 NULL) */

#if defined(_WIN32)
static void *g_main_fiber;

static void *gen_context_current(void) {
    if (g_current_gen && g_current_gen->ctx)
        return g_current_gen->ctx;
    if (!g_main_fiber)
        g_main_fiber = ConvertThreadToFiber(NULL);
    return g_main_fiber;
}

static void gen_context_switch(void *save, void *target) {
    (void)save;
    SwitchToFiber(target);
}

static void gen_context_release(AstGenerator *gen) {
    if (gen->ctx)
        DeleteFiber(gen->ctx);
    gen->ctx = NULL;
}
#else
static AstGenerator *g_starting_gen;  /* 새 컨텍스트가 시작할 제너레이터 */
static ucontext_t g_main_ctx;

static void *gen_context_current(void) {
    if (g_current_gen && g_current_gen->ctx)
        return g_current_gen->ctx;
    return &g_main_ctx;
}

static void gen_context_switch(void *save, void *target) {
    swapcontext((ucontext_t *)save, (ucontext_t *)target);
}

static void gen_context_release(AstGenerator *gen) {
    if (gen->ctx)
        free(gen->ctx);
    if (gen->stack)
        free(gen->stack);
    gen->ctx = NULL;
    gen->stack = NULL;
}
#endif

/* 제너레이터 본문을 끝까지 실행하고 호출자에게 영원히 제어를 넘긴다. */
static void gen_finish(AstGenerator *gen) {
    gen->done = 1;
    for (;;)
        gen_context_switch(gen->ctx, gen->caller_ctx);
}

static void gen_body_run(AstGenerator *gen) {
    Interp *gin = gen->interp;
    Signal sig;

    gin->sig = SIG_OK;
    gin->ret = NULL;
    gin->exc = NULL;
    gin->gen = gen;
    gin->depth = 0;
    sig = exec_block(gin, gen->fn->body, gen->body_env);
    if (sig == SIG_RETURN) {
        gin->sig = SIG_OK;
        gin->ret = NULL;
    }
    else if (sig == SIG_BREAK || sig == SIG_CONTINUE) {
        gin->sig = SIG_OK;
        RT_Throw(gin, "RuntimeError", "'break' and 'continue' are only allowed inside loops");
        gen->failed = 1;
    }
    else if (sig == SIG_THROW) {
        gen->failed = 1;
    }
    gen_finish(gen);
}

#if defined(_WIN32)
static void __stdcall gen_fiber_proc(void *param) {
    gen_body_run((AstGenerator *)param);
}
#else
static void gen_ucontext_proc(void) {
    gen_body_run(g_starting_gen);
}
#endif

RValue *Generator_Next(Interp *in, AstGenerator *gen, int *ended) {
    *ended = 0;
    if (!gen || gen->done) {
        *ended = 1;
        return NULL;
    }

    if (!gen->started) {
        gen->started = 1;
        gen->caller_gen = g_current_gen;
        gen->caller_ctx = gen_context_current();
#if defined(_WIN32)
        gen->ctx = CreateFiber(ASTER_GEN_STACK, gen_fiber_proc, gen);
        if (!gen->ctx) {
            RT_Throw(in, "RuntimeError", "cannot create a generator execution context");
            return NULL;
        }
#else
        {
            ucontext_t *uc = xmalloc_fallback(sizeof(ucontext_t));
            gen->stack = xmalloc_fallback(ASTER_GEN_STACK);
            gen->ctx = uc;
            if (getcontext(uc) != 0) {
                gen_context_release(gen);
                RT_Throw(in, "RuntimeError", "cannot create a generator execution context");
                return NULL;
            }
            uc->uc_stack.ss_sp = gen->stack;
            uc->uc_stack.ss_size = ASTER_GEN_STACK;
            uc->uc_link = NULL;
            g_starting_gen = gen;
            makecontext(uc, gen_ucontext_proc, 0);
        }
#endif
    }
    else {
        gen->caller_gen = g_current_gen;
        gen->caller_ctx = gen_context_current();
    }

    {
        void *save = gen_context_current();
        g_current_gen = gen;
        gen_context_switch(save, gen->ctx);
    }
    g_current_gen = gen->caller_gen;

    if (gen->failed) {
        Interp *gin = gen->interp;
        size_t i;
        /* 제너레이터 안쪽 프레임을 호출자 스택 뒤에 이어 붙인다. */
        for (i = 0; i < gin->nframes; i++)
            Interp_FrameAppend(in, gin->frames[i].name, gin->frames[i].line);
        in->sig = SIG_THROW;
        in->exc = gin->exc;
        in->line = gin->line;
        gin->sig = SIG_OK;
        gin->exc = NULL;
        gen_context_release(gen);
        *ended = 1;
        return NULL;
    }
    if (gen->done) {
        gen_context_release(gen);
        *ended = 1;
        return NULL;
    }
    return gen->yielded;
}

/* ---- 연산 ---- */

static int to_int_pair(Interp *in, RValue *l, RValue *r, int64_t *a, int64_t *b) {
    int ok;
    *a = RValue_AsInt(in, l, &ok);
    if (!ok)
        return 0;
    *b = RValue_AsInt(in, r, &ok);
    return ok;
}

static int64_t ipow(Interp *in, int64_t base, int64_t exp, int *ok) {
    int64_t result = 1;
    *ok = 1;
    while (exp > 0) {
        if (exp & 1) {
            if (__builtin_mul_overflow(result, base, &result)) {
                *ok = 0;
                return 0;
            }
        }
        exp >>= 1;
        if (exp) {
            if (__builtin_mul_overflow(base, base, &base)) {
                *ok = 0;
                return 0;
            }
        }
    }
    (void)in;
    return result;
}

static RValue *int_overflow(Interp *in) {
    RT_Throw(in, "OverflowError", "integer arithmetic overflowed int range");
    return NULL;
}

static RValue *int_add(Interp *in, int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r))
        return int_overflow(in);
    return RValue_Int(in, r);
}

static RValue *int_sub(Interp *in, int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r))
        return int_overflow(in);
    return RValue_Int(in, r);
}

static RValue *int_mul(Interp *in, int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r))
        return int_overflow(in);
    return RValue_Int(in, r);
}


static int repeat_count(Interp *in, RValue *v, int64_t *out) {
    if (v && v->kind == RV_INT) {
        *out = v->u.i;
        return 1;
    }
    if (v && v->kind == RV_BOOL) {
        *out = v->u.b;
        return 1;
    }
    RT_Throw(in, "TypeError", "sequence repeat count must be an integer");
    return 0;
}

static RValue *seq_repeat(Interp *in, RValue *seq, int64_t times) {
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
        return RT_ThrowValue(in, "OverflowError", "sequence is too large");
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

static const char *binop_hook_name(operator_ty op) {
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

static const char *binop_reflected_name(operator_ty op) {
    switch (op) {
    case Add: return "__radd__";
    case Sub: return "__rsub__";
    case Mult: return "__rmul__";
    case Div: return "__rdiv__";
    case FloorDiv: return "__rfloordiv__";
    case Mod: return "__rmod__";
    case Pow: return "__rpow__";
    }
    return NULL;
}

/* 객체 피연산자의 연산자 훅. 처리했으면 결과를, 아니면 NULL(신호 없음)을 준다. */
static int binop_hook(Interp *in, operator_ty op, RValue *left, RValue *right,
                      RValue **out) {
    const char *name = binop_hook_name(op);
    const char *reflected = binop_reflected_name(op);

    *out = NULL;
    if (!name)
        return 0;
    if (left && left->kind == RV_OBJECT && RValue_HasMethod(in, left, name)) {
        RValue *args[1];
        args[0] = right;
        *out = Interp_CallMethod(in, left, name, args, 1);
        return 1;
    }
    if (right && right->kind == RV_OBJECT && reflected &&
        RValue_HasMethod(in, right, reflected)) {
        RValue *args[1];
        args[0] = left;
        *out = Interp_CallMethod(in, right, reflected, args, 1);
        return 1;
    }
    return 0;
}

static RValue *apply_binop(Interp *in, operator_ty op, RValue *left, RValue *right) {
    int use_float = 0;
    int ok;
    RValue *hooked;
    int handled;

    handled = binop_hook(in, op, left, right, &hooked);
    if (handled)
        return hooked;
    if (in->sig != SIG_OK)
        return NULL;

    if (op == Add && (left->kind == RV_STR || right->kind == RV_STR)) {
        RValue *result = RValue_Str(in, RString_Concat(in, RValue_ToString(in, left),
                                                       RValue_ToString(in, right)));
        return in->sig == SIG_OK ? result : NULL;
    }

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
        return RT_ThrowValue(in, "TypeError", "arithmetic requires numbers");
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
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
            return RValue_Float(in, a / b);
        case FloorDiv:
            if (b == 0.0)
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
            return RValue_Float(in, __builtin_floor(a / b));
        case Mod: {
            double r;
            if (b == 0.0)
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
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
            return int_add(in, a, b);
        case Sub:
            return int_sub(in, a, b);
        case Mult:
            return int_mul(in, a, b);
        case Div:
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
            if (a == INT64_MIN && b == -1)
                return int_overflow(in);
            return RValue_Int(in, a / b);
        case FloorDiv: {
            int64_t q;
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
            if (a == INT64_MIN && b == -1)
                return int_overflow(in);
            q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
                q -= 1;
            return RValue_Int(in, q);
        }
        case Mod: {
            int64_t r;
            if (b == 0)
                return RT_ThrowValue(in, "ZeroDivisionError", "division by zero");
            if (a == INT64_MIN && b == -1)
                return RValue_Int(in, 0);
            r = a % b;
            if (r != 0 && ((r < 0) != (b < 0)))
                r += b;
            return RValue_Int(in, r);
        }
        case Pow: {
            int64_t r;
            int ok;
            if (b < 0)
                return RValue_Float(in, __builtin_pow((double)a, (double)b));
            r = ipow(in, a, b, &ok);
            if (!ok)
                return int_overflow(in);
            return RValue_Int(in, r);
        }
        }
        return null_value(in);
    }
}

static operator_ty assign_op_to_operator(assign_op_ty op) {
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

static RValue *get_attr(Interp *in, RValue *base, const char *name) {
    if (!base || base->kind == RV_NULL)
        return RT_ThrowValue(in, "NullPointerError", "accessed '%s' on a null value", name);

    switch (base->kind) {
    case RV_OBJECT: {
        AstInstance *obj = base->u.obj;
        PropDesc *prop = Class_FindProperty(obj->cls, name);
        InstField *field;
        MethodDesc *method;

        if (prop) {
            if (!prop->getter)
                return RT_ThrowValue(in, "AttributeError", "property '%s' has no getter", name);
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
        return RT_ThrowValue(in, "AttributeError", "'%s' has no attribute '%s'",
                             obj->cls->name, name);
    }
    case RV_PTR: {
        RValue *inner = base->u.ptr.slot ? *base->u.ptr.slot : NULL;
        if (!inner || inner->kind == RV_NULL)
            return RT_ThrowValue(in, "NullPointerError", "accessed '%s' through a null pointer", name);
        return get_attr(in, inner, name);
    }
    case RV_STRUCT: {
        int index = StructValue_FieldIndex(base->u.stv->type, name);
        if (index < 0)
            return RT_ThrowValue(in, "AttributeError", "struct '%s' has no field '%s'",
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
        return RT_ThrowValue(in, "AttributeError", "'%s' object has no attribute '%s'",
                             RValue_TypeName(base), name);
    }
    case RV_STR: {
        AstBuiltin *method = String_Method(in, base, name);
        if (method)
            return RValue_Builtin(in, method);
        return RT_ThrowValue(in, "AttributeError", "str has no attribute '%s'", name);
    }
    case RV_MODULE: {
        RValue *exported = Module_GetExported(in, base->u.module, name);
        if (exported)
            return exported;
        return RT_ThrowValue(in, "AttributeError", "module '%s' has no member '%s'",
                             base->u.module->name, name);
    }
    case RV_SUPER: {
        AstClass *owner = base->u.super->owner;
        AstClass *base_cls = owner ? owner->base : NULL;
        MethodDesc *method = base_cls ? Class_FindMethod(base_cls, name) : NULL;
        AstFn *bound;
        if (!method)
            return RT_ThrowValue(in, "AttributeError", "base class has no method '%s'",
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
        return RT_ThrowValue(in, "AttributeError", "class '%s' has no static member '%s'",
                             base->u.cls->name, name);
    }
    case RV_ENUM:
        if (!strcmp(name, "name"))
            return RValue_Str(in, AsterString_FromCStr(base->u.en.name));
        if (!strcmp(name, "ordinal"))
            return RValue_Int(in, (int64_t)base->u.en.index);
        if (!strcmp(name, "variant"))
            return RValue_Str(in, AsterString_FromCStr(base->u.en.name));
        if (!strcmp(name, "arity"))
            return RValue_Int(in, (int64_t)base->u.en.npayload);
        if (!strcmp(name, "payload")) {
            AsterList *list = List_New(in);
            size_t i;
            for (i = 0; i < base->u.en.npayload; i++)
                List_Append(in, list, RValue_Copy(in, base->u.en.payload[i]));
            return RValue_List(in, list);
        }
        if (!strcmp(name, "value")) {
            AstEnumMember *m = &base->u.en.type->members[base->u.en.index];
            return m->value ? m->value : RValue_Int(in, (int64_t)base->u.en.index);
        }
        return RT_ThrowValue(in, "AttributeError", "enum value has no attribute '%s'", name);
    case RV_ENUMTYPE: {
        size_t i;
        for (i = 0; i < base->u.etype->nmembers; i++) {
            if (strcmp(base->u.etype->members[i].name, name))
                continue;
            if (base->u.etype->members[i].nparams > 0)
                return RValue_EnumCtor(in, base->u.etype, i);
            return RValue_Enum(in, base->u.etype, name, i);
        }
        return RT_ThrowValue(in, "AttributeError", "enum '%s' has no member '%s'",
                             base->u.etype->name, name);
    }
    case RV_EXC:
        if (!strcmp(name, "message"))
            return base->u.exc->message;
        if (!strcmp(name, "name"))
            return RValue_Str(in, AsterString_FromCStr(base->u.exc->name));
        return RT_ThrowValue(in, "AttributeError", "exception value has no attribute '%s'", name);
    default:
        return RT_ThrowValue(in, "AttributeError", "this value has no attribute '%s'", name);
    }
}

/* ---- 타입 검사/캐스트 ---- */

static int class_name_matches(AstClass *cls, const char *name) {
    for (; cls; cls = cls->base) {
        size_t i;
        if (!strcmp(cls->name, name))
            return 1;
        for (i = 0; i < cls->ninterfaces; i++)
            if (!strcmp(cls->interfaces[i], name))
                return 1;
    }
    return 0;
}

static int value_is_type(Interp *in, RValue *v, typeref_ty ty) {
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
    case FunctionTy_kind:
        return v->kind == RV_FUNC || v->kind == RV_BUILTIN || v->kind == RV_OVERLOAD ||
               v->kind == RV_VARIANT_CTOR;
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
    case UnionTy_kind: {
        size_t i;
        for (i = 0; i < (size_t)asdl_seq_LEN(ty->v.UnionTy.members); i++)
            if (value_is_type(in, v, asdl_seq_GET(ty->v.UnionTy.members, i)))
                return 1;
        return 0;
    }
    }
    return 0;
}

static RValue *do_cast(Interp *in, typeref_ty ty, RValue *value) {
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
    case FunctionTy_kind:
    case UnionTy_kind:
        return value;
    }
    return value;
}

/* ---- 초기화 ---- */

static RValue *eval_initializer(Interp *in, Env *env, typeref_ty ty, initializer_ty init) {
    if (!init || init->kind == NoInit_kind)
        return DefaultForType(in, env, ty);
    if (init->kind == ExprInit_kind)
        return eval(in, init->v.ExprInit.value, env);
    if (init->kind == PositionalInit_kind || init->kind == DesignatedInit_kind) {
        AstStructType *type = struct_type_from_typeref(in, env, ty);
        RValue *base;
        size_t i;

        if (!type)
            return RT_ThrowValue(in, "TypeError", "struct initializers can only be used with struct types");
        base = DefaultForType(in, env, ty);
        if (!base || base->kind != RV_STRUCT)
            return base;
        if (init->kind == PositionalInit_kind) {
            asdl_seq *values = init->v.PositionalInit.values;
            size_t n = asdl_seq_LEN(values);
            if (n > type->nfields)
                return RT_ThrowValue(in, "TypeError", "too many initializers for '%s'", type->tag);
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
                    return RT_ThrowValue(in, "TypeError", "'%s' has no field '%s'",
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

static AstStructType *struct_type_from_typeref(Interp *in, Env *env, typeref_ty ty) {
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

static void resolve_bases(Interp *in, AstClass *cls) {
    if (!cls->base_name || cls->base)
        return;
    {
        RValue **cell = Env_LookupCell(cls->env, cls->base_name);
        if (!cell || !*cell || (*cell)->kind != RV_CLASS) {
            RT_Throw(in, "TypeError", "cannot find base class '%s'", cls->base_name);
            return;
        }
        cls->base = (*cell)->u.cls;
        resolve_bases(in, cls->base);
    }
}

static void init_instance_fields(Interp *in, AstClass *cls, AstInstance *obj, Env *self_env) {
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

static AstFn *find_ctor(AstClass *cls) {
    for (; cls; cls = cls->base) {
        if (cls->ctor)
            return cls->ctor;
    }
    return NULL;
}

static RValue *instantiate_class(Interp *in, AstClass *cls, RValue **args, size_t nargs) {
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
        return RT_ThrowValue(in, "TypeError", "'%s' has no constructor", cls->name);
    }
    return self;
}

static StructCtorDesc *find_struct_ctor(AstStructType *type, size_t nargs) {
    size_t i;
    for (i = 0; i < type->nctors; i++) {
        if (asdl_seq_LEN(type->ctors[i].params) == nargs)
            return &type->ctors[i];
    }
    return NULL;
}

static int bind_struct_params(Interp *in, asdl_seq *params, Env *env, RValue **args, size_t nargs) {
    size_t i;
    for (i = 0; i < nargs; i++) {
        param_ty p = asdl_seq_GET(params, i);
        Env_Define(in, env, p->name, RValue_Copy(in, args[i]));
    }
    return 1;
}

static RValue *instantiate_struct(Interp *in, AstStructType *type, RValue **args, size_t nargs) {
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
        return RT_ThrowValue(in, "TypeError", "no constructor of '%s' matches the arguments", type->tag);
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
                return RT_ThrowValue(in, "TypeError", "'%s' has no field '%s'",
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

static PropDesc *class_property_slot(AstClass *cls, const char *name) {
    size_t k;
    for (k = 0; k < cls->nprops; k++) {
        if (!strcmp(cls->props[k].name, name))
            return &cls->props[k];
    }
    return NULL;
}

static MethodDesc *find_method_slot(MethodDesc *methods, size_t count, const char *name) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (!strcmp(methods[i].name, name))
            return &methods[i];
    }
    return NULL;
}

static AstClass *build_class(Interp *in, stmt_ty s, Env *env) {
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
    {
        asdl_seq *ifaces = s->v.ClassDef.interfaces;
        size_t ni = asdl_seq_LEN(ifaces);
        if (ni) {
            cls->interfaces = Arena_Alloc(in->arena, ni * sizeof(const char *));
            cls->ninterfaces = ni;
            for (i = 0; i < ni; i++) {
                typeref_ty t = asdl_seq_GET(ifaces, i);
                cls->interfaces[i] = t ? typeref_name(t) : "";
            }
        }
    }

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

static AstStructType *build_struct(Interp *in, stmt_ty s, Env *env) {
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

/* ---- FFI ---- */

#if defined(_WIN32)
static void *native_symbol(const char *name) {
    static void *modules[4];
    static int loaded;
    void *sym;
    int i;

    if (!loaded) {
        loaded = 1;
        modules[0] = GetModuleHandleA(NULL);
        modules[1] = LoadLibraryA("msvcrt.dll");
        modules[2] = LoadLibraryA("ucrtbase.dll");
        modules[3] = LoadLibraryA("kernel32.dll");
    }
    for (i = 0; i < 4; i++) {
        if (modules[i] && (sym = GetProcAddress(modules[i], name)))
            return sym;
    }
    return NULL;
}
#else
static void *native_symbol(const char *name) {
    return dlsym(RTLD_DEFAULT, name);
}
#endif

typedef union {
    int64_t i;
    double d;
} FfiArg;

void *Interp_NativeSymbol(const char *name) {
    return native_symbol(name);
}

static typeref_ty ffi_return_type(ret_ty ret) {
    if (ret && ret->kind == ValueRet_kind)
        return ret->v.ValueRet.ty;
    return NULL;
}

static int ffi_is_float(typeref_ty ty) {
    return ty && ty->kind == Primitive_kind && ty->v.Primitive.kind == FloatT;
}

static RValue *ffi_result(Interp *in, ret_ty ret, int64_t ir, double dr) {
    typeref_ty ty = ffi_return_type(ret);

    if (ffi_is_float(ty))
        return RValue_Float(in, dr);
    if (ty && ty->kind == Primitive_kind && ty->v.Primitive.kind == StrT) {
        const char *p = (const char *)(intptr_t)ir;
        return p ? RValue_Str(in, AsterString_FromCStr(p)) : RValue_Null(in);
    }
    if (ty && ty->kind == Primitive_kind && ty->v.Primitive.kind == BoolT)
        return RValue_Bool(in, ir != 0);
    if (!ty || (ty->kind == Primitive_kind && ty->v.Primitive.kind == NullT))
        return RValue_Null(in);
    return RValue_Int(in, ir);
}

/* all_double이면 모든 인자를 double로, 아니면 int64로 넘긴다.
   부동소수/정수가 섞인 시그니처는 지원하지 않는다 (호출부에서 거른다). */
static RValue *ffi_invoke(Interp *in, AstFn *decl, void *sym, FfiArg *a, int all_double) {
    int ret_double = ffi_is_float(ffi_return_type(decl->ret));
    size_t n = asdl_seq_LEN(decl->params);
    int64_t ir = 0;
    double dr = 0;

    switch (n) {
    case 0:
        if (ret_double)
            dr = ((double (*)(void))sym)();
        else
            ir = ((int64_t (*)(void))sym)();
        break;
    case 1:
        if (all_double) {
            if (ret_double)
                dr = ((double (*)(double))sym)(a[0].d);
            else
                ir = ((int64_t (*)(double))sym)(a[0].d);
        }
        else {
            if (ret_double)
                dr = ((double (*)(int64_t))sym)(a[0].i);
            else
                ir = ((int64_t (*)(int64_t))sym)(a[0].i);
        }
        break;
    case 2:
        if (all_double) {
            if (ret_double)
                dr = ((double (*)(double, double))sym)(a[0].d, a[1].d);
            else
                ir = ((int64_t (*)(double, double))sym)(a[0].d, a[1].d);
        }
        else {
            if (ret_double)
                dr = ((double (*)(int64_t, int64_t))sym)(a[0].i, a[1].i);
            else
                ir = ((int64_t (*)(int64_t, int64_t))sym)(a[0].i, a[1].i);
        }
        break;
    case 3:
        if (all_double) {
            if (ret_double)
                dr = ((double (*)(double, double, double))sym)(a[0].d, a[1].d, a[2].d);
            else
                ir = ((int64_t (*)(double, double, double))sym)(a[0].d, a[1].d, a[2].d);
        }
        else {
            if (ret_double)
                dr = ((double (*)(int64_t, int64_t, int64_t))sym)(a[0].i, a[1].i, a[2].i);
            else
                ir = ((int64_t (*)(int64_t, int64_t, int64_t))sym)(a[0].i, a[1].i, a[2].i);
        }
        break;
    case 4:
        if (all_double) {
            if (ret_double)
                dr = ((double (*)(double, double, double, double))sym)(a[0].d, a[1].d,
                                                                      a[2].d, a[3].d);
            else
                ir = ((int64_t (*)(double, double, double, double))sym)(a[0].d, a[1].d,
                                                                        a[2].d, a[3].d);
        }
        else {
            if (ret_double)
                dr = ((double (*)(int64_t, int64_t, int64_t, int64_t))sym)(
                    a[0].i, a[1].i, a[2].i, a[3].i);
            else
                ir = ((int64_t (*)(int64_t, int64_t, int64_t, int64_t))sym)(
                    a[0].i, a[1].i, a[2].i, a[3].i);
        }
        break;
    default:
        return RT_ThrowValue(in, "NotImplementedError",
                             "extern function '%s' supports at most 4 arguments",
                             decl->name);
    }
    return ffi_result(in, decl->ret, ir, dr);
}

/* extern 선언에 구현이 있는지: 내장 함수나 네이티브 심볼 중 하나. */
static RValue *bi_extern_call(Interp *in, RValue *self, RValue **args, size_t nargs);

static int extern_has_impl(Env *env, const char *name) {
    RValue **cell = Env_LookupCell(env, name);
    if (cell && *cell && (*cell)->kind == RV_BUILTIN &&
        (*cell)->u.builtin->fn != bi_extern_call)
        return 1;
    return native_symbol(name) != NULL;
}

static RValue *bi_extern_call(Interp *in, RValue *self, RValue **args, size_t nargs) {
    AstFn *decl = NULL;
    const char *name;
    FfiArg *argv;
    size_t i, n;
    size_t ndouble = 0;
    int ok = 1;
    void *sym;
    RValue **cell;

    if (self && self->kind == RV_FUNC && self->u.fn)
        decl = self->u.fn;
    name = decl ? decl->name : "?";

    /* extern 선언은 C 심볼 바인딩이 우선이다. 같은 이름의 전역 내장 함수가
       있더라도 네이티브 심볼이 존재하면 그쪽으로 호출한다. */
    sym = native_symbol(name);
    if (!sym) {
        cell = Env_LookupCell(in->globals, name);
        if (cell && *cell && (*cell)->kind == RV_BUILTIN &&
            (*cell)->u.builtin->fn != bi_extern_call)
            return (*cell)->u.builtin->fn(in, (*cell)->u.builtin->self, args, nargs);
    }

    if (!decl)
        return RT_ThrowValue(in, "NotImplementedError",
                             "extern function '%s' has no native implementation", name);
    n = asdl_seq_LEN(decl->params);
    if (n != nargs)
        return RT_ThrowValue(in, "TypeError", "extern function '%s' requires %zu argument(s)",
                             name, n);
    argv = n ? Arena_Alloc(in->arena, n * sizeof(FfiArg)) : NULL;
    for (i = 0; i < n; i++) {
        param_ty p = asdl_seq_GET(decl->params, i);
        if (ffi_is_float(p->ty)) {
            argv[i].d = RValue_AsDouble(in, args[i], &ok);
            ndouble++;
        }
        else if (p->ty && p->ty->kind == Primitive_kind &&
                 p->ty->v.Primitive.kind == StrT) {
            if (args[i]->kind != RV_STR) {
                return RT_ThrowValue(in, "TypeError",
                                     "argument %zu of extern function '%s' must be str",
                                     name, i + 1);
            }
            argv[i].i = (int64_t)(intptr_t)args[i]->u.s->data;
        }
        else {
            argv[i].i = RValue_AsInt(in, args[i], &ok);
        }
        if (!ok)
            return NULL;
    }
    if (ndouble != 0 && ndouble != n)
        return RT_ThrowValue(in, "NotImplementedError",
                             "extern function '%s': cannot mix float and non-float arguments",
                             name);
    if (!sym)
        return RT_ThrowValue(in, "NotImplementedError",
                             "extern function '%s' has no native implementation", name);
    return ffi_invoke(in, decl, sym, argv, ndouble == n);
}

static Signal exec_decl(Interp *in, stmt_ty s, Env *env) {
    switch (s->kind) {
    case FunctionDef_kind:
        if (!Env_HasLocal(env, s->v.FunctionDef.name)) {
            if (s->v.FunctionDef.is_extern) {
                AstFn *decl = make_fn(in, env, s->v.FunctionDef.name,
                                      s->v.FunctionDef.params, 0,
                                      s->v.FunctionDef.returns, NULL, NULL);
                if (!extern_has_impl(env, decl->name)) {
                    RT_Throw(in, "NotImplementedError",
                             "extern function '%s' has no native implementation",
                             decl->name);
                    return SIG_THROW;
                }
                decl->kind = FN_BUILTIN;
                decl->builtin.name = decl->name;
                decl->builtin.fn = bi_extern_call;
                decl->builtin.self = RValue_Func(in, decl);
                Env_Define(in, env, decl->name, RValue_Func(in, decl));
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
                et->members[i].params = em->params;
                et->members[i].nparams = asdl_seq_LEN(em->params);
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

static void predeclare_one(Interp *in, Env *env, stmt_ty s) {
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

static void predeclare(Interp *in, Env *env, asdl_seq *body) {
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

static Signal exec_block(Interp *in, asdl_seq *body, Env *env) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++) {
        Signal sig = exec_stmt(in, asdl_seq_GET(body, i), env);
        if (sig != SIG_OK)
            return sig;
    }
    return SIG_OK;
}

static int declared_type_known(typeref_ty ty) {
    if (!ty)
        return 0;
    switch (ty->kind) {
    case Primitive_kind:
    case Pointer_kind:
    case Nullable_kind:
    case StructRef_kind:
    case FunctionTy_kind:
        return 1;
    case Named_kind: {
        const char *name = ty->v.Named.name;
        return strcmp(name, "var") && strcmp(name, "any") && strcmp(name, "list") &&
               strcmp(name, "dict") && strcmp(name, "set") && strcmp(name, "tuple") &&
               strcmp(name, "frozenset") && strcmp(name, "map");
    }
    case UnionTy_kind: {
        size_t i;
        for (i = 0; i < (size_t)asdl_seq_LEN(ty->v.UnionTy.members); i++)
            if (!declared_type_known(asdl_seq_GET(ty->v.UnionTy.members, i)))
                return 0;
        return 1;
    }
    }
    return 0;
}

static int check_declared_value(Interp *in, typeref_ty ty, RValue *value) {
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
        RT_Throw(in, "TypeError", "cannot assign %s to a variable of type %s",
                 typeref_name(ty), RValue_TypeName(value));
        return 0;
    }
    return 1;
}

static Signal exec_var_decl(Interp *in, stmt_ty s, Env *env) {
    initializer_ty init = s->v.VarDecl.init;
    /* 람다 초기화에서 선언 타입은 람다의 반환 타입 표기다. 값은 람다 자체다. */
    int lambda_init = init && init->kind == ExprInit_kind &&
                      init->v.ExprInit.value->kind == Lambda_kind;
    RValue *value = eval_initializer(in, env, s->v.VarDecl.ty, init);
    if (in->sig != SIG_OK)
        return in->sig;
    if (!lambda_init && !check_declared_value(in, s->v.VarDecl.ty, value))
        return in->sig;
    if (s->v.VarDecl.is_const)
        Env_DefineConst(in, env, s->v.VarDecl.name, RValue_Copy(in, value));
    else
        Env_Define(in, env, s->v.VarDecl.name, RValue_Copy(in, value));
    return SIG_OK;
}

static Signal exec_destructure(Interp *in, stmt_ty s, Env *env) {
    RValue *value = eval(in, s->v.Destructure.value, env);
    asdl_seq *names = s->v.Destructure.names;
    size_t n = asdl_seq_LEN(names);
    RValue **items = n ? Arena_Alloc(in->arena, n * sizeof(RValue *)) : NULL;
    IterCursor cur;
    size_t got = 0, i;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!Iter_Begin(in, value, &cur))
        return in->sig;
    for (;;) {
        int ended = 0, ok = 1;
        RValue *item = Iter_Next(in, &cur, &ended, &ok);
        if (!ok)
            return in->sig;
        if (ended)
            break;
        if (got < n)
            items[got] = item;
        got++;
    }
    if (got != n) {
        RT_Throw(in, "ValueError", "cannot unpack %zu values into %zu names", got, n);
        return SIG_THROW;
    }
    for (i = 0; i < n; i++)
        Env_Define(in, env, (identifier)asdl_seq_GET(names, i), RValue_Copy(in, items[i]));
    return SIG_OK;
}

static Signal exec_assert(Interp *in, stmt_ty s, Env *env) {
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
        RT_Throw(in, "AssertionError", "assertion failed");
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
static int attr_target(Interp *in, RValue *base, const char *name, Target *out) {
    out->slot = NULL;
    out->prop = NULL;
    out->self = NULL;
    out->container = NULL;
    out->index = NULL;
    out->is_subscript = 0;

    if (base && base->kind == RV_PTR)
        base = base->u.ptr.slot ? *base->u.ptr.slot : NULL;
    if (!base || base->kind == RV_NULL) {
        RT_Throw(in, "NullPointerError", "accessed '%s' on a null value", name);
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
        RT_Throw(in, "AttributeError", "class '%s' has no static field '%s'",
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
            RT_Throw(in, "AttributeError", "'%s' has no field '%s'",
                     base->u.obj->cls->name, name);
            return 0;
        }
        out->slot = &field->cell;
        return 1;
    }
    if (base->kind == RV_STRUCT) {
        int index = StructValue_FieldIndex(base->u.stv->type, name);
        if (index < 0) {
            RT_Throw(in, "AttributeError", "struct '%s' has no field '%s'",
                     base->u.stv->type->tag, name);
            return 0;
        }
        out->slot = &base->u.stv->cells[index];
        return 1;
    }
    RT_Throw(in, "AttributeError", "cannot access '%s' on this value", name);
    return 0;
}

/* 변수/필드/포인터 대상/프로퍼티를 복사 없이 평가한다. */
static RValue *eval_ref(Interp *in, expr_ty e, Env *env) {
    switch (e->kind) {
    case Name_kind: {
        RValue **cell = Env_LookupCell(env, e->v.Name.id);
        if (!cell || !*cell) {
            RT_Throw(in, "NameError", "name '%s' is not defined", e->v.Name.id);
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
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
            return NULL;
        }
        if (p->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "value cannot be dereferenced");
            return NULL;
        }
        if (!p->u.ptr.slot || !*p->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
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

static int resolve_target(Interp *in, expr_ty target, Env *env, Target *out) {
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
            RT_Throw(in, "NameError", "name '%s' is not defined", target->v.Name.id);
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
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
            return 0;
        }
        if (p->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "cannot assign through a non-dereferenceable value");
            return 0;
        }
        if (!p->u.ptr.slot || !*p->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
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
            RT_Throw(in, "TypeError", "slice assignment is not supported");
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
        RT_Throw(in, "TypeError", "invalid assignment target");
        return 0;
    }
}

static RValue *property_read(Interp *in, PropDesc *prop, RValue *self) {
    if (!prop->getter)
        return RT_ThrowValue(in, "AttributeError", "property '%s' has no getter", prop->name);
    return call_fn(in, bind_method(in, prop->getter, self), NULL, 0);
}

static void property_write(Interp *in, PropDesc *prop, RValue *self, RValue *value) {
    RValue *args[1];
    if (!prop->setter) {
        RT_Throw(in, "AttributeError", "property '%s' has no setter", prop->name);
        return;
    }
    args[0] = value;
    call_fn(in, bind_method(in, prop->setter, self), args, 1);
}

static int is_collection_value(const RValue *v) {
    return v && (v->kind == RV_LIST || v->kind == RV_TUPLE ||
                 v->kind == RV_DICT || v->kind == RV_SET ||
                 v->kind == RV_FROZENSET);
}

static int eval_slice_index_bound(Interp *in, expr_ty e, Env *env, size_t fallback, int *out) {
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
        RT_Throw(in, "TypeError", "slice bounds must be int");
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
static Signal exec_slice_assign(Interp *in, expr_ty target, RValue *value, Env *env) {
    RValue *container = eval_ref(in, target->v.Subscript.value, env);
    expr_ty slice = target->v.Subscript.index;
    int lo, hi;
    size_t len, i, count;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!container || container->kind != RV_LIST) {
        RT_Throw(in, "TypeError", "slice assignment is only allowed on lists");
        return SIG_THROW;
    }
    len = container->u.list->len;
    if (slice->v.Slice.step) {
        RValue *step = eval(in, slice->v.Slice.step, env);
        if (in->sig != SIG_OK)
            return in->sig;
        if (!step || step->kind != RV_INT || step->u.i != 1) {
            RT_Throw(in, "ValueError", "slice assignment step must be 1");
            return SIG_THROW;
        }
    }
    if (!eval_slice_index_bound(in, slice->v.Slice.lower, env, len, &lo) ||
        !eval_slice_index_bound(in, slice->v.Slice.upper, env, len, &hi))
        return in->sig;
    if (hi < lo)
        hi = lo;
    {
        IterCursor cur;
        AsterList *vals;
        if (!Iter_Begin(in, value, &cur)) {
            RT_Throw(in, "TypeError", "slice assignment requires an iterable value");
            return SIG_THROW;
        }
        vals = List_New(in);
        for (;;) {
            int ended = 0, ok = 1;
            RValue *item = Iter_Next(in, &cur, &ended, &ok);
            if (!ok)
                return in->sig;
            if (ended)
                break;
            List_Append(in, vals, RValue_Copy(in, item));
        }
        count = vals->len;
        for (i = 0; i < (size_t)(hi - lo); i++)
            List_RemoveAt(container->u.list, (size_t)lo);
        for (i = 0; i < count; i++)
            List_Insert(in, container->u.list, (size_t)lo + i, vals->items[i]);
    }
    return SIG_OK;
}

static Signal exec_assign(Interp *in, stmt_ty s, Env *env) {
    expr_ty target = s->v.Assign.target;
    RValue *value = eval(in, s->v.Assign.value, env);
    Target tgt;

    if (in->sig != SIG_OK)
        return in->sig;

    if (target->kind == Subscript_kind &&
        target->v.Subscript.index->kind == Slice_kind) {
        if (s->v.Assign.op != AssignEq) {
            RT_Throw(in, "TypeError", "slice assignment only supports '='");
            return SIG_THROW;
        }
        return exec_slice_assign(in, target, value, env);
    }

    if (target->kind == Tuple_kind) {
        asdl_seq *elems = target->v.Tuple.elements;
        size_t n = asdl_seq_LEN(elems);
        RValue **items = n ? Arena_Alloc(in->arena, n * sizeof(RValue *)) : NULL;
        IterCursor cur;
        size_t got = 0, i;
        if (s->v.Assign.op != AssignEq) {
            RT_Throw(in, "TypeError", "destructuring only supports '='");
            return SIG_THROW;
        }
        if (!Iter_Begin(in, value, &cur)) {
            RT_Throw(in, "TypeError", "cannot destructure a %s value", RValue_TypeName(value));
            return SIG_THROW;
        }
        for (;;) {
            int ended = 0, ok = 1;
            RValue *item = Iter_Next(in, &cur, &ended, &ok);
            if (!ok)
                return in->sig;
            if (ended)
                break;
            if (got < n)
                items[got] = item;
            got++;
        }
        if (got != n) {
            RT_Throw(in, "ValueError", "cannot unpack %zu values into %zu names", got, n);
            return SIG_THROW;
        }
        for (i = 0; i < n; i++) {
            expr_ty sub = asdl_seq_GET(elems, i);
            RValue *item = items[i];
            if (sub->kind != Name_kind) {
                RT_Throw(in, "TypeError", "destructuring targets must be names");
                return SIG_THROW;
            }
            int is_const = 0;
            Env_LookupCellFlags(env, sub->v.Name.id, &is_const);
            if (is_const) {
                RT_Throw(in, "TypeError", "cannot assign to const variable '%s'",
                         sub->v.Name.id);
                return SIG_THROW;
            }
            Env_Define(in, env, sub->v.Name.id, RValue_Copy(in, item));
        }
        return SIG_OK;
    }

    if (target->kind == Name_kind) {
        int is_const = 0;
        RValue **cell = Env_LookupCellFlags(env, target->v.Name.id, &is_const);

        if (is_const) {
            RT_Throw(in, "TypeError", "cannot assign to const variable '%s'",
                     target->v.Name.id);
            return SIG_THROW;
        }
        if (!cell) {
            /* 컬렉션에는 타입 이름이 없으므로 함수/람다처럼 첫 대입에서 정의한다. */
            if (value->kind == RV_FUNC || value->kind == RV_BUILTIN ||
                is_collection_value(value)) {
                Env_Define(in, env, target->v.Name.id, RValue_Copy(in, value));
                return SIG_OK;
            }
            RT_Throw(in, "NameError", "name '%s' is not defined", target->v.Name.id);
            return SIG_THROW;
        }
        if (s->v.Assign.op != AssignEq) {
            value = apply_binop(in, assign_op_to_operator(s->v.Assign.op), *cell, value);
            if (in->sig != SIG_OK)
                return in->sig;
        }
        *cell = RValue_Copy(in, value);
        return SIG_OK;
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

static Signal exec_if(Interp *in, stmt_ty s, Env *env) {
    RValue *test = eval(in, s->v.If.test, env);
    if (in->sig != SIG_OK)
        return in->sig;
    if (RValue_Truthy(test))
        return exec_block(in, s->v.If.body, Env_New(in, env));
    return exec_block(in, s->v.If.orelse, Env_New(in, env));
}

static int stmt_declares_names(stmt_ty s) {
    switch (s->kind) {
    case VarDecl_kind:
    case Destructure_kind:
    case FunctionDef_kind:
    case ClassDef_kind:
    case StructDef_kind:
    case InterfaceDef_kind:
    case ExceptionDef_kind:
    case EnumDef_kind:
    case Import_kind:
    case ImportFrom_kind:
        return 1;
    case Export_kind:
        return stmt_declares_names(s->v.Export.body);
    default:
        return 0;
    }
}

/* 본문이 자기 환경에 이름을 만들지 않으면, 반복마다 환경을 새로 만들 필요가 없다. */
static int body_declares_names(asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        if (stmt_declares_names(asdl_seq_GET(body, i)))
            return 1;
    return 0;
}

static Signal exec_while(Interp *in, stmt_ty s, Env *env) {
    int fresh = body_declares_names(s->v.While.body);
    int pooled = fresh && !body_escapes_env(s->v.While.body);
    Env *body_env = fresh ? NULL : Env_New(in, env);

    for (;;) {
        RValue *test = eval(in, s->v.While.test, env);
        Signal sig;
        Env *iter_env;
        if (in->sig != SIG_OK)
            return in->sig;
        if (!RValue_Truthy(test))
            break;
        iter_env = fresh ? (pooled ? Env_AcquireBlock(in, env) : Env_New(in, env))
                         : body_env;
        sig = exec_block(in, s->v.While.body, iter_env);
        if (pooled)
            Env_ReleaseBlock(in, iter_env);
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

static Signal exec_for(Interp *in, stmt_ty s, Env *env) {
    Env *loop_env = Env_New(in, env);
    int fresh = body_declares_names(s->v.For.body);
    int pooled = fresh && !body_escapes_env(s->v.For.body);
    Env *body_env = fresh ? NULL : Env_New(in, loop_env);

    exec_stmt(in, s->v.For.init, loop_env);
    if (in->sig != SIG_OK)
        return in->sig;
    for (;;) {
        RValue *test = eval(in, s->v.For.test, loop_env);
        Signal sig;
        Env *iter_env;
        if (in->sig != SIG_OK)
            return in->sig;
        if (!RValue_Truthy(test))
            break;
        iter_env = fresh ? (pooled ? Env_AcquireBlock(in, loop_env) : Env_New(in, loop_env))
                         : body_env;
        sig = exec_block(in, s->v.For.body, iter_env);
        if (pooled)
            Env_ReleaseBlock(in, iter_env);
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

static Signal exec_for_range(Interp *in, stmt_ty s, Env *env) {
    Env *loop_env = Env_New(in, env);
    Env *body_env;
    int fresh;
    int pooled;
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
        RT_Throw(in, "ValueError", "range for step cannot be zero");
        return SIG_THROW;
    }

    Env_Define(in, loop_env, s->v.ForRange.name, RValue_Int(in, from));
    cell = Env_LookupCell(loop_env, s->v.ForRange.name);
    fresh = body_declares_names(s->v.ForRange.body);
    pooled = fresh && !body_escapes_env(s->v.ForRange.body);
    body_env = fresh ? NULL : Env_New(in, loop_env);
    for (i = from; step > 0 ? i < to : i > to; i += step) {
        Signal sig;
        Env *iter_env;
        *cell = RValue_Int(in, i);
        iter_env = fresh ? (pooled ? Env_AcquireBlock(in, loop_env) : Env_New(in, loop_env))
                         : body_env;
        sig = exec_block(in, s->v.ForRange.body, iter_env);
        if (pooled)
            Env_ReleaseBlock(in, iter_env);
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

static Signal exec_for_in(Interp *in, stmt_ty s, Env *env) {
    Env *loop_env = Env_New(in, env);
    Env *body_env;
    int fresh;
    int pooled;
    RValue *iterable = eval(in, s->v.ForIn.iter, env);
    IterCursor cur;
    RValue **cell;

    if (in->sig != SIG_OK)
        return in->sig;
    if (!Iter_Begin(in, iterable, &cur))
        return in->sig;

    Env_Define(in, loop_env, s->v.ForIn.name, null_value(in));
    cell = Env_LookupCell(loop_env, s->v.ForIn.name);
    fresh = body_declares_names(s->v.ForIn.body);
    pooled = fresh && !body_escapes_env(s->v.ForIn.body);
    body_env = fresh ? NULL : Env_New(in, loop_env);
    for (;;) {
        int ended = 0, ok = 1;
        RValue *item = Iter_Next(in, &cur, &ended, &ok);
        Signal sig;
        Env *iter_env;
        if (!ok)
            return in->sig;
        if (ended)
            break;
        if (!check_declared_value(in, s->v.ForIn.ty, item))
            return in->sig;
        *cell = item;
        iter_env = fresh ? (pooled ? Env_AcquireBlock(in, loop_env) : Env_New(in, loop_env))
                         : body_env;
        sig = exec_block(in, s->v.ForIn.body, iter_env);
        if (pooled)
            Env_ReleaseBlock(in, iter_env);
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

static Signal exec_try(Interp *in, stmt_ty s, Env *env) {
    size_t saved_frames = in->nframes;
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
                Interp_FrameTruncate(in, saved_frames);
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
    if (sig != SIG_THROW)
        Interp_FrameTruncate(in, saved_frames);
    return sig;
}

static Signal exec_return(Interp *in, stmt_ty s, Env *env) {
    if (s->v.Return.value)
        in->ret = eval(in, s->v.Return.value, env);
    else
        in->ret = null_value(in);
    if (in->sig != SIG_OK)
        return in->sig;
    in->sig = SIG_RETURN;
    return SIG_RETURN;
}

static Signal exec_throw(Interp *in, stmt_ty s, Env *env) {
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

static char *read_source_file(const char *path, size_t *size) {
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

static char *module_name_from_path(Interp *in, asdl_seq *path) {
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

static int resolve_module_file(Interp *in, const char *name, char *out, size_t cap) {
    return Aster_ResolveModuleFile(in->base_dir, in->stdlib_dir, in->module_dirs,
                                   in->nmodule_dirs, name, out, cap);
}

static AstModule *module_cache_find(Interp *in, const char *name) {
    size_t i;
    for (i = 0; i < in->nmodules; i++) {
        if (!strcmp(in->modules[i]->name, name))
            return in->modules[i];
    }
    return NULL;
}

static void module_cache_add(Interp *in, AstModule *module) {
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

static const char *exported_stmt_name(stmt_ty s) {
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

static void collect_module_exports(Interp *in, AstModule *module, asdl_seq *body) {
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

static AstModule *load_module(Interp *in, asdl_seq *path) {
    char *name = module_name_from_path(in, path);
    AstModule *module = module_cache_find(in, name);
    AstModule *builtin = Module_Builtin(in, name);
    char file[1024];
    char *source;
    size_t size = 0;
    AsterError err;
    mod_ty mod;
    Signal sig;

    if (module)
        return module;
    if (builtin)
        return builtin;
    if (!resolve_module_file(in, name, file, sizeof file)) {
        RT_Throw(in, "ModuleNotFoundError", "cannot find module '%s'", name);
        return NULL;
    }
    source = read_source_file(file, &size);
    if (!source) {
        RT_Throw(in, "ImportError", "cannot read module '%s': %s", name, file);
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

static void import_public_names(Interp *in, Env *env, AstModule *module) {
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

static Signal exec_import(Interp *in, stmt_ty s, Env *env) {
    AstModule *module = load_module(in, s->v.Import.path);
    const char *bind_name;

    if (!module)
        return in->sig;
    bind_name = s->v.Import.alias;
    if (!bind_name) {
        size_t n = asdl_seq_LEN(s->v.Import.path);
        bind_name = asdl_seq_GET(s->v.Import.path, n - 1);
    }
    if (!s->v.Import.wildcard) {
        /* Java 스타일: import a.b.C; 는 모듈의 C 선언을 직접 가져온다. */
        RValue *decl = Module_GetExported(in, module, bind_name);
        if (decl) {
            Env_Define(in, env, bind_name, RValue_Copy(in, decl));
            return SIG_OK;
        }
    }
    Env_Define(in, env, bind_name, RValue_Module(in, module));
    if (s->v.Import.wildcard)
        import_public_names(in, env, module);
    return SIG_OK;
}

static Signal exec_import_from(Interp *in, stmt_ty s, Env *env) {
    AstModule *module = load_module(in, s->v.ImportFrom.module);

    if (!module)
        return in->sig;
    if (s->v.ImportFrom.name) {
        const char *name = s->v.ImportFrom.name;
        RValue *value = Module_GetExported(in, module, name);
        if (!value) {
            RT_Throw(in, "ImportError", "module '%s' has no member '%s'",
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

static int match_pattern(Interp *in, match_pattern_ty pat, RValue *value,
                         Env *env, int *ok) {
    *ok = 1;
    switch (pat->kind) {
    case PBind_kind:
        if (strcmp(pat->v.PBind.name, "_"))
            Env_Define(in, env, pat->v.PBind.name, RValue_Copy(in, value));
        return 1;
    case PLiteral_kind: {
        RValue *lit = eval(in, pat->v.PLiteral.value, env);
        if (in->sig != SIG_OK) {
            *ok = 0;
            return 0;
        }
        return RValue_Equal(lit, value);
    }
    case PVariant_kind: {
        RValue *ctor = eval(in, pat->v.PVariant.path, env);
        AstEnumType *etype;
        size_t index, i;
        asdl_seq *items = pat->v.PVariant.items;
        size_t nitems = asdl_seq_LEN(items);
        if (in->sig != SIG_OK) {
            *ok = 0;
            return 0;
        }
        if (ctor->kind == RV_ENUM) {
            etype = ctor->u.en.type;
            index = ctor->u.en.index;
        }
        else if (ctor->kind == RV_VARIANT_CTOR) {
            etype = ctor->u.vc.type;
            index = ctor->u.vc.index;
        }
        else {
            RT_Throw(in, "TypeError", "case pattern must name an enum variant");
            *ok = 0;
            return 0;
        }
        if (value->kind != RV_ENUM || value->u.en.type != etype ||
            value->u.en.index != index || value->u.en.npayload != nitems)
            return 0;
        for (i = 0; i < nitems; i++) {
            int sub_ok = 1;
            if (!match_pattern(in, asdl_seq_GET(items, i), value->u.en.payload[i],
                               env, &sub_ok))
                return 0;
            if (!sub_ok) {
                *ok = 0;
                return 0;
            }
        }
        return 1;
    }
    }
    *ok = 0;
    return 0;
}

static Signal exec_switch(Interp *in, stmt_ty s, Env *env) {
    RValue *subject = eval(in, s->v.Switch.subject, env);
    asdl_seq *cases;
    size_t i;
    if (in->sig != SIG_OK)
        return in->sig;
    cases = s->v.Switch.cases;
    for (i = 0; i < asdl_seq_LEN(cases); i++) {
        match_case_ty mc = asdl_seq_GET(cases, i);
        Env *case_env = Env_New(in, env);
        int ok = 1;
        if (match_pattern(in, mc->pattern, subject, case_env, &ok))
            return exec_block(in, mc->body, case_env);
        if (!ok)
            return in->sig;
    }
    return SIG_OK;
}

/* ------------------------------------------------------------ 디버거 */

static const char *debug_stmt_name(stmt_ty s) {
    switch (s->kind) {
    case FunctionDef_kind: return "fn";
    case ClassDef_kind: return "class";
    case InterfaceDef_kind: return "interface";
    case StructDef_kind: return "struct";
    case VarDecl_kind: return "var";
    case EnumDef_kind: return "enum";
    case Destructure_kind: return "unpack";
    case Assert_kind: return "assert";
    case Assign_kind: return "assign";
    case If_kind: return "if";
    case While_kind: return "while";
    case For_kind: return "for";
    case ForRange_kind: return "for-range";
    case ForIn_kind: return "for-in";
    case Switch_kind: return "switch";
    case Try_kind: return "try";
    case Yield_kind: return "yield";
    case Return_kind: return "return";
    case Throw_kind: return "throw";
    case Break_kind: return "break";
    case Continue_kind: return "continue";
    case Import_kind: return "import";
    case ImportFrom_kind: return "from-import";
    case Export_kind: return "export";
    case ExceptionDef_kind: return "exception";
    case ExprStmt_kind: return "expr";
    }
    return "stmt";
}

static int debug_has_break(Interp *in, int line) {
    size_t i;
    for (i = 0; i < in->ndebug_breaks; i++)
        if (in->debug_breaks[i] == line)
            return 1;
    return 0;
}

static void debug_add_break(Interp *in, int line) {
    if (in->ndebug_breaks == in->cap_debug_breaks) {
        size_t want = in->cap_debug_breaks ? in->cap_debug_breaks * 2 : 8;
        int *grown = realloc(in->debug_breaks, want * sizeof *grown);
        if (!grown)
            return;
        in->debug_breaks = grown;
        in->cap_debug_breaks = want;
    }
    in->debug_breaks[in->ndebug_breaks++] = line;
}

static char *debug_trim(char *s) {
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == '\r' || end[-1] == '\n' ||
                       end[-1] == ' ' || end[-1] == '\t'))
        *--end = 0;
    return s;
}

static void debug_print_value(Interp *in, expr_ty e, Env *env) {
    Signal saved_sig = in->sig;
    RValue *saved_exc = in->exc;
    int saved_debug = in->debug;
    RValue *value;

    in->sig = SIG_OK;
    in->debug = 0;              /* 식 평가 중에는 다시 멈추지 않는다 */
    value = eval(in, e, env);
    if (in->sig != SIG_OK) {
        RValue *exc = in->exc;
        if (exc && exc->kind == RV_EXC) {
            fprintf(stderr, "%s: ", exc->u.exc->name);
            RValue_Print(stderr, exc->u.exc->message, 0);
            fputc('\n', stderr);
        }
        else {
            fputs("error\n", stderr);
        }
    }
    else {
        RValue_Print(stderr, value, 1);
        fputc('\n', stderr);
    }
    in->debug = saved_debug;
    in->sig = saved_sig;
    in->exc = saved_exc;
}

static void debug_tick(Interp *in, stmt_ty s, Env *env) {
    char line[512];

    if (!in->debug)
        return;
    if (!in->debug_step && !debug_has_break(in, s->lineno))
        return;
    in->debug_step = 0;

    for (;;) {
        char *cmd;
        fprintf(stderr, "[%s:%d] %s\n",
                in->debug_file ? in->debug_file : "<program>",
                s->lineno, debug_stmt_name(s));
        fputs("(dbg) ", stderr);
        fflush(stderr);
        if (!fgets(line, sizeof line, stdin)) {
            in->debug = 0;
            fputc('\n', stderr);
            return;
        }
        cmd = debug_trim(line);
        if (cmd[0] == 's' && (cmd[1] == 0 || strcmp(cmd, "step") == 0)) {
            in->debug_step = 1;
            return;
        }
        if (cmd[0] == 'c' && (cmd[1] == 0 || strcmp(cmd, "continue") == 0)) {
            return;
        }
        if (cmd[0] == 'q' && (cmd[1] == 0 || strcmp(cmd, "quit") == 0)) {
            fflush(stdout);
            exit(0);
        }
        if ((cmd[0] == 'p' || cmd[0] == 'b') &&
            (cmd[1] == ' ' || cmd[1] == '\t')) {
            char *arg = debug_trim(cmd + 1);
            if (cmd[0] == 'b') {
                int target = atoi(arg);
                if (target > 0) {
                    debug_add_break(in, target);
                    fprintf(stderr, "breakpoint at line %d\n", target);
                }
                else {
                    size_t i;
                    fputs("breakpoints:", stderr);
                    for (i = 0; i < in->ndebug_breaks; i++)
                        fprintf(stderr, " %d", in->debug_breaks[i]);
                    fputc('\n', stderr);
                }
            }
            else {
                AsterError err;
                expr_ty e = Aster_ParseExpression(arg, strlen(arg),
                                                  in->arena, &err);
                if (!e)
                    fprintf(stderr, "error: %s\n", err.message);
                else
                    debug_print_value(in, e, env);
            }
            continue;
        }
        if (cmd[0] == 'h' || cmd[0] == '?') {
            fputs("  s, step        execute one statement\n"
                  "  c, continue    run until a breakpoint\n"
                  "  p EXPR         evaluate and print EXPR\n"
                  "  EXPR           print EXPR\n"
                  "  b LINE         add a breakpoint\n"
                  "  b              list breakpoints\n"
                  "  q, quit        stop the program\n",
                  stderr);
            continue;
        }
        if (cmd[0]) {
            AsterError err;
            expr_ty e = Aster_ParseExpression(cmd, strlen(cmd), in->arena, &err);
            if (!e) {
                fprintf(stderr, "error: %s\n", err.message);
            }
            else {
                debug_print_value(in, e, env);
            }
            continue;
        }
        /* 빈 줄은 한 문장 진행으로 본다. */
        in->debug_step = 1;
        return;
    }
}

static Signal exec_stmt(Interp *in, stmt_ty s, Env *env) {
    in->line = s->lineno;
    if (in->nframes > 0)
        in->frames[in->nframes - 1].line = s->lineno;

    if (in->debug)
        debug_tick(in, s, env);

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
    case Switch_kind:
        return exec_switch(in, s, env);
    case Try_kind:
        return exec_try(in, s, env);
    case Return_kind:
        return exec_return(in, s, env);
    case Yield_kind: {
        RValue *value = s->v.Yield.value ? eval(in, s->v.Yield.value, env) : null_value(in);
        if (in->sig != SIG_OK)
            return in->sig;
        if (!in->gen) {
            RT_Throw(in, "RuntimeError", "'yield' is only allowed inside generator functions");
            return SIG_THROW;
        }
        in->gen->yielded = value;
        gen_context_switch(in->gen->ctx, in->gen->caller_ctx);
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
    case ExprStmt_kind: {
        RValue *value = eval(in, s->v.ExprStmt.value, env);
        if (in->repl && in->sig == SIG_OK && value && value->kind != RV_NULL) {
            RValue_Print(stdout, value, 1);
            fputc('\n', stdout);
            fflush(stdout);
        }
        return in->sig;
    }
    }
    return SIG_OK;
}

/* ---- 식 ---- */

/* 인자가 적으면 호출자 스택의 버퍼를 쓴다 (호출이 끝나기 전에는 유효하다). */
static int eval_args(Interp *in, asdl_seq *args, Env *env,
                     RValue **small, size_t smallcap, RValue ***out, size_t *nout) {
    size_t n = asdl_seq_LEN(args);
    RValue **values = n <= smallcap ? small : Arena_Alloc(in->arena, n * sizeof(RValue *));
    size_t i;

    for (i = 0; i < n; i++) {
        values[i] = eval(in, asdl_seq_GET(args, i), env);
        if (in->sig != SIG_OK)
            return 0;
    }
    *out = values;
    *nout = n;
    return 1;
}

static RValue *make_exception(Interp *in, RValue *value, const char *exc_type) {
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
        RT_Throw(in, "TypeError", "'throw' requires a string or an exception value");
        return NULL;
    }
}

static RValue *eval_new(Interp *in, expr_ty e, Env *env) {
    RValue **cell = Env_LookupCell(env, e->v.New.cls);
    RValue *small[8];
    RValue **args;
    size_t nargs;
    RValue *type_value;

    if (!cell || !*cell)
        return RT_ThrowValue(in, "NameError", "cannot find type '%s'", e->v.New.cls);
    type_value = *cell;
    if (!eval_args(in, e->v.New.args, env, small, 8, &args, &nargs))
        return NULL;

    if (type_value->kind == RV_CLASS)
        return instantiate_class(in, type_value->u.cls, args, nargs);
    if (type_value->kind == RV_STRUCTTYPE)
        return instantiate_struct(in, type_value->u.stype, args, nargs);
    return RT_ThrowValue(in, "TypeError", "'%s' cannot be created with 'new'",
                         e->v.New.cls);
}

/* ---- 컬렉션 ---- */

static int is_setlike(const RValue *v) {
    return v && (v->kind == RV_SET || v->kind == RV_FROZENSET);
}

static int set_subset(const RValue *a, const RValue *b) {
    size_t i;
    for (i = 0; i < a->u.set->len; i++) {
        if (!Set_Has(b->u.set, a->u.set->items[i]))
            return 0;
    }
    return 1;
}

static int eval_slice_bound(Interp *in, expr_ty e, Env *env, int *present, int64_t *out) {
    RValue *v;

    if (!e) {
        *present = 0;
        return 1;
    }
    v = eval(in, e, env);
    if (in->sig != SIG_OK)
        return 0;
    if (!v || (v->kind != RV_INT && v->kind != RV_BOOL)) {
        RT_Throw(in, "TypeError", "slice indices must be integers");
        return 0;
    }
    *present = 1;
    *out = v->kind == RV_INT ? v->u.i : v->u.b;
    return 1;
}

static int64_t clamp_slice(int64_t v, int64_t len, int64_t step) {
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
static RValue *eval_slice(Interp *in, RValue *container, expr_ty slice_expr, Env *env) {
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
        len = (int64_t)container->u.s->cp_length;
    else
        return RT_ThrowValue(in, "TypeError", "'%s' object does not support slicing",
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
        AsterString *s = container->u.s;
        char *buf = xmalloc_fallback(s->length + 1);
        for (i = start; step > 0 ? i < stop : i > stop; ) {
            size_t off = AsterString_ByteOffset(s, (size_t)i);
            size_t adv = 1;
            Aster_UTF8_Decode(s->data + off, s->length - off, &adv);
            memcpy(buf + n, s->data + off, adv);
            n += adv;
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

static RValue *eval(Interp *in, expr_ty e, Env *env) {
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
            RT_Throw(in, "NameError", "name '%s' is not defined", e->v.Name.id);
            return NULL;
        }
        return *cell;
    }
    case Self_kind: {
        RValue **cell = Env_LookupCell(env, "self");
        if (!cell) {
            RT_Throw(in, "RuntimeError", "'self' is only allowed inside methods");
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
                IterCursor cur;
                if (in->sig != SIG_OK)
                    return NULL;
                if (!Iter_Begin(in, inner, &cur))
                    return NULL;
                for (;;) {
                    int ended = 0, ok = 1;
                    RValue *v = Iter_Next(in, &cur, &ended, &ok);
                    if (!ok)
                        return NULL;
                    if (ended)
                        break;
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
                    RT_Throw(in, "TypeError", "dicts can only be spread into dicts");
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
                IterCursor cur;
                if (in->sig != SIG_OK)
                    return NULL;
                if (!Iter_Begin(in, inner, &cur))
                    return NULL;
                for (;;) {
                    int ended = 0, ok = 1;
                    RValue *v = Iter_Next(in, &cur, &ended, &ok);
                    if (!ok)
                        return NULL;
                    if (ended)
                        break;
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
        IterCursor cur;
        int ok;
        if (in->sig != SIG_OK)
            return NULL;
        if (!Iter_Begin(in, source, &cur))
            return NULL;
        loop_env = Env_New(in, env);
        if (is_set)
            set = Set_New(in);
        else
            list = List_New(in);
        Env_Define(in, loop_env, name, null_value(in));
        for (;;) {
            int ended = 0;
            RValue *item, *value;
            ok = 1;
            item = Iter_Next(in, &cur, &ended, &ok);
            if (!ok)
                return NULL;
            if (ended)
                break;
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
        IterCursor cur;
        int ok;
        if (in->sig != SIG_OK)
            return NULL;
        if (!Iter_Begin(in, source, &cur))
            return NULL;
        loop_env = Env_New(in, env);
        dict = Dict_New(in);
        Env_Define(in, loop_env, e->v.DictComp.name, null_value(in));
        for (;;) {
            int ended = 0;
            RValue *item, *key, *value;
            ok = 1;
            item = Iter_Next(in, &cur, &ended, &ok);
            if (!ok)
                return NULL;
            if (ended)
                break;
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
        return RT_ThrowValue(in, "SyntaxError", "spread is not allowed here");
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
        if (current->kind == RV_INT) {
            updated = int_add(in, current->u.i, e->v.Incr.is_inc ? 1 : -1);
            if (!updated)
                return NULL;
        }
        else if (current->kind == RV_FLOAT)
            updated = RValue_Float(in, current->u.f + (e->v.Incr.is_inc ? 1.0 : -1.0));
        else
            return RT_ThrowValue(in, "TypeError", "cannot increment/decrement a %s value",
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
            if (operand->kind == RV_OBJECT && RValue_HasMethod(in, operand, "__neg__"))
                return Interp_CallMethod(in, operand, "__neg__", NULL, 0);
            if (operand->kind == RV_FLOAT)
                return RValue_Float(in, -operand->u.f);
            if (operand->kind == RV_INT && operand->u.i == INT64_MIN)
                return int_overflow(in);
            if (operand->kind == RV_INT || operand->kind == RV_BOOL)
                return RValue_Int(in, -RValue_AsInt(in, operand, &ok));
            RT_Throw(in, "TypeError", "unary '-' requires a number");
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
        if (left->kind == RV_OBJECT || right->kind == RV_OBJECT) {
            const char *name = NULL, *reflected = NULL;
            RValue *handled = NULL;
            switch (e->v.Compare.op) {
            case Eq: name = "__eq__"; reflected = "__eq__"; break;
            case NotEq: name = "__eq__"; reflected = "__eq__"; break;
            case Lt: name = "__lt__"; reflected = "__gt__"; break;
            case LtE: name = "__le__"; reflected = "__ge__"; break;
            case Gt: name = "__gt__"; reflected = "__lt__"; break;
            case GtE: name = "__ge__"; reflected = "__le__"; break;
            }
            if (left->kind == RV_OBJECT && RValue_HasMethod(in, left, name)) {
                RValue *args[1];
                args[0] = right;
                handled = Interp_CallMethod(in, left, name, args, 1);
            }
            else if (right->kind == RV_OBJECT &&
                     RValue_HasMethod(in, right, reflected)) {
                RValue *args[1];
                args[0] = left;
                handled = Interp_CallMethod(in, right, reflected, args, 1);
            }
            if (handled) {
                int truth = RValue_Truthy(handled);
                if (e->v.Compare.op == NotEq)
                    truth = !truth;
                return RValue_Bool(in, truth);
            }
            if (in->sig != SIG_OK)
                return NULL;
        }
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
            RT_Throw(in, "TypeError", "cannot take the address of a collection element");
            return NULL;
        }
        if (target.prop) {
            RT_Throw(in, "TypeError", "cannot take the address of a property");
            return NULL;
        }
        return RValue_Ptr(in, target.slot, NULL);
    }
    case Deref_kind: {
        RValue *operand = eval(in, e->v.Deref.value, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (!operand || operand->kind == RV_NULL) {
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
            return NULL;
        }
        if (operand->kind == RV_OBJECT)
            return operand;
        if (operand->kind != RV_PTR) {
            RT_Throw(in, "TypeError", "value cannot be dereferenced");
            return NULL;
        }
        if (!operand->u.ptr.slot || !*operand->u.ptr.slot) {
            RT_Throw(in, "NullPointerError", "dereferenced a null pointer");
            return NULL;
        }
        return RValue_Copy(in, *operand->u.ptr.slot);
    }
    case SizeOf_kind:
        return RValue_Int(in, (int64_t)Type_SizeOf(in, env, e->v.SizeOf.ty));
    case Call_kind: {
        RValue *callee;
        RValue *small[8];
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
        if (!eval_args(in, e->v.Call.args, env, small, 8, &args, &nargs))
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
        return RT_ThrowValue(in, "TypeError", "array literals are not supported");
    case Range_kind: {
        RValue *lo_v = eval(in, e->v.Range.start, env);
        RValue *hi_v, *step_v = NULL;
        AsterList *list;
        int64_t lo, hi, step = 1, i;
        int ok = 1;
        if (in->sig != SIG_OK)
            return NULL;
        hi_v = eval(in, e->v.Range.stop, env);
        if (in->sig != SIG_OK)
            return NULL;
        if (e->v.Range.step) {
            step_v = eval(in, e->v.Range.step, env);
            if (in->sig != SIG_OK)
                return NULL;
        }
        lo = RValue_AsInt(in, lo_v, &ok);
        if (!ok)
            return NULL;
        hi = RValue_AsInt(in, hi_v, &ok);
        if (!ok)
            return NULL;
        if (step_v) {
            step = RValue_AsInt(in, step_v, &ok);
            if (!ok)
                return NULL;
        }
        if (step == 0)
            return RT_ThrowValue(in, "ValueError", "range step cannot be zero");
        list = List_New(in);
        if (step > 0) {
            for (i = lo; i <= hi; i += step) {
                List_Append(in, list, RValue_Int(in, i));
                if (i > INT64_MAX - step)
                    break;
            }
        }
        else {
            for (i = lo; i >= hi; i += step) {
                List_Append(in, list, RValue_Int(in, i));
                if (i < INT64_MIN - step)
                    break;
            }
        }
        return RValue_List(in, list);
    }
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
        return RT_ThrowValue(in, "TypeError", "slicing is only allowed inside brackets");
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
    return RT_ThrowValue(in, "RuntimeError", "unknown expression node");
}

/* ---- 진입점 ---- */

int Interp_RunModule(Interp *in, mod_ty mod) {
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
        RT_Throw(in, "RuntimeError", "'break' and 'continue' are only allowed inside loops");
        return 1;
    }
    return in->sig == SIG_THROW ? 1 : 0;
}
