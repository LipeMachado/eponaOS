#include "libc.h"

#define NULL ((void*)0)
#define FD_STDIN  0
#define FD_STDOUT 1

static int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static int ends_with(const char *s, const char *suffix) {
    int sl = strlen(s); int tl = strlen(suffix);
    if (tl > sl) return 0;
    return strcmp(s + sl - tl, suffix) == 0;
}

static void strcpy(char *dst, const char *src) { while ((*dst++ = *src++)) {} }
static void strcat(char *dst, const char *src) { while (*dst) dst++; strcpy(dst, src); }

static void puts(const char *s) { sys_write(STDOUT_FILENO, s, (uint64_t)strlen(s)); }
static void putc(char c) { sys_write(STDOUT_FILENO, &c, 1); }
static void clear_screen(void) { putc('\f'); }

static void itoa(uint32_t val, char *buf) {
    char tmp[12]; int i = 11; tmp[i] = 0;
    if (val == 0) tmp[--i] = '0';
    while (val) { tmp[--i] = (char)('0' + val % 10); val /= 10; }
    int j = 0; while (tmp[i]) buf[j++] = tmp[i++];
    buf[j] = 0;
}

#define LINE_MAX 256
#define TOKEN_MAX 32
#define EPP_MAX_INSTALLED 32

struct installed_pkg {
    char name[32];
    char version[16];
    char file[64];
    int used;
};

static struct installed_pkg g_installed[EPP_MAX_INSTALLED];

/* ---- Signal handling ---- */
static volatile int g_got_sigint = 0;

static void sigint_handler(int sig) {
    (void)sig;
    g_got_sigint = 1;
}

/* ---- Readline with line editing ---- */
static int readline(char *buf, int max) {
    int pos = 0;
    buf[0] = 0;
    g_got_sigint = 0;
    while (1) {
        char c;
        if (sys_read(STDIN_FILENO, &c, 1) != 1) continue;
        if (g_got_sigint) {
            puts("^C\n");
            buf[0] = 0;
            return -1;
        }
        if (c == '\n') { putc('\n'); buf[pos] = 0; return 0; }
        if (c == '\b' || c == 127) { if (pos > 0) { pos--; putc('\b'); } continue; }
        if (c == 3) { /* Ctrl+C */
            puts("^C\n");
            buf[0] = 0;
            return -1;
        }
        if (c == 4) { /* Ctrl+D */
            puts("\nGoodbye!\n");
            sys_exit(0);
        }
        if (pos < max - 1) { buf[pos++] = c; putc(c); }
    }
}

static int tokenize(char *line, char **tokens, int max) {
    int count = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (count >= max - 1) break;
        tokens[count++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '|' && *p != '>' && *p != '<') p++;
        if (*p) *p++ = 0;
    }
    tokens[count] = 0;
    return count;
}

/* ---- Pipe & redirection detection ---- */
static int has_pipe(char *line) {
    for (int i = 0; line[i]; i++) if (line[i] == '|') return 1;
    return 0;
}

static char *find_redir_out(char **tokens, int argc) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
            if (i + 1 < argc) return tokens[i + 1];
        }
    }
    return NULL;
}

/* ---- File helpers ---- */
static int read_file(const char *path, char *buf, int cap) {
    int fd = sys_open(path);
    if (fd < 0) return -1;
    int total = 0;
    while (total < cap - 1) {
        int n = sys_read(fd, buf + total, (uint64_t)(cap - 1 - total));
        if (n <= 0) break;
        total += n;
    }
    buf[total] = 0;
    sys_close(fd);
    return total;
}

