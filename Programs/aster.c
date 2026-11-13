#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
/* fileno/isatty 선언을 위해 시스템 헤더보다 먼저 정의한다. */
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include "internal/arena.h"
#include "internal/ast.h"
#include "internal/formatter.h"
#include "internal/interp.h"
#include "internal/lexer.h"
#include "internal/linter.h"
#include "internal/parser.h"
#include "internal/pkg.h"
#include "internal/runtime.h"
#include "internal/typecheck.h"

#define ASTER_VERSION "Alpha 1.3.3"

static char *read_stream(FILE *f, size_t *size) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap + 1);

    if (!buf) {
        fputs("out of memory\n", stderr);
        return NULL;
    }
    for (;;) {
        size_t n;
        if (len == cap) {
            char *grown;
            cap *= 2;
            grown = realloc(buf, cap + 1);
            if (!grown) {
                free(buf);
                fputs("out of memory\n", stderr);
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
        return NULL;
    }
    buf[len] = 0;
    *size = len;
    return buf;
}

static char *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    char *buf;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return NULL;
    }
    buf = read_stream(f, size);
    if (!buf)
        fprintf(stderr, "cannot read %s\n", path);
    fclose(f);
    return buf;
}

static void print_error(const char *filename, const char *source, const AsterError *err) {
    const char *p = source;
    const char *line_start, *line_end;
    int line = 1;
    int i;

    fprintf(stderr, "%s:%d:%d: error: %s\n", filename, err->line, err->col + 1, err->message);

    while (*p && line < err->line) {
        if (*p == '\n')
            line++;
        p++;
    }
    line_start = p;
    line_end = strchr(line_start, '\n');
    if (!line_end)
        line_end = line_start + strlen(line_start);
    fprintf(stderr, "%.*s\n", (int)(line_end - line_start), line_start);

    for (i = 0; i < err->col; i++)
        fputc(' ', stderr);
    fputc('^', stderr);
    for (i = 1; i < err->length; i++)
        fputc('^', stderr);
    fputc('\n', stderr);
}

static void dump_tokens(const AsterTokenList *list) {
    size_t i;
    for (i = 0; i < list->count; i++) {
        const AsterToken *t = &list->items[i];
        printf("%d:%d-%d:%d %s", t->line, t->col, t->end_line, t->end_col,
               _AsterToken_Names[t->type]);
        if (t->length > 0)
            printf(" %.*s", t->length, t->start);
        putchar('\n');
    }
}

static void print_uncaught(const char *filename, Interp *in) {
    RValue *exc = in->exc;

    Interp_PrintTraceback(stderr, in);
    if (exc && exc->kind == RV_EXC) {
        fprintf(stderr, "%s:%d: error: %s: ", filename, in->line, exc->u.exc->name);
        RValue_Print(stderr, exc->u.exc->message, 0);
        fputc('\n', stderr);
    }
    else {
        fprintf(stderr, "%s:%d: error: Exception\n", filename, in->line);
    }
}

static int directory_exists(const char *path) {
    struct stat st;
    if (!path || !path[0])
        return 0;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *dir_of_file(const char *path, Arena *arena) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *cut = slash > bslash ? slash : bslash;
    size_t len;

    if (!cut)
        return Arena_Strndup(arena, ".", 1);
    len = (size_t)(cut - path);
    if (len == 0)
        len = 1;
    return Arena_Strndup(arena, path, len);
}

static const char *find_stdlib_dir(Arena *arena) {
    const char *home = getenv("ASTER_HOME");
    char buf[1024];

    if (home && home[0]) {
        snprintf(buf, sizeof buf, "%s/Lib", home);
        if (directory_exists(buf))
            return Arena_Strndup(arena, buf, strlen(buf));
    }
    if (directory_exists("Lib"))
        return Arena_Strndup(arena, "Lib", 3);
    return NULL;
}

static int stdin_is_tty(void) {
#if defined(_WIN32)
    return _isatty(_fileno(stdin));
#else
    return isatty(fileno(stdin));
#endif
}

