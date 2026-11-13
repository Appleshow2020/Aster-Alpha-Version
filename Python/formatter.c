/* Aster 표준 포매터.
   AST만 보고 다시 소스를 만든다. 주석, 빈 줄, 원래 줄바꿈은 보존하지 않지만
   출력은 항상 같은 모양이라 다시 포매팅해도 결과가 변하지 않는다. */
#include "internal/formatter.h"
#include <string.h>
#include "internal/object.h"

/* 파서의 우선순위 단계. 숫자가 클수록 더 단단히 묶인다. */
enum {
    P_LAMBDA = 0,
    P_TERNARY = 1,
    P_PIPE = 2,
    P_COALESCE = 3,
    P_OR = 4,
    P_AND = 5,
    P_NOT = 6,
    P_CMP = 7,
    P_RANGE = 8,
    P_ADD = 9,
    P_MUL = 10,
    P_UNARY = 11,
    P_POW = 12,
    P_POSTFIX = 13,
    P_PRIMARY = 14
};

static void fmt_expr(FILE *out, expr_ty e, int min_prec);
static void fmt_stmt(FILE *out, stmt_ty s, int indent);
static void fmt_type(FILE *out, typeref_ty t);
static void fmt_ret(FILE *out, ret_ty r);

/* 'export ' 뒤에 이어 쓸 때처럼 이미 줄 앞부분을 출력했다면 들여쓰기를 건너뛴다. */
static int g_skip_indent;

static void ind(FILE *out, int indent) {
    int i;
    if (g_skip_indent) {
        g_skip_indent = 0;
        return;
    }
    for (i = 0; i < indent; i++)
        fputs("    ", out);
}

static const char *access_str(access_ty a) {
    switch (a) {
    case Public: return "public ";
    case Protected: return "protected ";
    case Private: return "private ";
    case Internal: return "internal ";
    default: return "";
    }
}

static void fmt_name_list(FILE *out, asdl_seq *names) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(names); i++) {
        if (i)
            fputs(", ", out);
        fputs((const char *)asdl_seq_GET(names, i), out);
    }
}

static void fmt_dotted(FILE *out, asdl_seq *parts) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(parts); i++) {
        if (i)
            fputc('.', out);
        fputs((const char *)asdl_seq_GET(parts, i), out);
    }
}

static void fmt_type_params(FILE *out, asdl_seq *type_params) {
    size_t i;
    if (asdl_seq_LEN(type_params) == 0)
        return;
    fputc('<', out);
    for (i = 0; i < asdl_seq_LEN(type_params); i++) {
        type_param_ty tp = asdl_seq_GET(type_params, i);
        if (i)
            fputs(", ", out);
        fputs(tp->name, out);
        if (tp->bound) {
            fputs(" extends ", out);
            fmt_type(out, tp->bound);
        }
    }
    fputc('>', out);
}

static void fmt_type(FILE *out, typeref_ty t) {
    size_t i;

    if (!t) {
        fputs("null", out);
        return;
    }
    switch (t->kind) {
    case Primitive_kind:
        switch (t->v.Primitive.kind) {
        case IntT: fputs("int", out); break;
        case FloatT: fputs("float", out); break;
        case BoolT: fputs("bool", out); break;
        case StrT: fputs("str", out); break;
        case NullT: fputs("null", out); break;
        }
        break;
    case Named_kind:
        fputs(t->v.Named.name, out);
        if (asdl_seq_LEN(t->v.Named.args) > 0) {
            fputc('<', out);
            for (i = 0; i < asdl_seq_LEN(t->v.Named.args); i++) {
                if (i)
                    fputs(", ", out);
                fmt_type(out, (typeref_ty)asdl_seq_GET(t->v.Named.args, i));
            }
            fputc('>', out);
        }
        break;
    case StructRef_kind:
        fputs("struct ", out);
        fputs(t->v.StructRef.tag, out);
        break;
    case Pointer_kind:
        fmt_type(out, t->v.Pointer.base);
        fputc('*', out);
        break;
    case Nullable_kind:
        fmt_type(out, t->v.Nullable.base);
        fputc('?', out);
        break;
    case UnionTy_kind:
        for (i = 0; i < asdl_seq_LEN(t->v.UnionTy.members); i++) {
            if (i)
                fputs(" | ", out);
            fmt_type(out, (typeref_ty)asdl_seq_GET(t->v.UnionTy.members, i));
        }
        break;
    case FunctionTy_kind:
        fputs("fn(", out);
        for (i = 0; i < asdl_seq_LEN(t->v.FunctionTy.params); i++) {
            if (i)
                fputs(", ", out);
            fmt_type(out, (typeref_ty)asdl_seq_GET(t->v.FunctionTy.params, i));
        }
        fputs(") -> ", out);
        fmt_ret(out, t->v.FunctionTy.returns);
        break;
    }
}

static void fmt_ret(FILE *out, ret_ty r) {
    if (!r || r->kind == VoidRet_kind)
        fputs("void", out);
    else
        fmt_type(out, r->v.ValueRet.ty);
}

static void fmt_params(FILE *out, asdl_seq *params) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(params); i++) {
        param_ty p = (param_ty)asdl_seq_GET(params, i);
        if (i)
            fputs(", ", out);
        if (p->variadic == 1)
            fputs("vararg ", out);
        else if (p->variadic == 2)
            fputs("varkwarg ", out);
        fmt_type(out, p->ty);
        fputc(' ', out);
        fputs(p->name, out);
        if (p->default_value) {
            fputs(" = ", out);
            fmt_expr(out, p->default_value, P_LAMBDA);
        }
    }
}

static void fmt_constant(FILE *out, Value v) {
    switch (v.tag) {
    case VT_NULL:
        fputs("null", out);
        break;
    case VT_BOOL:
        fputs(v.u.i ? "true" : "false", out);
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
            fputs("null", out);
        }
        break;
    }
}

