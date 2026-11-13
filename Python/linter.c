/* Aster 린터: 사용하지 않는 지역 변수와 도달할 수 없는 코드. */
#include "internal/linter.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int line;
    int col;
} LintDecl;

typedef struct {
    const char *name;
} LintUse;

typedef struct {
    FILE *out;
    const char *filename;
    int warnings;

    LintDecl *decls;
    size_t ndecls, cap_decls;
    LintUse *uses;
    size_t nuses, cap_uses;
} Linter;

static void lint_stmt(Linter *l, stmt_ty s);

static void *grow(void *items, size_t *cap, size_t elem) {
    size_t want = *cap ? *cap * 2 : 16;
    void *grown = realloc(items, want * elem);
    if (!grown)
        return NULL;
    *cap = want;
    return grown;
}

static int has_decl(Linter *l, const char *name) {
    size_t i;
    for (i = 0; i < l->ndecls; i++)
        if (strcmp(l->decls[i].name, name) == 0)
            return 1;
    return 0;
}

static void add_decl(Linter *l, const char *name, int line, int col) {
    if (!name || has_decl(l, name))
        return;
    if (l->ndecls == l->cap_decls) {
        void *grown = grow(l->decls, &l->cap_decls, sizeof *l->decls);
        if (!grown)
            return;
        l->decls = grown;
    }
    l->decls[l->ndecls].name = name;
    l->decls[l->ndecls].line = line;
    l->decls[l->ndecls].col = col;
    l->ndecls++;
}

static int has_use(Linter *l, const char *name) {
    size_t i;
    for (i = 0; i < l->nuses; i++)
        if (strcmp(l->uses[i].name, name) == 0)
            return 1;
    return 0;
}

static void add_use(Linter *l, const char *name) {
    if (!name || has_use(l, name))
        return;
    if (l->nuses == l->cap_uses) {
        void *grown = grow(l->uses, &l->cap_uses, sizeof *l->uses);
        if (!grown)
            return;
        l->uses = grown;
    }
    l->uses[l->nuses].name = name;
    l->nuses++;
}

static void warn(Linter *l, int line, int col, const char *message) {
    fprintf(l->out, "%s:%d:%d: warning: %s\n", l->filename, line, col + 1, message);
    l->warnings++;
}

/* --------------------------------------------------------------- 식 순회 */