static int repl_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t want = *cap ? *cap : 1024;
        char *grown;
        while (want < *len + n + 1)
            want *= 2;
        grown = realloc(*buf, want);
        if (!grown)
            return 0;
        *buf = grown;
        *cap = want;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = 0;
    return 1;
}

/* REPL에서 같은 이름을 다시 정의할 수 있도록 이전 바인딩을 지운다. */
static void repl_clear_decl(Interp *interp, stmt_ty s) {
    const char *name = NULL;

    switch (s->kind) {
    case FunctionDef_kind: name = s->v.FunctionDef.name; break;
    case ClassDef_kind: name = s->v.ClassDef.name; break;
    case StructDef_kind: name = s->v.StructDef.tag; break;
    case InterfaceDef_kind: name = s->v.InterfaceDef.name; break;
    case ExceptionDef_kind: name = s->v.ExceptionDef.name; break;
    case EnumDef_kind: name = s->v.EnumDef.name; break;
    case Export_kind:
        repl_clear_decl(interp, s->v.Export.body);
        return;
    default:
        return;
    }
    if (name)
        Env_RemoveLocal(interp, interp->globals, name);
}

static void repl_clear_module_defs(Interp *interp, mod_ty mod) {
    size_t i;
    for (i = 0; i < asdl_seq_LEN(mod->v.Module.body); i++)
        repl_clear_decl(interp, asdl_seq_GET(mod->v.Module.body, i));
}