/* f-string 안의 리터럴 조각은 '{'/'}'를 두 번 써야 하고 따옴표는 이스케이프한다. */
static void fmt_fstring_text(FILE *out, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        size_t adv = 1;
        uint32_t cp = Aster_UTF8_Decode(s + i, n - i, &adv);
        if (adv == 1 && (unsigned char)s[i] >= 0x80) {
            fprintf(out, "\\x%02x", (unsigned char)s[i]);
            i++;
            continue;
        }
        if (cp == '"' || cp == '\\') {
            fputc('\\', out);
            fputc((int)cp, out);
        }
        else if (cp == '{') {
            fputs("{{", out);
        }
        else if (cp == '}') {
            fputs("}}", out);
        }
        else if (cp == '\n') {
            fputs("\\n", out);
        }
        else if (cp == '\r') {
            fputs("\\r", out);
        }
        else if (cp == '\t') {
            fputs("\\t", out);
        }
        else if (Aster_Str_NeedsEscape(cp)) {
            if (cp <= 0xff)
                fprintf(out, "\\x%02x", cp);
            else if (cp <= 0xffff)
                fprintf(out, "\\u%04x", cp);
            else
                fprintf(out, "\\U%08x", cp);
        }
        else {
            fwrite(s + i, 1, adv, out);
        }
        i += adv;
    }
}

static void fmt_fstring_spec(FILE *out, expr_ty spec) {
    if (spec && spec->kind == Constant_kind && spec->v.Constant.value.tag == VT_OBJ) {
        AsterString *s = (AsterString *)spec->v.Constant.value.u.o;
        size_t i;
        for (i = 0; i < s->length; i++) {
            char c = s->data[i];
            if (c == '"' || c == '\\')
                fputc('\\', out);
            fputc(c, out);
        }
    }
    else {
        fmt_expr(out, spec, 0);
    }
}

static void fmt_fstring(FILE *out, expr_ty e) {
    size_t i;
    fputs("f\"", out);
    for (i = 0; i < asdl_seq_LEN(e->v.FString.values); i++) {
        expr_ty part = (expr_ty)asdl_seq_GET(e->v.FString.values, i);
        if (part->kind == Constant_kind && part->v.Constant.value.tag == VT_OBJ) {
            AsterString *s = (AsterString *)part->v.Constant.value.u.o;
            fmt_fstring_text(out, s->data, s->length);
        }
        else if (part->kind == FormattedValue_kind) {
            fputc('{', out);
            fmt_expr(out, part->v.FormattedValue.value, 0);
            if (part->v.FormattedValue.conversion == 'r')
                fputs("!r", out);
            else if (part->v.FormattedValue.conversion == 's')
                fputs("!s", out);
            if (part->v.FormattedValue.format_spec) {
                fputc(':', out);
                fmt_fstring_spec(out, part->v.FormattedValue.format_spec);
            }
            fputc('}', out);
        }
    }
    fputc('"', out);
}

static void fmt_seq_exprs(FILE *out, asdl_seq *seq) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(seq); i++) {
        if (i)
            fputs(", ", out);
        fmt_expr(out, (expr_ty)asdl_seq_GET(seq, i), 0);
    }
}

static void fmt_argument(FILE *out, expr_ty e) {
    fmt_expr(out, e, 0);
}

static void fmt_binop_paren(FILE *out, expr_ty e, int min_prec) {
    fmt_expr(out, e, min_prec);
}