static void walk_expr(Linter *l, expr_ty e, int as_target) {
    size_t i;

    if (!e)
        return;
    switch (e->kind) {
    case Name_kind:
        if (!as_target)
            add_use(l, e->v.Name.id);
        break;
    case Lambda_kind:
        walk_expr(l, e->v.Lambda.body, 0);
        break;
    case Pipe_kind:
        walk_expr(l, e->v.Pipe.value, 0);
        walk_expr(l, e->v.Pipe.func, 0);
        break;
    case Ternary_kind:
        walk_expr(l, e->v.Ternary.test, 0);
        walk_expr(l, e->v.Ternary.body, 0);
        walk_expr(l, e->v.Ternary.orelse, 0);
        break;
    case Coalesce_kind:
        walk_expr(l, e->v.Coalesce.left, 0);
        walk_expr(l, e->v.Coalesce.right, 0);
        break;
    case BoolOp_kind:
        walk_expr(l, e->v.BoolOp.left, 0);
        walk_expr(l, e->v.BoolOp.right, 0);
        break;
    case UnaryOp_kind:
        walk_expr(l, e->v.UnaryOp.operand, 0);
        break;
    case BinOp_kind:
        walk_expr(l, e->v.BinOp.left, 0);
        walk_expr(l, e->v.BinOp.right, 0);
        break;
    case Compare_kind:
        walk_expr(l, e->v.Compare.left, 0);
        walk_expr(l, e->v.Compare.right, 0);
        break;
    case Is_kind:
        walk_expr(l, e->v.Is.value, 0);
        break;
    case In_kind:
        walk_expr(l, e->v.In.item, 0);
        walk_expr(l, e->v.In.container, 0);
        break;
    case Cast_kind:
        walk_expr(l, e->v.Cast.value, 0);
        break;
    case AddressOf_kind:
        walk_expr(l, e->v.AddressOf.value, 0);
        break;
    case Deref_kind:
        walk_expr(l, e->v.Deref.value, 0);
        break;
    case Call_kind:
        walk_expr(l, e->v.Call.func, 0);
        for (i = 0; i < asdl_seq_LEN(e->v.Call.args); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Call.args, i), 0);
        for (i = 0; i < asdl_seq_LEN(e->v.Call.keywords); i++) {
            keyword_ty kw = asdl_seq_GET(e->v.Call.keywords, i);
            walk_expr(l, kw->value, 0);
        }
        break;
    case New_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.New.args); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.New.args, i), 0);
        break;
    case Attribute_kind:
        walk_expr(l, e->v.Attribute.value, 0);
        break;
    case PtrAttribute_kind:
        walk_expr(l, e->v.PtrAttribute.value, 0);
        break;
    case OptionalAttribute_kind:
        walk_expr(l, e->v.OptionalAttribute.value, 0);
        break;
    case Array_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.Array.elements); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Array.elements, i), 0);
        break;
    case Range_kind:
        walk_expr(l, e->v.Range.start, 0);
        walk_expr(l, e->v.Range.stop, 0);
        walk_expr(l, e->v.Range.step, 0);
        break;
    case Subscript_kind:
        walk_expr(l, e->v.Subscript.value, 0);
        walk_expr(l, e->v.Subscript.index, 0);
        break;
    case Slice_kind:
        walk_expr(l, e->v.Slice.lower, 0);
        walk_expr(l, e->v.Slice.upper, 0);
        walk_expr(l, e->v.Slice.step, 0);
        break;
    case FString_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.FString.values); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.FString.values, i), 0);
        break;
    case FormattedValue_kind:
        walk_expr(l, e->v.FormattedValue.value, 0);
        walk_expr(l, e->v.FormattedValue.format_spec, 0);
        break;
    case List_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.List.elements); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.List.elements, i), 0);
        break;
    case Tuple_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.Tuple.elements); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Tuple.elements, i), 0);
        break;
    case Dict_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.Dict.keys); i++) {
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Dict.keys, i), 0);
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Dict.values, i), 0);
        }
        break;
    case Set_kind:
        for (i = 0; i < asdl_seq_LEN(e->v.Set.elements); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(e->v.Set.elements, i), 0);
        break;
    case Spread_kind:
        walk_expr(l, e->v.Spread.value, 0);
        break;
    case ListComp_kind:
        walk_expr(l, e->v.ListComp.iter, 0);
        walk_expr(l, e->v.ListComp.elt, 0);
        walk_expr(l, e->v.ListComp.cond, 0);
        break;
    case SetComp_kind:
        walk_expr(l, e->v.SetComp.iter, 0);
        walk_expr(l, e->v.SetComp.elt, 0);
        walk_expr(l, e->v.SetComp.cond, 0);
        break;
    case DictComp_kind:
        walk_expr(l, e->v.DictComp.iter, 0);
        walk_expr(l, e->v.DictComp.key, 0);
        walk_expr(l, e->v.DictComp.value, 0);
        walk_expr(l, e->v.DictComp.cond, 0);
        break;
    case Incr_kind:
        walk_expr(l, e->v.Incr.target, 1);
        break;
    default:
        break;
    }
}