static char *trim_start(char *s) {
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static int repl_command(const char *line) {
    if (!strcmp(line, ":quit") || !strcmp(line, ":q") || !strcmp(line, ":exit"))
        return 1;
    if (!strcmp(line, ":help")) {
        puts("  :help   show this help");
        puts("  :quit   leave the REPL (also Ctrl-D)");
        puts("  multi-line input continues until the block is complete");
        return 0;
    }
    printf("unknown command: %s (try :help)\n", line);
    return 0;
}

static void print_repl_error(Interp *interp) {
    RValue *exc = interp->exc;

    Interp_PrintTraceback(stderr, interp);
    if (exc && exc->kind == RV_EXC) {
        fprintf(stderr, "Error: %s: ", exc->u.exc->name);
        RValue_Print(stderr, exc->u.exc->message, 0);
        fputc('\n', stderr);
    }
    else {
        fputs("Error: Exception\n", stderr);
    }
}

/* REPL은 줄 끝 세미콜론을 생략할 수 있다: 그 오류면 ';'를 붙여 다시 파싱한다. */
static mod_ty repl_parse(Interp *interp, const char *buf, size_t len, AsterError *err,
                         int *incomplete) {
    mod_ty mod = Aster_ParseFile(buf, len, interp->arena, err);

    *incomplete = 0;
    if (mod)
        return mod;
    if (strstr(err->message, "unexpected end of file")) {
        *incomplete = 1;
        return NULL;
    }
    if (strstr(err->message, "missing ';'")) {
        char *tmp = malloc(len + 2);
        if (!tmp)
            return NULL;
        memcpy(tmp, buf, len);
        tmp[len] = ';';
        tmp[len + 1] = 0;
        mod = Aster_ParseFile(tmp, len + 1, interp->arena, err);
        free(tmp);
        if (mod)
            return mod;
        if (strstr(err->message, "unexpected end of file")) {
            *incomplete = 1;
            return NULL;
        }
    }
    return NULL;
}

static int run_repl(Interp *interp) {
    char *buf = NULL;
    size_t len = 0, cap = 0;
    char line[4096];
    int interactive = stdin_is_tty();

    if (interactive)
        printf("Aster %s REPL. Type :help for help, :quit to exit.\n", ASTER_VERSION);

    /* 편의를 위해 콘솔 이름을 미리 가져온다 (파일 실행 의미론은 그대로). */
    {
        static const char prelude[] = "import System.stdout.*;";
        AsterError perr;
        mod_ty pmod = Aster_ParseFile(prelude, sizeof prelude - 1, interp->arena, &perr);
        if (pmod) {
            interp->repl = 0;
            Interp_RunModule(interp, pmod);
            interp->sig = SIG_OK;
            interp->exc = NULL;
        }
    }

    for (;;) {
        AsterError err;
        mod_ty mod;
        size_t i;
        int blank = 1;

        if (interactive) {
            fputs(len ? "... " : ">>> ", stdout);
            fflush(stdout);
        }
        if (!fgets(line, sizeof line, stdin)) {
            if (interactive)
                fputc('\n', stdout);
            break;
        }
        if (len == 0) {
            char *cmd = trim_start(line);
            if (cmd[0] == ':') {
                size_t n = strlen(cmd);
                while (n && (cmd[n - 1] == '\n' || cmd[n - 1] == '\r'))
                    cmd[--n] = 0;
                if (repl_command(cmd))
                    break;
                continue;
            }
        }
        if (!repl_append(&buf, &len, &cap, line, strlen(line))) {
            fputs("out of memory\n", stderr);
            break;
        }
        for (i = 0; i < len; i++)
            if (!isspace((unsigned char)buf[i]))
                blank = 0;
        if (blank) {
            len = 0;
            continue;
        }
        {
            int incomplete = 0;
            mod = repl_parse(interp, buf, len, &err, &incomplete);
            if (!mod) {
                if (incomplete)
                    continue;   /* 블록이 아직 안 닫혔다: 다음 줄을 더 받는다 */
                print_error("<repl>", buf, &err);
                len = 0;
                continue;
            }
        }
        interp->repl = 1;
        interp->sig = SIG_OK;
        interp->exc = NULL;
        interp->ret = NULL;
        interp->nframes = 0;
        repl_clear_module_defs(interp, mod);
        if (Interp_RunModule(interp, mod))
            print_repl_error(interp);
        fflush(stdout);
        len = 0;
    }
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    enum { MODE_RUN, MODE_AST, MODE_TOKENS, MODE_CHECK, MODE_HELP, MODE_VERSION,
           MODE_FORMAT, MODE_LINT, MODE_DEBUG } mode = MODE_RUN;
    const char *path = NULL;
    char *source;
    size_t size;
    AsterError err;
    int i;
    int path_index = -1;
    char **prog_argv = NULL;
    int prog_argc = 0;
    int no_check = 0;

    if (argc > 1 && !strcmp(argv[1], "pkg"))
        return Pkg_Main(argc - 2, argv + 2);

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
            mode = MODE_HELP;
        else if (!strcmp(arg, "--version") || !strcmp(arg, "-V"))
            mode = MODE_VERSION;
        else if (!strcmp(arg, "--ast"))
            mode = MODE_AST;
        else if (!strcmp(arg, "--tokens"))
            mode = MODE_TOKENS;
        else if (!strcmp(arg, "--check"))
            mode = MODE_CHECK;
        else if (!strcmp(arg, "--format"))
            mode = MODE_FORMAT;
        else if (!strcmp(arg, "--lint"))
            mode = MODE_LINT;
        else if (!strcmp(arg, "--debug"))
            mode = MODE_DEBUG;
        else if (!strcmp(arg, "--no-check"))
            no_check = 1;
        else if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            fprintf(stderr, "unknown option: %s\n", arg);
            fputs("usage: aster [--ast|--tokens|--check|--format|--lint|--debug|--no-check] FILE [ARGS...]\n", stderr);
            return 2;
        }
        else {
            path = arg;
            path_index = i;
            break;
        }
    }
    if (path_index >= 0) {
        prog_argc = argc - path_index - 1;
        prog_argv = argv + path_index + 1;
    }

    if (mode == MODE_HELP) {
        puts("usage: aster [--ast|--tokens|--check|--format|--lint|--debug|--no-check] FILE [ARGS...]");
        puts("       aster [--ast|--tokens|--check|--format|--lint] -    (read from stdin)");
        puts("       aster                               (interactive REPL)");
        puts("       aster pkg <init|add|remove|list|check>   (package manager)");
        puts("  (default)   run a file (type-checks before running)");
        puts("  --ast       print the parsed AST");
        puts("  --tokens    print the token list");
        puts("  --check     type-check only");
        puts("  --format    print the canonical formatting (comments are dropped)");
        puts("  --lint      report unused variables and unreachable code");
        puts("  --debug     run under the statement debugger (s/c/p/b/q)");
        puts("  --no-check  run without type checking");
        puts("  --version   print the version");
        return 0;
    }
    if (mode == MODE_VERSION) {
        puts(ASTER_VERSION);
        return 0;
    }
    if (!path) {
        if (mode == MODE_RUN) {
            Arena *arena = Arena_New();
            Interp interp;

            memset(&interp, 0, sizeof interp);
            interp.arena = arena;
            interp.base_dir = ".";
            interp.stdlib_dir = find_stdlib_dir(arena);
            interp.argv = prog_argv;
            interp.argc = prog_argc;
            Interp_Init(&interp);
            Pkg_LoadDeps(arena, interp.base_dir, &interp);
            run_repl(&interp);
            Arena_Free(arena);
            return 0;
        }
        fputs("usage: aster [--ast|--tokens|--check|--no-check] FILE [ARGS...]\n", stderr);
        return 2;
    }

    source = strcmp(path, "-") == 0 ? read_stream(stdin, &size) : read_file(path, &size);
    if (!source) {
        if (strcmp(path, "-") == 0)
            fputs("cannot read standard input\n", stderr);
        return 2;
    }

    if (mode == MODE_TOKENS) {
        AsterTokenList list;
        if (!Aster_Tokenize(source, size, &list, &err)) {
            print_error(path, list.source ? list.source : source, &err);
            Aster_TokenList_Clear(&list);
            free(source);
            return 1;
        }
        dump_tokens(&list);
        Aster_TokenList_Clear(&list);
        free(source);
        return 0;
    }
    else {
        Arena *arena = Arena_New();
        Interp interp;
        TypeCheckEnv tenv;
        mod_ty mod;
        int failed;
        int check_failed;

        mod = Aster_ParseFile(source, size, arena, &err);
        if (!mod) {
            print_error(path, source, &err);
            Arena_Free(arena);
            free(source);
            return 1;
        }
        if (mode == MODE_AST) {
            Aster_AST_Dump(stdout, mod);
            Arena_Free(arena);
            free(source);
            return 0;
        }
        if (mode == MODE_FORMAT) {
            Aster_FormatModule(stdout, mod);
            fflush(stdout);
            Arena_Free(arena);
            free(source);
            return 0;
        }
        if (mode == MODE_LINT) {
            int warnings = Aster_LintModule(stderr, mod, path);
            fflush(stderr);
            Arena_Free(arena);
            free(source);
            return warnings ? 1 : 0;
        }

        memset(&interp, 0, sizeof(interp));
        interp.arena = arena;
        interp.base_dir = strcmp(path, "-") == 0 ? "." : dir_of_file(path, arena);
        interp.stdlib_dir = find_stdlib_dir(arena);
        interp.argv = prog_argv;
        interp.argc = prog_argc;
        Interp_Init(&interp);
        Pkg_LoadDeps(arena, interp.base_dir, &interp);

        tenv.base_dir = interp.base_dir;
        tenv.stdlib_dir = interp.stdlib_dir;
        tenv.module_dirs = interp.module_dirs;
        tenv.nmodule_dirs = interp.nmodule_dirs;

        if (mode == MODE_CHECK) {
            check_failed = TypeCheck_ModuleEx(arena, mod, path, &tenv) != 0;
            Arena_Free(arena);
            free(source);
            return check_failed ? 1 : 0;
        }
        if (!no_check && TypeCheck_ModuleEx(arena, mod, path, &tenv)) {
            Arena_Free(arena);
            free(source);
            return 1;
        }

        if (mode == MODE_DEBUG) {
            interp.debug = 1;
            interp.debug_step = 1;
            interp.debug_file = path;
        }
        failed = Interp_RunModule(&interp, mod);
        if (failed)
            print_uncaught(path, &interp);
        fflush(stdout);
        Arena_Free(arena);
        free(source);
        return failed ? 1 : 0;
    }
}