static void fmt_expr(FILE *out, expr_ty e, int min_prec) {
    size_t i;

    if (!e)
        return;
    switch (e->kind) {
    case Lambda_kind:
        if (min_prec > P_LAMBDA) {
            fputc('(', out);
            fmt_expr(out, e, P_LAMBDA);
            fputc(')', out);
            return;
        }
        if (asdl_seq_LEN(e->v.Lambda.params) == 1) {
            lparam_ty p = (lparam_ty)asdl_seq_GET(e->v.Lambda.params, 0);
            if (!p->ty) {
                fputs(p->name, out);
            }
            else {
                fputc('(', out);
                fmt_type(out, p->ty);
                fputc(' ', out);
                fputs(p->name, out);
                fputc(')', out);
            }
        }
        else {
            fputc('(', out);
            for (i = 0; i < asdl_seq_LEN(e->v.Lambda.params); i++) {
                lparam_ty p = (lparam_ty)asdl_seq_GET(e->v.Lambda.params, i);
                if (i)
                    fputs(", ", out);
                if (p->ty) {
                    fmt_type(out, p->ty);
                    fputc(' ', out);
                }
                fputs(p->name, out);
            }
            fputc(')', out);
        }
        fputs(" -> ", out);
        fmt_ret(out, e->v.Lambda.returns);
        fputs(" => ", out);
        fmt_expr(out, e->v.Lambda.body, P_PIPE);
        break;
    case Pipe_kind:
        if (min_prec > P_PIPE) {
            fputc('(', out);
            fmt_expr(out, e, P_PIPE);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Pipe.value, P_PIPE);
        fputs(" |> ", out);
        fmt_expr(out, e->v.Pipe.func, P_COALESCE);
        break;
    case Ternary_kind:
        if (min_prec > P_TERNARY) {
            fputc('(', out);
            fmt_expr(out, e, P_TERNARY);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Ternary.test, P_PIPE);
        fputs(" ? ", out);
        fmt_expr(out, e->v.Ternary.body, P_LAMBDA);
        fputs(" : ", out);
        fmt_expr(out, e->v.Ternary.orelse, P_TERNARY);
        break;
    case Coalesce_kind:
        if (min_prec > P_COALESCE) {
            fputc('(', out);
            fmt_expr(out, e, P_COALESCE);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Coalesce.left, P_OR);
        fputs(" ?? ", out);
        fmt_expr(out, e->v.Coalesce.right, P_COALESCE);
        break;
    case BoolOp_kind: {
        int prec = e->v.BoolOp.op == Or ? P_OR : P_AND;
        const char *text = e->v.BoolOp.op == Or ? " or " : " and ";
        if (min_prec > prec) {
            fputc('(', out);
            fmt_expr(out, e, prec);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.BoolOp.left, prec);
        fputs(text, out);
        fmt_expr(out, e->v.BoolOp.right, prec + 1);
        break;
    }
    case UnaryOp_kind:
        if (e->v.UnaryOp.op == Not) {
            if (min_prec > P_NOT) {
                fputc('(', out);
                fmt_expr(out, e, P_NOT);
                fputc(')', out);
                break;
            }
            /* 파서는 'not in'/'is not'을 UnaryOp(Not, In/Is)로 만든다. */
            if (e->v.UnaryOp.operand->kind == In_kind) {
                expr_ty in = e->v.UnaryOp.operand;
                fmt_expr(out, in->v.In.item, P_RANGE);
                fputs(" not in ", out);
                fmt_expr(out, in->v.In.container, P_RANGE);
                break;
            }
            if (e->v.UnaryOp.operand->kind == Is_kind) {
                expr_ty is = e->v.UnaryOp.operand;
                fmt_expr(out, is->v.Is.value, P_RANGE);
                fputs(" is not ", out);
                fmt_type(out, is->v.Is.ty);
                break;
            }
            fputs("not ", out);
            fmt_expr(out, e->v.UnaryOp.operand, P_NOT);
            break;
        }
        if (min_prec > P_UNARY) {
            fputc('(', out);
            fmt_expr(out, e, P_UNARY);
            fputc(')', out);
            break;
        }
        fputs(e->v.UnaryOp.op == USub ? "-" : "+", out);
        fmt_expr(out, e->v.UnaryOp.operand, P_UNARY);
        break;
    case BinOp_kind: {
        int prec;
        const char *text;
        switch (e->v.BinOp.op) {
        case Add: prec = P_ADD; text = " + "; break;
        case Sub: prec = P_ADD; text = " - "; break;
        case Mult: prec = P_MUL; text = " * "; break;
        case Div: prec = P_MUL; text = " / "; break;
        case FloorDiv: prec = P_MUL; text = " // "; break;
        case Mod: prec = P_MUL; text = " % "; break;
        default: prec = P_POW; text = " ** "; break;
        }
        if (min_prec > prec) {
            fputc('(', out);
            fmt_expr(out, e, prec);
            fputc(')', out);
            break;
        }
        if (e->v.BinOp.op == Pow) {
            /* '**'는 오른쪽 결합이다. */
            fmt_expr(out, e->v.BinOp.left, P_POSTFIX);
            fputs(text, out);
            fmt_expr(out, e->v.BinOp.right, P_UNARY);
        }
        else {
            fmt_expr(out, e->v.BinOp.left, prec);
            fputs(text, out);
            fmt_expr(out, e->v.BinOp.right, prec + 1);
        }
        break;
    }
    case Compare_kind: {
        const char *text;
        switch (e->v.Compare.op) {
        case Eq: text = " == "; break;
        case NotEq: text = " != "; break;
        case Lt: text = " < "; break;
        case LtE: text = " <= "; break;
        case Gt: text = " > "; break;
        default: text = " >= "; break;
        }
        if (min_prec > P_CMP) {
            fputc('(', out);
            fmt_expr(out, e, P_CMP);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Compare.left, P_RANGE);
        fputs(text, out);
        fmt_expr(out, e->v.Compare.right, P_RANGE);
        break;
    }
    case Is_kind:
        if (min_prec > P_CMP) {
            fputc('(', out);
            fmt_expr(out, e, P_CMP);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Is.value, P_RANGE);
        fputs(" is ", out);
        fmt_type(out, e->v.Is.ty);
        break;
    case In_kind:
        if (min_prec > P_CMP) {
            fputc('(', out);
            fmt_expr(out, e, P_CMP);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.In.item, P_RANGE);
        fputs(" in ", out);
        fmt_expr(out, e->v.In.container, P_RANGE);
        break;
    case Cast_kind:
        if (min_prec > P_UNARY) {
            fputc('(', out);
            fmt_expr(out, e, P_UNARY);
            fputc(')', out);
            break;
        }
        fputc('(', out);
        fmt_type(out, e->v.Cast.ty);
        fputc(')', out);
        fmt_expr(out, e->v.Cast.value, P_UNARY);
        break;
    case AddressOf_kind:
        if (min_prec > P_UNARY) {
            fputc('(', out);
            fmt_expr(out, e, P_UNARY);
            fputc(')', out);
            break;
        }
        fputc('&', out);
        fmt_expr(out, e->v.AddressOf.value, P_UNARY);
        break;
    case Deref_kind:
        if (min_prec > P_UNARY) {
            fputc('(', out);
            fmt_expr(out, e, P_UNARY);
            fputc(')', out);
            break;
        }
        fputc('*', out);
        fmt_expr(out, e->v.Deref.value, P_UNARY);
        break;
    case SizeOf_kind:
        fputs("sizeof(", out);
        fmt_type(out, e->v.SizeOf.ty);
        fputc(')', out);
        break;
    case Call_kind: {
        size_t j;
        fmt_binop_paren(out, e->v.Call.func, P_POSTFIX);
        fputc('(', out);
        for (i = 0; i < asdl_seq_LEN(e->v.Call.args); i++) {
            if (i)
                fputs(", ", out);
            fmt_expr(out, (expr_ty)asdl_seq_GET(e->v.Call.args, i), P_LAMBDA);
        }
        for (j = 0; j < asdl_seq_LEN(e->v.Call.keywords); j++) {
            keyword_ty kw = asdl_seq_GET(e->v.Call.keywords, j);
            if (i + j)
                fputs(", ", out);
            fputs(kw->name, out);
            fputs(" = ", out);
            fmt_expr(out, kw->value, P_LAMBDA);
        }
        fputc(')', out);
        break;
    }
    case New_kind:
        fputs("new ", out);
        fputs(e->v.New.cls, out);
        if (asdl_seq_LEN(e->v.New.type_args) > 0) {
            fputc('<', out);
            for (i = 0; i < asdl_seq_LEN(e->v.New.type_args); i++) {
                if (i)
                    fputs(", ", out);
                fmt_type(out, (typeref_ty)asdl_seq_GET(e->v.New.type_args, i));
            }
            fputc('>', out);
        }
        fputc('(', out);
        fmt_seq_exprs(out, e->v.New.args);
        fputc(')', out);
        break;
    case Attribute_kind:
        fmt_binop_paren(out, e->v.Attribute.value, P_POSTFIX);
        fputc('.', out);
        fputs(e->v.Attribute.attr, out);
        break;
    case PtrAttribute_kind:
        fmt_binop_paren(out, e->v.PtrAttribute.value, P_POSTFIX);
        fputs("->", out);
        fputs(e->v.PtrAttribute.attr, out);
        break;
    case OptionalAttribute_kind:
        fmt_binop_paren(out, e->v.OptionalAttribute.value, P_POSTFIX);
        fputs("?.", out);
        fputs(e->v.OptionalAttribute.attr, out);
        break;
    case Name_kind:
        fputs(e->v.Name.id, out);
        break;
    case Self_kind:
        fputs("self", out);
        break;
    case Constant_kind:
        fmt_constant(out, e->v.Constant.value);
        break;
    case Array_kind:
    case List_kind:
        fputc('[', out);
        for (i = 0; i < asdl_seq_LEN(e->kind == Array_kind
                                          ? e->v.Array.elements
                                          : e->v.List.elements);
             i++) {
            if (i)
                fputs(", ", out);
            fmt_argument(out, (expr_ty)asdl_seq_GET(e->kind == Array_kind
                                                    ? e->v.Array.elements
                                                    : e->v.List.elements, i));
        }
        fputc(']', out);
        break;
    case Tuple_kind:
        fputc('(', out);
        fmt_seq_exprs(out, e->v.Tuple.elements);
        if (asdl_seq_LEN(e->v.Tuple.elements) == 1)
            fputc(',', out);
        fputc(')', out);
        break;
    case Dict_kind:
        fputc('{', out);
        for (i = 0; i < asdl_seq_LEN(e->v.Dict.keys); i++) {
            expr_ty key = (expr_ty)asdl_seq_GET(e->v.Dict.keys, i);
            expr_ty value = (expr_ty)asdl_seq_GET(e->v.Dict.values, i);
            if (i)
                fputs(", ", out);
            if (key && key->kind == Spread_kind) {
                fputs("**", out);
                fmt_expr(out, key->v.Spread.value, 0);
            }
            else {
                fmt_expr(out, key, 0);
                fputs(": ", out);
                fmt_expr(out, value, 0);
            }
        }
        fputc('}', out);
        break;
    case Set_kind:
        fputc('{', out);
        for (i = 0; i < asdl_seq_LEN(e->v.Set.elements); i++) {
            if (i)
                fputs(", ", out);
            fmt_argument(out, (expr_ty)asdl_seq_GET(e->v.Set.elements, i));
        }
        fputc('}', out);
        break;
    case Spread_kind:
        fputc('*', out);
        fmt_expr(out, e->v.Spread.value, 0);
        break;
    case Range_kind:
        if (min_prec > P_RANGE) {
            fputc('(', out);
            fmt_expr(out, e, P_RANGE);
            fputc(')', out);
            break;
        }
        fmt_expr(out, e->v.Range.start, P_ADD);
        fputs("..", out);
        fmt_expr(out, e->v.Range.stop, P_ADD);
        if (e->v.Range.step) {
            fputs("..", out);
            fmt_expr(out, e->v.Range.step, P_ADD);
        }
        break;
    case Subscript_kind:
        fmt_binop_paren(out, e->v.Subscript.value, P_POSTFIX);
        fputc('[', out);
        if (e->v.Subscript.index->kind == Slice_kind) {
            expr_ty sl = e->v.Subscript.index;
            if (sl->v.Slice.lower)
                fmt_expr(out, sl->v.Slice.lower, 0);
            fputc(':', out);
            if (sl->v.Slice.upper)
                fmt_expr(out, sl->v.Slice.upper, 0);
            if (sl->v.Slice.step) {
                fputc(':', out);
                fmt_expr(out, sl->v.Slice.step, 0);
            }
        }
        else {
            fmt_expr(out, e->v.Subscript.index, 0);
        }
        fputc(']', out);
        break;
    case Slice_kind:
        if (e->v.Slice.lower)
            fmt_expr(out, e->v.Slice.lower, 0);
        fputc(':', out);
        if (e->v.Slice.upper)
            fmt_expr(out, e->v.Slice.upper, 0);
        if (e->v.Slice.step) {
            fputc(':', out);
            fmt_expr(out, e->v.Slice.step, 0);
        }
        break;
    case FString_kind:
        fmt_fstring(out, e);
        break;
    case FormattedValue_kind:
        fputc('{', out);
        fmt_expr(out, e->v.FormattedValue.value, 0);
        fputc('}', out);
        break;
    case ListComp_kind:
        fputc('[', out);
        fmt_expr(out, e->v.ListComp.elt, 0);
        fputs(" for ", out);
        fputs(e->v.ListComp.name, out);
        fputs(" in ", out);
        fmt_expr(out, e->v.ListComp.iter, 0);
        if (e->v.ListComp.cond) {
            fputs(" if ", out);
            fmt_expr(out, e->v.ListComp.cond, 0);
        }
        fputc(']', out);
        break;
    case SetComp_kind:
        fputc('{', out);
        fmt_expr(out, e->v.SetComp.elt, 0);
        fputs(" for ", out);
        fputs(e->v.SetComp.name, out);
        fputs(" in ", out);
        fmt_expr(out, e->v.SetComp.iter, 0);
        if (e->v.SetComp.cond) {
            fputs(" if ", out);
            fmt_expr(out, e->v.SetComp.cond, 0);
        }
        fputc('}', out);
        break;
    case DictComp_kind:
        fputc('{', out);
        fmt_expr(out, e->v.DictComp.key, 0);
        fputs(": ", out);
        fmt_expr(out, e->v.DictComp.value, 0);
        fputs(" for ", out);
        fputs(e->v.DictComp.name, out);
        fputs(" in ", out);
        fmt_expr(out, e->v.DictComp.iter, 0);
        if (e->v.DictComp.cond) {
            fputs(" if ", out);
            fmt_expr(out, e->v.DictComp.cond, 0);
        }
        fputc('}', out);
        break;
    case Incr_kind:
        if (e->v.Incr.is_prefix) {
            if (min_prec > P_UNARY) {
                fputc('(', out);
                fmt_expr(out, e, P_UNARY);
                fputc(')', out);
                break;
            }
            fputs(e->v.Incr.is_inc ? "++" : "--", out);
            fmt_expr(out, e->v.Incr.target, P_UNARY);
        }
        else {
            fmt_expr(out, e->v.Incr.target, P_POSTFIX);
            fputs(e->v.Incr.is_inc ? "++" : "--", out);
        }
        break;
    }
}