/* ---- EPP helpers ---- */
static void print_value(const char *manifest, const char *key) {
    int kl = strlen(key);
    const char *p = manifest;
    while (*p) {
        if (starts_with(p, key) && p[kl] == '=') {
            p += kl + 1;
            while (*p && *p != '\n') putc(*p++);
            return;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    puts("unknown");
}

static int get_value(const char *manifest, const char *key, char *out, int cap) {
    int kl = strlen(key);
    const char *p = manifest;
    while (*p) {
        if (starts_with(p, key) && p[kl] == '=') {
            int i = 0; p += kl + 1;
            while (*p && *p != '\n' && i < cap - 1) out[i++] = *p++;
            out[i] = 0; return 0;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    out[0] = 0; return -1;
}

static void package_path(const char *name, char *out) {
    strcpy(out, name);
    if (!ends_with(out, ".epk")) strcat(out, ".epk");
}

static int installed_find(const char *name) {
    for (int i = 0; i < EPP_MAX_INSTALLED; i++)
        if (g_installed[i].used && strcmp(g_installed[i].name, name) == 0) return i;
    return -1;
}

static int installed_add(const char *name, const char *version, const char *file) {
    int idx = installed_find(name);
    if (idx < 0) { for (int i = 0; i < EPP_MAX_INSTALLED; i++) if (!g_installed[i].used) { idx = i; break; } }
    if (idx < 0) return -1;
    g_installed[idx].used = 1;
    strcpy(g_installed[idx].name, name);
    strcpy(g_installed[idx].version, version);
    strcpy(g_installed[idx].file, file);
    return 0;
}

static int installed_remove(const char *name) {
    int idx = installed_find(name);
    if (idx < 0) return -1;
    g_installed[idx].used = 0;
    return 0;
}

static void installed_list(void) {
    int count = 0;
    for (int i = 0; i < EPP_MAX_INSTALLED; i++) {
        if (!g_installed[i].used) continue;
        puts("  "); puts(g_installed[i].name); puts(" "); puts(g_installed[i].version);
        puts(" -> "); puts(g_installed[i].file); putc('\n');
        count++;
    }
    if (count == 0) puts("  (none)\n");
}

static void epk_execute(const char *path) {
    char manifest[512], file[64];
    if (read_file(path, manifest, sizeof(manifest)) < 0) { puts("epp: cannot open: "); puts(path); putc('\n'); return; }
    puts("Package: "); print_value(manifest, "name"); putc('\n');
    puts("Version: "); print_value(manifest, "version"); putc('\n');
    puts("About:   "); print_value(manifest, "description"); putc('\n');
    if (get_value(manifest, "file", file, sizeof(file)) < 0) { puts("epp: no file entry\n"); return; }
    int fd = sys_open(file); if (fd < 0) { puts("epp: payload missing: "); puts(file); putc('\n'); return; }
    sys_close(fd);
    puts("Status:  installed/ready (payload "); puts(file); puts(")\n");
}

static int epk_install(const char *path) {
    char manifest[512], name[32], version[16], file[64];
    if (read_file(path, manifest, sizeof(manifest)) < 0) { puts("epp: cannot open: "); puts(path); putc('\n'); return -1; }
    if (get_value(manifest, "name", name, sizeof(name)) < 0 ||
        get_value(manifest, "version", version, sizeof(version)) < 0 ||
        get_value(manifest, "file", file, sizeof(file)) < 0) { puts("epp: invalid manifest\n"); return -1; }
    int fd = sys_open(file); if (fd < 0) { puts("epp: payload missing: "); puts(file); putc('\n'); return -1; }
    sys_close(fd);
    if (installed_add(name, version, file) < 0) { puts("epp: db full\n"); return -1; }
    puts("Installed "); puts(name); puts(" "); puts(version); putc('\n');
    return 0;
}

/* ---- Commands ---- */
static void cmd_help(void) {
    puts("Commands:\n");
    puts("  help              show this help\n");
    puts("  clear             clear screen\n");
    puts("  pwd               print working directory\n");
    puts("  ls [path]         list directory\n");
    puts("  cat <file>        print file contents\n");
    puts("  write <file> <t>  create/overwrite file\n");
    puts("  rm <file>         delete file\n");
    puts("  stat <file>       show file info\n");
    puts("  mkdir <dir>       create directory\n");
    puts("  run <elf>         execute ELF binary\n");
    puts("  echo <text>       print text\n");
    puts("  pid               show process ID\n");
    puts("  ps                show processes\n");
    puts("  kill <pid>        kill process\n");
    puts("  shutdown          power off\n");
    puts("  reboot            restart system\n");
    puts("  epp <cmd> [pkg]   Epona Package manager\n");
    puts("  edit [file]       text editor\n");
    puts("  <file>.epk        execute package manifest\n");
    puts("  pipe: cmd1 | cmd2  pipe output to command\n");
    puts("  redir: cmd > file  redirect output to file\n");
}

static void cmd_ls(char **tokens, int argc) {
    char buf[2048];
    const char *path = argc > 1 ? tokens[1] : "/";
    int n = sys_readdir(path, buf, sizeof(buf));
    if (n < 0) { puts("ls: cannot read directory\n"); return; }
    if (n == 0) { puts("(empty)\n"); return; }
    puts(buf);
}

static void cmd_cat(char **tokens, int argc) {
    if (argc < 2) { puts("usage: cat <file>\n"); return; }
    int fd = sys_open(tokens[1]);
    if (fd < 0) { puts("cat: cannot open "); puts(tokens[1]); putc('\n'); return; }
    char buf[256];
    while (1) {
        int n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(STDOUT_FILENO, buf, (uint64_t)n);
    }
    sys_close(fd);
}

static void cmd_write(char **tokens, int argc) {
    if (argc < 3) { puts("usage: write <file> <text>\n"); return; }
    int fd = sys_create(tokens[1]);
    if (fd < 0) { puts("write: cannot create "); puts(tokens[1]); putc('\n'); return; }
    for (int i = 2; i < argc; i++) {
        if (i > 2) sys_write(fd, " ", 1);
        sys_write(fd, tokens[i], (uint64_t)strlen(tokens[i]));
    }
    sys_write(fd, "\n", 1);
    sys_close(fd);
}

static void cmd_rm(char **tokens, int argc) {
    if (argc < 2) { puts("usage: rm <file>\n"); return; }
    if (sys_unlink(tokens[1]) < 0) { puts("rm: cannot remove "); puts(tokens[1]); putc('\n'); }
}

static void cmd_stat(char **tokens, int argc) {
    if (argc < 2) { puts("usage: stat <file>\n"); return; }
    uint32_t size; uint8_t flags;
    if (sys_stat(tokens[1], &size, &flags) < 0) { puts("stat: cannot stat "); puts(tokens[1]); putc('\n'); return; }
    puts(tokens[1]);
    if (flags & 2) puts(" (dir)"); else puts(" (file)");
    putc('\n');
    puts("  size: "); char num[12]; itoa(size, num); puts(num); puts(" bytes\n");
}

static void cmd_run(char **tokens, int argc) {
    if (argc < 2) { puts("usage: run <elf>\n"); return; }
    if (ends_with(tokens[1], ".epk")) { puts("run: .epk is a manifest, not ELF\n"); return; }
    puts("Executing...\n");
    if (sys_exec(tokens[1]) < 0) { puts("run: cannot execute "); puts(tokens[1]); putc('\n'); }
}

static void cmd_mkdir(char **tokens, int argc) {
    if (argc < 2) { puts("usage: mkdir <dir>\n"); return; }
    int fd = sys_create(tokens[1]);
    if (fd < 0) { puts("mkdir: failed\n"); return; }
    sys_close(fd);
    puts("created directory "); puts(tokens[1]); putc('\n');
}

static void cmd_pid(void) {
    char num[12]; itoa((uint32_t)sys_getpid(), num);
    puts("PID: "); puts(num); putc('\n');
}

static void cmd_epp(char **tokens, int argc) {
    char path[64], repo[512];
    if (argc < 2 || strcmp(tokens[1], "help") == 0) {
        puts("Epona Package Manager (epp)\n");
        puts("  epp update          refresh repo index\n");
        puts("  epp list            list packages\n");
        puts("  epp installed       list installed\n");
        puts("  epp info <pkg>      package info\n");
        puts("  epp install <pkg>   install package\n");
        puts("  epp remove <pkg>    remove package\n");
        return;
    }
    if (strcmp(tokens[1], "update") == 0) {
        if (read_file("repo.epk", repo, sizeof(repo)) < 0) { puts("epp: repo.epk not found\n"); return; }
        puts("Repository updated.\n");
    } else if (strcmp(tokens[1], "list") == 0) {
        if (read_file("repo.epk", repo, sizeof(repo)) < 0) { puts("epp: run epp update first\n"); return; }
        const char *p = repo;
        while (*p) {
            if (starts_with(p, "package=")) { puts("  "); p += 8; while (*p && *p != '\n') putc(*p++); putc('\n'); }
            while (*p && *p != '\n') p++; if (*p == '\n') p++;
        }
    } else if (strcmp(tokens[1], "installed") == 0) {
        installed_list();
    } else if (strcmp(tokens[1], "remove") == 0) {
        if (argc < 3) { puts("usage: epp remove <pkg>\n"); return; }
        if (installed_remove(tokens[2]) < 0) { puts("epp: not installed: "); puts(tokens[2]); putc('\n'); }
        else { puts("Removed "); puts(tokens[2]); putc('\n'); }
    } else if (strcmp(tokens[1], "info") == 0 || strcmp(tokens[1], "install") == 0) {
        if (argc < 3) { puts("usage: epp "); puts(tokens[1]); puts(" <pkg>\n"); return; }
        package_path(tokens[2], path);
        if (strcmp(tokens[1], "info") == 0) epk_execute(path);
        else if (epk_install(path) == 0) puts("epp: installed\n");
    } else {
        puts("epp: unknown: "); puts(tokens[1]); putc('\n');
    }
}

/* ---- Execute a single command with optional redirection ---- */
static void exec_cmd(char **tokens, int argc) {
    if (argc == 0) return;

    /* Check for output redirection */
    char *redir_file = find_redir_out(tokens, argc);
    int redir_fd = -1;
    int append = 0;

    if (redir_file) {
        /* Find the > or >> token and truncate argc there */
        for (int i = 0; i < argc; i++) {
            if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
                append = (tokens[i][1] == '>');
                tokens[i] = 0;
                argc = i;
                break;
            }
        }
        if (append) {
            /* For append, we'd need to seek to end — for now just create */
            redir_fd = sys_create(redir_file);
        } else {
            redir_fd = sys_create(redir_file);
        }
        if (redir_fd >= 0) {
            sys_dup2(redir_fd, STDOUT_FILENO);
        }
    }

    /* Built-in commands */
    if (strcmp(tokens[0], "help") == 0) cmd_help();
    else if (strcmp(tokens[0], "clear") == 0) clear_screen();
    else if (strcmp(tokens[0], "pwd") == 0) puts("/\n");
    else if (strcmp(tokens[0], "ls") == 0) cmd_ls(tokens, argc);
    else if (strcmp(tokens[0], "cat") == 0) cmd_cat(tokens, argc);
    else if (strcmp(tokens[0], "write") == 0) cmd_write(tokens, argc);
    else if (strcmp(tokens[0], "rm") == 0) cmd_rm(tokens, argc);
    else if (strcmp(tokens[0], "stat") == 0) cmd_stat(tokens, argc);
    else if (strcmp(tokens[0], "run") == 0) cmd_run(tokens, argc);
    else if (strcmp(tokens[0], "mkdir") == 0) cmd_mkdir(tokens, argc);
    else if (strcmp(tokens[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) { if (i > 1) putc(' '); puts(tokens[i]); }
        putc('\n');
    }
    else if (strcmp(tokens[0], "pid") == 0) cmd_pid();
    else if (strcmp(tokens[0], "epp") == 0) cmd_epp(tokens, argc);
    else if (strcmp(tokens[0], "edit") == 0) {
        if (argc > 1) sys_exec("EDIT.ELF");
        else sys_exec("EDIT.ELF");
    }
    else if (strcmp(tokens[0], "shutdown") == 0) { sys_reboot(REBOOT_CMD_SHUTDOWN); }
    else if (strcmp(tokens[0], "reboot") == 0) { sys_reboot(REBOOT_CMD_REBOOT); }
    else if (ends_with(tokens[0], ".epk")) epk_execute(tokens[0]);
    else {
        /* Try to run as ELF */
        if (sys_exec(tokens[0]) < 0) {
            puts("Unknown command: "); puts(tokens[0]); putc('\n');
        }
    }

    /* Restore stdout if redirected */
    if (redir_fd >= 0) {
        sys_dup2(FD_STDOUT, STDOUT_FILENO);
        sys_close(redir_fd);
    }
}

/* ---- Execute piped commands: cmd1 | cmd2 ---- */
static void exec_pipe(char *line) {
    char *pipe_pos = (void*)0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '|') { line[i] = 0; pipe_pos = &line[i + 1]; break; }
    }
    if (!pipe_pos) return;

    /* Trim leading spaces of second command */
    while (*pipe_pos == ' ') pipe_pos++;

    int pipe_fds[2];
    if (sys_pipe(pipe_fds) < 0) { puts("pipe: failed\n"); return; }

    int child = sys_fork();
    if (child == 0) {
        /* Child: stdout -> pipe write end, then exec left side */
        sys_dup2(pipe_fds[1], STDOUT_FILENO);
        sys_close(pipe_fds[0]);
        sys_close(pipe_fds[1]);

        char *tokens[TOKEN_MAX];
        char buf[LINE_MAX];
        int plen = strlen(line);
        for (int i = 0; i <= plen; i++) buf[i] = line[i];
        int argc = tokenize(buf, tokens, TOKEN_MAX);
        exec_cmd(tokens, argc);
        sys_exit(0);
    } else {
        /* Parent: stdin <- pipe read end, then exec right side */
        sys_dup2(pipe_fds[0], STDIN_FILENO);
        sys_close(pipe_fds[0]);
        sys_close(pipe_fds[1]);

        char *tokens[TOKEN_MAX];
        char buf[LINE_MAX];
        int plen = strlen(pipe_pos);
        for (int i = 0; i <= plen; i++) buf[i] = pipe_pos[i];
        int argc = tokenize(buf, tokens, TOKEN_MAX);
        exec_cmd(tokens, argc);

        /* Wait for child */
        sys_wait((void*)0);

        /* Restore stdin */
        sys_dup2(FD_STDIN, STDIN_FILENO);
    }
}

/* ---- Main ---- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    sys_signal(SIGINT, sigint_handler);

    char line[LINE_MAX];
    char *tokens[TOKEN_MAX];

    while (1) {
        puts("user@epona:~$ ");
        if (readline(line, LINE_MAX) < 0) continue;
        if (line[0] == 0) continue;

        /* Handle exit specially */
        char *tok_first = line;
        while (*tok_first == ' ') tok_first++;
        if (starts_with(tok_first, "exit")) {
            puts("Goodbye!\n");
            return 0;
        }

        if (has_pipe(line)) {
            exec_pipe(line);
        } else {
            int argc2 = tokenize(line, tokens, TOKEN_MAX);
            if (argc2 > 0) exec_cmd(tokens, argc2);
        }
    }
}