static void walk_initializer(Linter *l, initializer_ty init) {
    size_t i;
    if (!init)
        return;
    switch (init->kind) {
    case ExprInit_kind:
        walk_expr(l, init->v.ExprInit.value, 0);
        break;
    case PositionalInit_kind:
        for (i = 0; i < asdl_seq_LEN(init->v.PositionalInit.values); i++)
            walk_expr(l, (expr_ty)asdl_seq_GET(init->v.PositionalInit.values, i), 0);
        break;
    case DesignatedInit_kind:
        for (i = 0; i < asdl_seq_LEN(init->v.DesignatedInit.items); i++) {
            designation_ty d = (designation_ty)asdl_seq_GET(init->v.DesignatedInit.items, i);
            walk_expr(l, d->value, 0);
        }
        break;
    case NoInit_kind:
        break;
    }
}

static void check_block(Linter *l, asdl_seq *body);

static void walk_stmt(Linter *l, stmt_ty s) {
    size_t i;

    if (!s)
        return;
    switch (s->kind) {
    case VarDecl_kind:
        add_decl(l, s->v.VarDecl.name, s->lineno, s->col_offset);
        walk_initializer(l, s->v.VarDecl.init);
        break;
    case Destructure_kind:
        for (i = 0; i < asdl_seq_LEN(s->v.Destructure.names); i++)
            add_decl(l, (const char *)asdl_seq_GET(s->v.Destructure.names, i),
                     s->lineno, s->col_offset);
        walk_expr(l, s->v.Destructure.value, 0);
        break;
    case Assert_kind:
        walk_expr(l, s->v.Assert.test, 0);
        walk_expr(l, s->v.Assert.message, 0);
        break;
    case Assign_kind:
        walk_expr(l, s->v.Assign.target, 1);
        walk_expr(l, s->v.Assign.value, 0);
        break;
    case If_kind:
        walk_expr(l, s->v.If.test, 0);
        check_block(l, s->v.If.body);
        check_block(l, s->v.If.orelse);
        break;
    case While_kind:
        walk_expr(l, s->v.While.test, 0);
        check_block(l, s->v.While.body);
        break;
    case For_kind:
        walk_stmt(l, s->v.For.init);
        walk_expr(l, s->v.For.test, 0);
        walk_stmt(l, s->v.For.step);
        check_block(l, s->v.For.body);
        break;
    case ForRange_kind:
        add_decl(l, s->v.ForRange.name, s->lineno, s->col_offset);
        walk_expr(l, s->v.ForRange.iter->start, 0);
        walk_expr(l, s->v.ForRange.iter->stop, 0);
        walk_expr(l, s->v.ForRange.iter->step, 0);
        check_block(l, s->v.ForRange.body);
        break;
    case ForIn_kind:
        add_decl(l, s->v.ForIn.name, s->lineno, s->col_offset);
        walk_expr(l, s->v.ForIn.iter, 0);
        check_block(l, s->v.ForIn.body);
        break;
    case Switch_kind:
        walk_expr(l, s->v.Switch.subject, 0);
        for (i = 0; i < asdl_seq_LEN(s->v.Switch.cases); i++) {
            match_case_ty c = (match_case_ty)asdl_seq_GET(s->v.Switch.cases, i);
            if (c->guard)
                walk_expr(l, c->guard, 0);
            check_block(l, c->body);
        }
        break;
    case Try_kind:
        check_block(l, s->v.Try.body);
        for (i = 0; i < asdl_seq_LEN(s->v.Try.handlers); i++) {
            handler_ty h = (handler_ty)asdl_seq_GET(s->v.Try.handlers, i);
            add_decl(l, h->name, s->lineno, s->col_offset);
            check_block(l, h->body);
        }
        check_block(l, s->v.Try.finalbody);
        break;
    case Yield_kind:
        walk_expr(l, s->v.Yield.value, 0);
        break;
    case Return_kind:
        walk_expr(l, s->v.Return.value, 0);
        break;
    case Throw_kind:
        walk_expr(l, s->v.Throw.value, 0);
        break;
    case Export_kind:
        walk_stmt(l, s->v.Export.body);
        break;
    case ExprStmt_kind:
        walk_expr(l, s->v.ExprStmt.value, 0);
        break;
    default:
        /* 중첩 함수/클래스는 자기 스코프를 따로 린트한다. */
        break;
    }
}