/* ------------------------------------------------------------- 초기화 */

static void fmt_initializer(FILE *out, initializer_ty init) {
    size_t i;
    if (!init)
        return;
    switch (init->kind) {
    case NoInit_kind:
        break;
    case ExprInit_kind:
        fputs(" = ", out);
        fmt_expr(out, init->v.ExprInit.value, 0);
        break;
    case PositionalInit_kind:
        fputs(" = {", out);
        for (i = 0; i < asdl_seq_LEN(init->v.PositionalInit.values); i++) {
            if (i)
                fputs(", ", out);
            fmt_expr(out, (expr_ty)asdl_seq_GET(init->v.PositionalInit.values, i), 0);
        }
        fputc('}', out);
        break;
    case DesignatedInit_kind:
        fputs(" = { ", out);
        for (i = 0; i < asdl_seq_LEN(init->v.DesignatedInit.items); i++) {
            designation_ty d = (designation_ty)asdl_seq_GET(init->v.DesignatedInit.items, i);
            if (i)
                fputs(", ", out);
            fputc('.', out);
            fputs(d->field, out);
            fputs(" = ", out);
            fmt_expr(out, d->value, 0);
        }
        fputs(" }", out);
        break;
    }
}

/* ------------------------------------------------------- 블록과 문장 */

