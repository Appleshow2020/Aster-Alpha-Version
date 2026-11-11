#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "internal/pkg.h"

#define MANIFEST "aster.pkg"

typedef struct {
    const char *alias;
    const char *path;
} Dep;

typedef struct {
    const char *name;
    const char *version;
    Dep *deps;
    size_t ndeps, cap;
} Manifest;

static int file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}

static int dir_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static char *xstrndup(Arena *arena, const char *s, size_t n)
{
    return Arena_Strndup(arena, s, n);
}

static char *join_path(Arena *arena, const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = Arena_Alloc(arena, la + lb + 2);
    memcpy(p, a, la);
    if (la && p[la - 1] != '/' && p[la - 1] != '\\')
        p[la++] = '/';
    memcpy(p + la, b, lb);
    p[la + lb] = 0;
    return p;
}

/* 경로의 부모 디렉터리. 위가 없으면 NULL. */
static char *parent_dir(Arena *arena, const char *path)
{
    size_t n = strlen(path);
    while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\'))
        n--;
    while (n > 0 && path[n - 1] != '/' && path[n - 1] != '\\')
        n--;
    if (n == 0)
        return NULL;
    while (n > 1 && (path[n - 1] == '/' || path[n - 1] == '\\'))
        n--;
    return xstrndup(arena, path, n);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *cut = slash > bslash ? slash : bslash;
    return cut ? cut + 1 : path;
}

static int is_absolute(const char *path)
{
    if (!path || !path[0])
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return path[1] == ':';
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

static char *read_text(Arena *arena, const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    buf = Arena_Alloc(arena, (size_t)size + 1);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return NULL;
    }
    buf[size] = 0;
    fclose(f);
    return buf;
}

static void manifest_push_dep(Arena *arena, Manifest *m, const char *alias,
                              const char *path)
{
    size_t i;
    for (i = 0; i < m->ndeps; i++) {
        if (!strcmp(m->deps[i].alias, alias)) {
            m->deps[i].path = path;
            return;
        }
    }
    if (m->ndeps == m->cap) {
        size_t cap = m->cap ? m->cap * 2 : 4;
        Dep *grown = Arena_Alloc(arena, cap * sizeof(Dep));
        if (m->deps)
            memcpy(grown, m->deps, m->ndeps * sizeof(Dep));
        m->deps = grown;
        m->cap = cap;
    }
    m->deps[m->ndeps].alias = alias;
    m->deps[m->ndeps].path = path;
    m->ndeps++;
}

static int manifest_remove_dep(Manifest *m, const char *alias)
{
    size_t i;
    for (i = 0; i < m->ndeps; i++) {
        if (!strcmp(m->deps[i].alias, alias)) {
            memmove(&m->deps[i], &m->deps[i + 1],
                    (m->ndeps - i - 1) * sizeof(Dep));
            m->ndeps--;
            return 1;
        }
    }
    return 0;
}

static void parse_manifest(Arena *arena, const char *text, Manifest *m)
{
    const char *p = text;
    int in_deps = 0;

    memset(m, 0, sizeof(*m));
    while (*p) {
        char line[1024];
        size_t n = 0;
        const char *eq;
        while (*p && *p != '\n' && n + 1 < sizeof line)
            line[n++] = *p++;
        line[n] = 0;
        if (*p == '\n')
            p++;
        {
            char *work = line;
            char *key;
            char *value;
            char *t = trim(work);
            if (!t[0] || t[0] == '#')
                continue;
            if (t[0] == '[') {
                char *close = strchr(t, ']');
                if (!close)
                    continue;
                *close = 0;
                in_deps = !strcmp(trim(t + 1), "deps");
                continue;
            }
            eq = strchr(t, '=');
            if (!eq)
                continue;
            key = (char *)xstrndup(arena, t, (size_t)(eq - t));
            value = (char *)trim((char *)eq + 1);
            {
                char *k = (char *)trim(key);
                size_t vn = strlen(value);
                if (vn >= 2 && (value[0] == '"' || value[0] == '\'') &&
                    value[vn - 1] == value[0]) {
                    value++;
                    vn -= 2;
                }
                value = (char *)xstrndup(arena, value, vn);
                if (in_deps) {
                    manifest_push_dep(arena, m, k, value);
                }
                else if (!strcmp(k, "name")) {
                    m->name = value;
                }
                else if (!strcmp(k, "version")) {
                    m->version = value;
                }
            }
        }
    }
}

