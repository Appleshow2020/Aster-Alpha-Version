#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "internal/arena.h"
#include "internal/ast.h"
#include "internal/interp.h"
#include "internal/lexer.h"
#include "internal/parser.h"
#include "internal/pkg.h"
#include "internal/runtime.h"
#include "internal/typecheck.h"

#define ASTER_VERSION "0.3.0"

static char *read_stream(FILE *f, size_t *size)
{
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

static char *read_file(const char *path, size_t *size)
{
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

static void print_error(const char *filename, const char *source, const AsterError *err)
{
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

static void dump_tokens(const AsterTokenList *list)
{
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

static void print_uncaught(const char *filename, Interp *in)
{
    RValue *exc = in->exc;

    if (exc && exc->kind == RV_EXC) {
        fprintf(stderr, "%s:%d: error: %s: ", filename, in->line, exc->u.exc->name);
        RValue_Print(stderr, exc->u.exc->message, 0);
        fputc('\n', stderr);
    }
    else {
        fprintf(stderr, "%s:%d: error: Exception\n", filename, in->line);
    }
}

static int directory_exists(const char *path)
{
    struct stat st;
    if (!path || !path[0])
        return 0;
    return stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static char *dir_of_file(const char *path, Arena *arena)
{
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

static const char *find_stdlib_dir(Arena *arena)
{
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

int main(int argc, char **argv)
{
    enum { MODE_RUN, MODE_AST, MODE_TOKENS, MODE_CHECK, MODE_HELP, MODE_VERSION } mode = MODE_RUN;
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
        else if (!strcmp(arg, "--no-check"))
            no_check = 1;
        else if (arg[0] == '-' && strcmp(arg, "-") != 0) {
            fprintf(stderr, "unknown option: %s\n", arg);
            fputs("usage: aster [--ast|--tokens|--check|--no-check] FILE [ARGS...]\n", stderr);
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
        puts("usage: aster [--ast|--tokens|--check|--no-check] FILE [ARGS...]");
        puts("       aster [--ast|--tokens|--check] -    (표준 입력에서 읽는다)");
        puts("       aster pkg <init|add|remove|list|check>   (패키지 관리자)");
        puts("  (기본)      파일을 실행한다 (실행 전에 타입 검사를 한다)");
        puts("  --ast       파싱한 AST를 출력한다");
        puts("  --tokens    토큰 목록을 출력한다");
        puts("  --check     정적 타입 검사만 수행한다");
        puts("  --no-check  타입 검사 없이 실행한다");
        puts("  --version   버전을 출력한다");
        return 0;
    }
    if (mode == MODE_VERSION) {
        puts(ASTER_VERSION);
        return 0;
    }
    if (!path) {
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
        mod_ty mod;
        int failed;

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
        if (mode == MODE_CHECK) {
            int errors = TypeCheck_Module(arena, mod, path);
            Arena_Free(arena);
            free(source);
            return errors ? 1 : 0;
        }
        if (!no_check && TypeCheck_Module(arena, mod, path)) {
            Arena_Free(arena);
            free(source);
            return 1;
        }

        memset(&interp, 0, sizeof(interp));
        interp.arena = arena;
        interp.base_dir = strcmp(path, "-") == 0 ? "." : dir_of_file(path, arena);
        interp.stdlib_dir = find_stdlib_dir(arena);
        interp.argv = prog_argv;
        interp.argc = prog_argc;
        Interp_Init(&interp);
        Pkg_LoadDeps(arena, interp.base_dir, &interp);
        failed = Interp_RunModule(&interp, mod);
        if (failed)
            print_uncaught(path, &interp);
        fflush(stdout);
        Arena_Free(arena);
        free(source);
        return failed ? 1 : 0;
    }
}