static void fmt_block_body(FILE *out, asdl_seq *body, int indent) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(body); i++)
        fmt_stmt(out, (stmt_ty)asdl_seq_GET(body, i), indent);
}

/* "{\n <body> <indent>}" 를 출력하고 줄바꿈은 하지 않는다. */
static void fmt_block_no_nl(FILE *out, asdl_seq *body, int indent) {
    fputs("{\n", out);
    fmt_block_body(out, body, indent + 1);
    ind(out, indent);
    fputc('}', out);
}

static void fmt_if(FILE *out, stmt_ty s, int indent, int leading) {
    if (leading)
        ind(out, indent);
    fputs("if (", out);
    fmt_expr(out, s->v.If.test, 0);
    fputs(") ", out);
    fmt_block_no_nl(out, s->v.If.body, indent);
    if (asdl_seq_LEN(s->v.If.orelse) == 1) {
        stmt_ty only = (stmt_ty)asdl_seq_GET(s->v.If.orelse, 0);
        if (only->kind == If_kind) {
            fputs(" else ", out);
            fmt_if(out, only, indent, 0);
            return;
        }
    }
    if (asdl_seq_LEN(s->v.If.orelse) > 0) {
        fputs(" else ", out);
        fmt_block_no_nl(out, s->v.If.orelse, indent);
    }
    fputc('\n', out);
}

static const char *assign_op_str(assign_op_ty op) {
    switch (op) {
    case AssignAdd: return "+=";
    case AssignSub: return "-=";
    case AssignMult: return "*=";
    case AssignDiv: return "/=";
    case AssignFloorDiv: return "//=";
    case AssignMod: return "%=";
    case AssignPow: return "**=";
    default: return "=";
    }
}

/* for(...) 안쪽처럼 문장 하나를 세미콜론 없이 출력한다. */
static void fmt_inline_stmt(FILE *out, stmt_ty s) {
    switch (s->kind) {
    case VarDecl_kind:
        if (s->v.VarDecl.is_const)
            fputs("const ", out);
        fmt_type(out, s->v.VarDecl.ty);
        fputc(' ', out);
        fputs(s->v.VarDecl.name, out);
        fmt_initializer(out, s->v.VarDecl.init);
        break;
    case Destructure_kind:
        fputs("var (", out);
        fmt_name_list(out, s->v.Destructure.names);
        fputs(") = ", out);
        fmt_expr(out, s->v.Destructure.value, 0);
        break;
    case Assign_kind:
        fmt_expr(out, s->v.Assign.target, 0);
        fputc(' ', out);
        fputs(assign_op_str(s->v.Assign.op), out);
        fputc(' ', out);
        fmt_expr(out, s->v.Assign.value, 0);
        break;
    case ExprStmt_kind:
        fmt_expr(out, s->v.ExprStmt.value, 0);
        break;
    default:
        break;
    }
}