static int terminates(stmt_ty s) {
    return s->kind == Return_kind || s->kind == Throw_kind ||
           s->kind == Break_kind || s->kind == Continue_kind;
}

static void check_block(Linter *l, asdl_seq *body) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        walk_stmt(l, (stmt_ty)asdl_seq_GET(body, i));
    for (i = 1; i < asdl_seq_LEN(body); i++) {
        if (terminates((stmt_ty)asdl_seq_GET(body, i - 1))) {
            stmt_ty dead = (stmt_ty)asdl_seq_GET(body, i);
            warn(l, dead->lineno, dead->col_offset, "unreachable code");
            break;
        }
    }
}

/* --------------------------------------------------------- 함수 단위 검사 */

static void report_unused(Linter *l) {
    size_t i;
    for (i = 0; i < l->ndecls; i++) {
        const char *name = l->decls[i].name;
        if (name[0] == '_')
            continue;               /* _로 시작하면 의도적으로 안 써도 된다. */
        if (!has_use(l, name))
            warn(l, l->decls[i].line, l->decls[i].col,
                 "unused variable (declared but never read)");
    }
}

static void lint_function(Linter *l, asdl_seq *params, asdl_seq *body) {
    size_t i;

    l->ndecls = 0;
    l->nuses = 0;
    for (i = 0; i < asdl_seq_LEN(params); i++) {
        param_ty p = (param_ty)asdl_seq_GET(params, i);
        add_decl(l, p->name, 0, 0);
        add_use(l, p->name);
    }
    check_block(l, body);
    report_unused(l);
}

static void lint_members(Linter *l, asdl_seq *members) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(members); i++) {
        member_ty m = (member_ty)asdl_seq_GET(members, i);
        switch (m->kind) {
        case Method_kind:
            lint_function(l, m->v.Method.params, m->v.Method.body);
            break;
        case Constructor_kind:
            lint_function(l, m->v.Constructor.params, m->v.Constructor.body);
            break;
        case Getter_kind:
            lint_function(l, NULL, m->v.Getter.body);
            break;
        case Setter_kind:
            l->ndecls = 0;
            l->nuses = 0;
            add_decl(l, m->v.Setter.param->name, 0, 0);
            add_use(l, m->v.Setter.param->name);
            check_block(l, m->v.Setter.body);
            report_unused(l);
            break;
        default:
            break;
        }
    }
}

static void lint_struct_members(Linter *l, asdl_seq *members) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(members); i++) {
        struct_member_ty m = (struct_member_ty)asdl_seq_GET(members, i);
        if (m->kind == StructCtor_kind)
            lint_function(l, m->v.StructCtor.params, m->v.StructCtor.body);
    }
}

static void lint_stmt(Linter *l, stmt_ty s) {
    if (!s)
        return;
    switch (s->kind) {
    case FunctionDef_kind:
        if (!s->v.FunctionDef.is_extern) {
            lint_function(l, s->v.FunctionDef.params, s->v.FunctionDef.body);
        }
        break;
    case ClassDef_kind:
        lint_members(l, s->v.ClassDef.members);
        break;
    case StructDef_kind:
        lint_struct_members(l, s->v.StructDef.members);
        break;
    case Export_kind:
        lint_stmt(l, s->v.Export.body);
        break;
    default:
        break;
    }
}

int Aster_LintModule(FILE *out, mod_ty mod, const char *filename) {
    Linter l;
    size_t i;

    memset(&l, 0, sizeof l);
    l.out = out;
    l.filename = filename ? filename : "<input>";
    if (!mod)
        return 0;
    for (i = 0; i < asdl_seq_LEN(mod->v.Module.body); i++)
        lint_stmt(&l, (stmt_ty)asdl_seq_GET(mod->v.Module.body, i));
    free(l.decls);
    free(l.uses);
    return l.warnings;
}