static int load_manifest_at(Arena *arena, const char *dir, Manifest *m)
{
    char *path = join_path(arena, dir, MANIFEST);
    char *text = read_text(arena, path);
    if (!text)
        return 0;
    parse_manifest(arena, text, m);
    return 1;
}

static int write_manifest(const char *dir, const Manifest *m)
{
    size_t len = strlen(dir) + strlen(MANIFEST) + 2;
    char *path = malloc(len);
    FILE *f;
    size_t i;

    if (!path)
        return 0;
    snprintf(path, len, "%s/%s", dir, MANIFEST);
    f = fopen(path, "wb");
    free(path);
    if (!f)
        return 0;
    fprintf(f, "# Aster package manifest\n");
    if (m->name)
        fprintf(f, "name = %s\n", m->name);
    if (m->version)
        fprintf(f, "version = %s\n", m->version);
    fprintf(f, "\n[deps]\n");
    for (i = 0; i < m->ndeps; i++)
        fprintf(f, "%s = %s\n", m->deps[i].alias, m->deps[i].path);
    fclose(f);
    return 1;
}

/* dep 경로가 import alias를 실제로 찾을 수 있는지 검사한다. */
static int dep_resolvable(const char *root, const char *alias)
{
    char buf[1024];
    snprintf(buf, sizeof buf, "%s/%s.ast", root, alias);
    if (file_exists(buf))
        return 1;
    snprintf(buf, sizeof buf, "%s/%s/__init__.ast", root, alias);
    return file_exists(buf);
}

static void print_usage(void)
{
    puts("usage: aster pkg <command> [args]");
    puts("  init [name]        현재 디렉터리에 aster.pkg를 만든다");
    puts("  add NAME PATH      의존성을 추가한다 (PATH는 패키지의 부모 디렉터리)");
    puts("  remove NAME        의존성을 제거한다");
    puts("  list               의존성 목록을 출력한다");
    puts("  check              의존성이 실제로 있는지 검사한다");
}

static int pkg_init(Arena *arena, int argc, char **argv)
{
    Manifest m;
    const char *name = argc > 0 ? argv[0] : NULL;
    if (file_exists(MANIFEST)) {
        puts("aster.pkg가 이미 있습니다");
        return 1;
    }
    memset(&m, 0, sizeof m);
    m.name = name ? name : "app";
    m.version = "0.1.0";
    if (!write_manifest(".", &m)) {
        fputs("aster.pkg를 쓸 수 없습니다\n", stderr);
        return 1;
    }
    printf("생성: aster.pkg (name = %s)\n", m.name);
    (void)arena;
    return 0;
}

static int pkg_add(Arena *arena, int argc, char **argv)
{
    Manifest m;
    const char *alias;
    const char *path;
    const char *root;

    if (argc != 2) {
        fputs("usage: aster pkg add NAME PATH\n", stderr);
        return 2;
    }
    alias = argv[0];
    path = argv[1];
    if (!dir_exists(path)) {
        fprintf(stderr, "디렉터리가 없습니다: %s\n", path);
        return 1;
    }
    /* 패키지 디렉터리 자체를 줬다면 부모를 저장한다. */
    if (dep_resolvable(path, alias)) {
        root = path;
    }
    else if (!strcmp(base_name(path), alias) && file_exists(join_path(arena, path, "__init__.ast"))) {
        root = parent_dir(arena, path);
        if (!root) {
            fprintf(stderr, "%s의 부모 디렉터리를 찾을 수 없습니다\n", path);
            return 1;
        }
    }
    else {
        fprintf(stderr, "%s에서 '%s' 패키지를 찾을 수 없습니다 "
                        "(%s/%s.ast 또는 %s/%s/__init__.ast 필요)\n",
                path, alias, path, alias, path, alias);
        return 1;
    }

    if (!load_manifest_at(arena, ".", &m)) {
        memset(&m, 0, sizeof m);
        m.name = "app";
        m.version = "0.1.0";
    }
    manifest_push_dep(arena, &m, xstrndup(arena, alias, strlen(alias)),
                      xstrndup(arena, root, strlen(root)));
    if (!write_manifest(".", &m)) {
        fputs("aster.pkg를 쓸 수 없습니다\n", stderr);
        return 1;
    }
    printf("추가: %s -> %s\n", alias, root);
    return 0;
}