static void fmt_pattern(FILE *out, match_pattern_ty p) {
    size_t i;
    switch (p->kind) {
    case PLiteral_kind:
        fmt_expr(out, p->v.PLiteral.value, 0);
        break;
    case PBind_kind:
        fputs(p->v.PBind.name, out);
        break;
    case PVariant_kind:
        fmt_expr(out, p->v.PVariant.path, P_POSTFIX);
        if (asdl_seq_LEN(p->v.PVariant.items) > 0) {
            fputc('(', out);
            for (i = 0; i < asdl_seq_LEN(p->v.PVariant.items); i++) {
                if (i)
                    fputs(", ", out);
                fmt_pattern(out, (match_pattern_ty)asdl_seq_GET(p->v.PVariant.items, i));
            }
            fputc(')', out);
        }
        break;
    case PList_kind:
        fputc('[', out);
        for (i = 0; i < asdl_seq_LEN(p->v.PList.items); i++) {
            if (i)
                fputs(", ", out);
            fmt_pattern(out, (match_pattern_ty)asdl_seq_GET(p->v.PList.items, i));
        }
        if (p->v.PList.rest) {
            if (i)
                fputs(", ", out);
            fputc('*', out);
            fputs(p->v.PList.rest, out);
        }
        fputc(']', out);
        break;
    case PDict_kind:
        fputc('{', out);
        for (i = 0; i < asdl_seq_LEN(p->v.PDict.items); i++) {
            pdict_item_ty d = asdl_seq_GET(p->v.PDict.items, i);
            if (i)
                fputs(", ", out);
            fmt_expr(out, d->key, P_LAMBDA);
            fputs(": ", out);
            fmt_pattern(out, d->value);
        }
        if (p->v.PDict.rest) {
            if (i)
                fputs(", ", out);
            fputs("**", out);
            fputs(p->v.PDict.rest, out);
        }
        fputc('}', out);
        break;
    }
}

static void fmt_class_member(FILE *out, member_ty m, int indent) {
    ind(out, indent);
    switch (m->kind) {
    case Field_kind:
        fputs(access_str(m->v.Field.access), out);
        if (m->v.Field.is_static)
            fputs("static ", out);
        fmt_type(out, m->v.Field.ty);
        fputc(' ', out);
        fputs(m->v.Field.name, out);
        if (m->v.Field.value) {
            fputs(" = ", out);
            fmt_expr(out, m->v.Field.value, 0);
        }
        fputs(";\n", out);
        break;
    case Method_kind:
        fputs(access_str(m->v.Method.access), out);
        if (m->v.Method.is_static)
            fputs("static ", out);
        fputs("fn ", out);
        fputs(m->v.Method.name, out);
        fmt_type_params(out, m->v.Method.type_params);
        fputc('(', out);
        fmt_params(out, m->v.Method.params);
        fputs(") -> ", out);
        fmt_ret(out, m->v.Method.returns);
        fputc(' ', out);
        fmt_block_no_nl(out, m->v.Method.body, indent);
        fputc('\n', out);
        break;
    case Constructor_kind:
        fputs("constructor(", out);
        fmt_params(out, m->v.Constructor.params);
        fputs(") ", out);
        fmt_block_no_nl(out, m->v.Constructor.body, indent);
        fputc('\n', out);
        break;
    case Property_kind:
        fputs(access_str(m->v.Property.access), out);
        fputs("@property ", out);
        fmt_type(out, m->v.Property.ty);
        fputc(' ', out);
        fputs(m->v.Property.name, out);
        fputs(";\n", out);
        break;
    case Getter_kind:
        fputs("getter ", out);
        fputs(m->v.Getter.name, out);
        fputc(' ', out);
        fmt_block_no_nl(out, m->v.Getter.body, indent);
        fputc('\n', out);
        break;
    case Setter_kind:
        fputs("setter ", out);
        fputs(m->v.Setter.name, out);
        fputc('(', out);
        fmt_type(out, m->v.Setter.param->ty);
        fputc(' ', out);
        fputs(m->v.Setter.param->name, out);
        fputs(") ", out);
        fmt_block_no_nl(out, m->v.Setter.body, indent);
        fputc('\n', out);
        break;
    }
}

static void fmt_struct_member(FILE *out, struct_member_ty m, const char *tag, int indent) {
    size_t i;
    ind(out, indent);
    switch (m->kind) {
    case StructField_kind:
        fmt_type(out, m->v.StructField.ty);
        fputc(' ', out);
        fputs(m->v.StructField.name, out);
        fputs(";\n", out);
        break;
    case StructCtor_kind:
        fputs(tag, out);
        fputc('(', out);
        fmt_params(out, m->v.StructCtor.params);
        fputc(')', out);
        if (asdl_seq_LEN(m->v.StructCtor.inits) > 0) {
            fputs(": ", out);
            for (i = 0; i < asdl_seq_LEN(m->v.StructCtor.inits); i++) {
                member_init_ty mi = (member_init_ty)asdl_seq_GET(m->v.StructCtor.inits, i);
                if (i)
                    fputs(", ", out);
                fputs(mi->field, out);
                fputc('(', out);
                fmt_expr(out, mi->value, 0);
                fputc(')', out);
            }
        }
        fputc(' ', out);
        fmt_block_no_nl(out, m->v.StructCtor.body, indent);
        fputc('\n', out);
        break;
    }
}

static void fmt_member_list(FILE *out, asdl_seq *members, int indent) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(members); i++)
        fmt_class_member(out, (member_ty)asdl_seq_GET(members, i), indent);
}

static void fmt_stmt(FILE *out, stmt_ty s, int indent) {
    size_t i;

    if (!s)
        return;
    switch (s->kind) {
    case FunctionDef_kind:
        ind(out, indent);
        if (s->v.FunctionDef.is_extern) {
            fputs("extern fn ", out);
            fputs(s->v.FunctionDef.name, out);
            fmt_type_params(out, s->v.FunctionDef.type_params);
            fputc('(', out);
            fmt_params(out, s->v.FunctionDef.params);
            fputs(") -> ", out);
            fmt_ret(out, s->v.FunctionDef.returns);
            fputs(";\n", out);
            break;
        }
        fputs("fn ", out);
        fputs(s->v.FunctionDef.name, out);
        fmt_type_params(out, s->v.FunctionDef.type_params);
        fputc('(', out);
        fmt_params(out, s->v.FunctionDef.params);
        fputs(") -> ", out);
        fmt_ret(out, s->v.FunctionDef.returns);
        fputc(' ', out);
        fmt_block_no_nl(out, s->v.FunctionDef.body, indent);
        fputc('\n', out);
        break;
    case ClassDef_kind:
        ind(out, indent);
        fputs("class ", out);
        fputs(s->v.ClassDef.name, out);
        fmt_type_params(out, s->v.ClassDef.type_params);
        if (s->v.ClassDef.base) {
            fputs(" extends ", out);
            fmt_type(out, s->v.ClassDef.base);
        }
        if (asdl_seq_LEN(s->v.ClassDef.interfaces) > 0) {
            fputs(" implements ", out);
            for (i = 0; i < asdl_seq_LEN(s->v.ClassDef.interfaces); i++) {
                if (i)
                    fputs(", ", out);
                fmt_type(out, (typeref_ty)asdl_seq_GET(s->v.ClassDef.interfaces, i));
            }
        }
        fputs(" {\n", out);
        fmt_member_list(out, s->v.ClassDef.members, indent + 1);
        ind(out, indent);
        fputs("}\n", out);
        break;
    case InterfaceDef_kind:
        ind(out, indent);
        fputs("interface ", out);
        fputs(s->v.InterfaceDef.name, out);
        fmt_type_params(out, s->v.InterfaceDef.type_params);
        fputs(" {\n", out);
        for (i = 0; i < asdl_seq_LEN(s->v.InterfaceDef.methods); i++) {
            signature_ty m = (signature_ty)asdl_seq_GET(s->v.InterfaceDef.methods, i);
            ind(out, indent + 1);
            fputs(access_str(m->access), out);
            fputs("fn ", out);
            fputs(m->name, out);
            fputc('(', out);
            fmt_params(out, m->params);
            fputs(") -> ", out);
            fmt_ret(out, m->returns);
            fputs(";\n", out);
        }
        ind(out, indent);
        fputs("}\n", out);
        break;
    case StructDef_kind:
        ind(out, indent);
        if (s->v.StructDef.alias) {
            fputs("typedef struct ", out);
            fputs(s->v.StructDef.tag, out);
            fputs(" {\n", out);
            for (i = 0; i < asdl_seq_LEN(s->v.StructDef.members); i++)
                fmt_struct_member(out, (struct_member_ty)asdl_seq_GET(
                                           s->v.StructDef.members, i),
                                  s->v.StructDef.tag, indent + 1);
            ind(out, indent);
            fputs("} ", out);
            fputs(s->v.StructDef.alias, out);
            fputs(";\n", out);
        }
        else {
            fputs("struct ", out);
            fputs(s->v.StructDef.tag, out);
            fputs(" {\n", out);
            for (i = 0; i < asdl_seq_LEN(s->v.StructDef.members); i++)
                fmt_struct_member(out, (struct_member_ty)asdl_seq_GET(
                                           s->v.StructDef.members, i),
                                  s->v.StructDef.tag, indent + 1);
            ind(out, indent);
            fputs("};\n", out);
        }
        break;
    case VarDecl_kind:
        ind(out, indent);
        if (s->v.VarDecl.is_const)
            fputs("const ", out);
        fmt_type(out, s->v.VarDecl.ty);
        fputc(' ', out);
        fputs(s->v.VarDecl.name, out);
        fmt_initializer(out, s->v.VarDecl.init);
        fputs(";\n", out);
        break;
    case EnumDef_kind: {
        size_t nmethods = asdl_seq_LEN(s->v.EnumDef.methods);
        ind(out, indent);
        fputs("enum ", out);
        fputs(s->v.EnumDef.name, out);
        fputs(" { ", out);
        for (i = 0; i < asdl_seq_LEN(s->v.EnumDef.members); i++) {
            enum_member_ty m = (enum_member_ty)asdl_seq_GET(s->v.EnumDef.members, i);
            if (i)
                fputs(", ", out);
            fputs(m->name, out);
            if (asdl_seq_LEN(m->params) > 0) {
                fputc('(', out);
                fmt_params(out, m->params);
                fputc(')', out);
            }
            if (m->value) {
                fputs(" = ", out);
                fmt_expr(out, m->value, 0);
            }
        }
        if (nmethods == 0) {
            fputs(" }\n", out);
            break;
        }
        fputs(";\n", out);
        for (i = 0; i < nmethods; i++)
            fmt_class_member(out, (member_ty)asdl_seq_GET(s->v.EnumDef.methods, i),
                             indent + 1);
        ind(out, indent);
        fputs("}\n", out);
        break;
    }
    case Destructure_kind:
        ind(out, indent);
        fputs("var (", out);
        fmt_name_list(out, s->v.Destructure.names);
        fputs(") = ", out);
        fmt_expr(out, s->v.Destructure.value, 0);
        fputs(";\n", out);
        break;
    case Assert_kind:
        ind(out, indent);
        fputs("assert ", out);
        fmt_expr(out, s->v.Assert.test, 0);
        if (s->v.Assert.message) {
            fputs(", ", out);
            fmt_expr(out, s->v.Assert.message, 0);
        }
        fputs(";\n", out);
        break;
    case Assign_kind:
        ind(out, indent);
        fmt_expr(out, s->v.Assign.target, 0);
        fputc(' ', out);
        fputs(assign_op_str(s->v.Assign.op), out);
        fputc(' ', out);
        fmt_expr(out, s->v.Assign.value, 0);
        fputs(";\n", out);
        break;
    case If_kind:
        fmt_if(out, s, indent, 1);
        break;
    case While_kind:
        ind(out, indent);
        fputs("while (", out);
        fmt_expr(out, s->v.While.test, 0);
        fputs(") ", out);
        fmt_block_no_nl(out, s->v.While.body, indent);
        fputc('\n', out);
        break;
    case For_kind:
        ind(out, indent);
        fputs("for (", out);
        fmt_inline_stmt(out, s->v.For.init);
        fputs("; ", out);
        fmt_expr(out, s->v.For.test, 0);
        fputs("; ", out);
        fmt_inline_stmt(out, s->v.For.step);
        fputs(") ", out);
        fmt_block_no_nl(out, s->v.For.body, indent);
        fputc('\n', out);
        break;
    case ForRange_kind:
        ind(out, indent);
        fputs("for (", out);
        fmt_type(out, s->v.ForRange.ty);
        fputc(' ', out);
        fputs(s->v.ForRange.name, out);
        fputs("; ", out);
        fputs(s->v.ForRange.name, out);
        fputs(" = ", out);
        fmt_expr(out, s->v.ForRange.iter->start, P_ADD);
        fputs("..", out);
        fmt_expr(out, s->v.ForRange.iter->stop, P_ADD);
        if (s->v.ForRange.iter->step) {
            fputs("..", out);
            fmt_expr(out, s->v.ForRange.iter->step, P_ADD);
        }
        fputs(") ", out);
        fmt_block_no_nl(out, s->v.ForRange.body, indent);
        fputc('\n', out);
        break;
    case ForIn_kind:
        ind(out, indent);
        fputs("for (", out);
        fmt_type(out, s->v.ForIn.ty);
        fputc(' ', out);
        fputs(s->v.ForIn.name, out);
        fputs(" in ", out);
        fmt_expr(out, s->v.ForIn.iter, 0);
        fputs(") ", out);
        fmt_block_no_nl(out, s->v.ForIn.body, indent);
        fputc('\n', out);
        break;
    case Switch_kind:
        ind(out, indent);
        fputs("switch (", out);
        fmt_expr(out, s->v.Switch.subject, 0);
        fputs(") {\n", out);
        for (i = 0; i < asdl_seq_LEN(s->v.Switch.cases); i++) {
            match_case_ty c = (match_case_ty)asdl_seq_GET(s->v.Switch.cases, i);
            ind(out, indent + 1);
            if (!c->guard && c->pattern->kind == PBind_kind &&
                strcmp(c->pattern->v.PBind.name, "_") == 0) {
                fputs("default ", out);
            }
            else {
                fputs("case ", out);
                fmt_pattern(out, c->pattern);
                if (c->guard) {
                    fputs(" when ", out);
                    fmt_expr(out, c->guard, P_LAMBDA);
                }
                fputc(' ', out);
            }
            fmt_block_no_nl(out, c->body, indent + 1);
            fputc('\n', out);
        }
        ind(out, indent);
        fputs("}\n", out);
        break;
    case Try_kind:
        ind(out, indent);
        fputs("try ", out);
        fmt_block_no_nl(out, s->v.Try.body, indent);
        for (i = 0; i < asdl_seq_LEN(s->v.Try.handlers); i++) {
            handler_ty h = (handler_ty)asdl_seq_GET(s->v.Try.handlers, i);
            fputs(" catch (", out);
            fmt_type(out, h->ty);
            fputc(' ', out);
            fputs(h->name, out);
            fputs(") ", out);
            fmt_block_no_nl(out, h->body, indent);
        }
        if (asdl_seq_LEN(s->v.Try.finalbody) > 0) {
            fputs(" finally ", out);
            fmt_block_no_nl(out, s->v.Try.finalbody, indent);
        }
        fputc('\n', out);
        break;
    case Yield_kind:
        ind(out, indent);
        fputs("yield", out);
        if (s->v.Yield.value) {
            fputc(' ', out);
            fmt_expr(out, s->v.Yield.value, 0);
        }
        fputs(";\n", out);
        break;
    case Return_kind:
        ind(out, indent);
        fputs("return", out);
        if (s->v.Return.value) {
            fputc(' ', out);
            fmt_expr(out, s->v.Return.value, 0);
        }
        fputs(";\n", out);
        break;
    case Throw_kind:
        ind(out, indent);
        fputs("throw ", out);
        fmt_expr(out, s->v.Throw.value, 0);
        if (s->v.Throw.exc_type) {
            fputs(" as ", out);
            fputs(s->v.Throw.exc_type, out);
        }
        fputs(";\n", out);
        break;
    case Break_kind:
        ind(out, indent);
        fputs("break;\n", out);
        break;
    case Continue_kind:
        ind(out, indent);
        fputs("continue;\n", out);
        break;
    case Import_kind:
        ind(out, indent);
        fputs("import ", out);
        fmt_dotted(out, s->v.Import.path);
        if (s->v.Import.wildcard)
            fputs(".*", out);
        else if (s->v.Import.alias) {
            fputs(" as ", out);
            fputs(s->v.Import.alias, out);
        }
        fputs(";\n", out);
        break;
    case ImportFrom_kind: {
        asdl_seq *names = s->v.ImportFrom.names;
        ind(out, indent);
        fputs("from ", out);
        fmt_dotted(out, s->v.ImportFrom.module);
        fputs(" import ", out);
        if (asdl_seq_LEN(names) == 0) {
            fputc('*', out);
        }
        else {
            for (size_t i = 0; i < asdl_seq_LEN(names); i++) {
                import_name_ty item = asdl_seq_GET(names, i);
                if (i)
                    fputs(", ", out);
                fputs(item->name, out);
                if (item->alias) {
                    fputs(" as ", out);
                    fputs(item->alias, out);
                }
            }
        }
        fputs(";\n", out);
        break;
    }
    case Export_kind:
        ind(out, indent);
        fputs("export ", out);
        g_skip_indent = 1;
        fmt_stmt(out, s->v.Export.body, indent);
        break;
    case ExceptionDef_kind:
        ind(out, indent);
        fputs("exception ", out);
        fputs(s->v.ExceptionDef.name, out);
        if (s->v.ExceptionDef.base) {
            fputs(" extends ", out);
            fputs(s->v.ExceptionDef.base, out);
        }
        fputs(";\n", out);
        break;
    case ExprStmt_kind:
        ind(out, indent);
        fmt_expr(out, s->v.ExprStmt.value, 0);
        fputs(";\n", out);
        break;
    }
}

void Aster_FormatModule(FILE *out, mod_ty mod) {
    size_t i;

    if (!mod)
        return;
    if (asdl_seq_LEN(mod->v.Module.package) > 0) {
        fputs("package ", out);
        fmt_dotted(out, mod->v.Module.package);
        fputs(";\n\n", out);
    }
    for (i = 0; i < asdl_seq_LEN(mod->v.Module.body); i++)
        fmt_stmt(out, (stmt_ty)asdl_seq_GET(mod->v.Module.body, i), 0);
}