static int pkg_remove(Arena *arena, int argc, char **argv)
{
    Manifest m;
    if (argc != 1) {
        fputs("usage: aster pkg remove NAME\n", stderr);
        return 2;
    }
    if (!load_manifest_at(arena, ".", &m)) {
        fputs("aster.pkg가 없습니다\n", stderr);
        return 1;
    }
    if (!manifest_remove_dep(&m, argv[0])) {
        fprintf(stderr, "그런 의존성이 없습니다: %s\n", argv[0]);
        return 1;
    }
    if (!write_manifest(".", &m)) {
        fputs("aster.pkg를 쓸 수 없습니다\n", stderr);
        return 1;
    }
    printf("제거: %s\n", argv[0]);
    return 0;
}

static int pkg_list(Arena *arena, int argc, char **argv)
{
    Manifest m;
    size_t i;
    (void)argc;
    (void)argv;
    if (!load_manifest_at(arena, ".", &m)) {
        fputs("aster.pkg가 없습니다 (aster pkg init)\n", stderr);
        return 1;
    }
    if (m.name)
        printf("name: %s\n", m.name);
    if (m.version)
        printf("version: %s\n", m.version);
    if (m.ndeps == 0)
        puts("의존성 없음");
    for (i = 0; i < m.ndeps; i++)
        printf("%s = %s\n", m.deps[i].alias, m.deps[i].path);
    return 0;
}

static int pkg_check(Arena *arena, int argc, char **argv)
{
    Manifest m;
    size_t i;
    int failed = 0;
    (void)argc;
    (void)argv;
    if (!load_manifest_at(arena, ".", &m)) {
        fputs("aster.pkg가 없습니다 (aster pkg init)\n", stderr);
        return 1;
    }
    for (i = 0; i < m.ndeps; i++) {
        const char *root = m.deps[i].path;
        if (dep_resolvable(root, m.deps[i].alias)) {
            printf("ok   %s = %s\n", m.deps[i].alias, root);
        }
        else {
            printf("fail %s = %s (모듈을 찾을 수 없음)\n",
                   m.deps[i].alias, root);
            failed = 1;
        }
    }
    return failed ? 1 : 0;
}

int Pkg_Main(int argc, char **argv)
{
    Arena *arena = Arena_New();
    int code;

    if (argc == 0 || !strcmp(argv[0], "-h") || !strcmp(argv[0], "--help")) {
        print_usage();
        Arena_Free(arena);
        return argc == 0 ? 2 : 0;
    }
    if (!strcmp(argv[0], "init"))
        code = pkg_init(arena, argc - 1, argv + 1);
    else if (!strcmp(argv[0], "add"))
        code = pkg_add(arena, argc - 1, argv + 1);
    else if (!strcmp(argv[0], "remove"))
        code = pkg_remove(arena, argc - 1, argv + 1);
    else if (!strcmp(argv[0], "list"))
        code = pkg_list(arena, argc - 1, argv + 1);
    else if (!strcmp(argv[0], "check"))
        code = pkg_check(arena, argc - 1, argv + 1);
    else {
        fprintf(stderr, "unknown pkg command: %s\n", argv[0]);
        print_usage();
        code = 2;
    }
    Arena_Free(arena);
    return code;
}

/* ---- 로더 연결 ---- */

static const char *find_manifest_dir(Arena *arena, const char *base_dir)
{
    const char *start = base_dir && base_dir[0] ? base_dir : ".";
    char *dir = xstrndup(arena, start, strlen(start));
    int depth;
    for (depth = 0; depth < 32 && dir; depth++) {
        if (file_exists(join_path(arena, dir, MANIFEST)))
            return dir;
        dir = parent_dir(arena, dir);
    }
    return NULL;
}

void Pkg_LoadDeps(Arena *arena, const char *base_dir, Interp *in)
{
    const char *dir = find_manifest_dir(arena, base_dir);
    Manifest m;
    size_t i;
    const char *env;

    if (dir && load_manifest_at(arena, dir, &m)) {
        for (i = 0; i < m.ndeps; i++) {
            const char *root = m.deps[i].path;
            if (!is_absolute(root))
                root = join_path(arena, dir, root);
            Interp_AddModuleDir(in, root);
        }
    }

    env = getenv("ASTER_PATH");
    if (env && env[0]) {
        char *copy = xstrndup(arena, env, strlen(env));
        char *p = copy;
        while (p && *p) {
            char *sep = strchr(p, ';');
#ifdef __unix__
            if (!sep)
                sep = strchr(p, ':');
#endif
            if (sep)
                *sep = 0;
            if (*p)
                Interp_AddModuleDir(in, Arena_Strndup(arena, p, strlen(p)));
            p = sep ? sep + 1 : NULL;
        }
    }
}
