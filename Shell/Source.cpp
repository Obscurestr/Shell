#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_TOKENS 256
#define MAX_CMDS   64

static void die(const char* msg) {
    perror(msg);
    exit(1);
}

static void* xmalloc(size_t n) {
    void* p = malloc(n);
    if (!p) die("malloc");
    return p;
}

static char* xstrdup(const char* s) {
    char* d = strdup(s);
    if (!d) die("strdup");
    return d;
}

static char* trim(char* s) {
    if (!s) return s;
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char* e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

typedef enum {
    T_WORD, T_PIPE, T_IN, T_OUT, T_OUT_APP, T_AMP, T_END
} TokType;

typedef struct {
    TokType type;
    char* text;
} Token;

typedef struct {
    Token items[MAX_TOKENS];
    int n;
} TokenList;

static void token_push(TokenList* tl, TokType t, const char* txt) {
    if (tl->n >= MAX_TOKENS) {
        fprintf(stderr, "Too many tokens\n");
        return;
    }
    tl->items[tl->n].type = t;
    tl->items[tl->n].text = txt ? xstrdup(txt) : NULL;
    tl->n++;
}

static void free_tokens(TokenList* tl) {
    for (int i = 0;i < tl->n;i++) free(tl->items[i].text);
    tl->n = 0;
}

static void tokenize(const char* line, TokenList* out) {
    out->n = 0;
    const char* p = line;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;

        if (*p == '|') { token_push(out, T_PIPE, NULL); p++; continue; }
        if (*p == '&') { token_push(out, T_AMP, NULL); p++; continue; }
        if (*p == '<') { token_push(out, T_IN, NULL); p++; continue; }
        if (*p == '>') {
            if (*(p + 1) == '>') { token_push(out, T_OUT_APP, NULL); p += 2; }
            else { token_push(out, T_OUT, NULL); p++; }
            continue;
        }

        char buf[4096]; size_t bi = 0;
        while (*p && !isspace((unsigned char)*p) && *p != '|' && *p != '&' && *p != '<' && *p != '>') {
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                while (*p && *p != quote) {
                    if (*p == '\\' && quote == '"' && *(p + 1)) { p++; }
                    if (bi < sizeof(buf) - 1) buf[bi++] = *p;
                    p++;
                }
                if (*p == quote) p++;
            }
            else if (*p == '\\' && *(p + 1)) {
                p++;
                if (bi < sizeof(buf) - 1) buf[bi++] = *p++;
            }
            else {
                if (bi < sizeof(buf) - 1) buf[bi++] = *p++;
            }
        }
        buf[bi] = '\0';
        token_push(out, T_WORD, buf);
    }
    token_push(out, T_END, NULL);
}

typedef struct {
    char** argv;
    char* in_file;
    char* out_file;
    bool append;
} Command;

typedef struct {
    Command cmds[MAX_CMDS];
    int n_cmds;
    bool background;
} Pipeline;

static void free_command(Command* c) {
    if (!c) return;
    if (c->argv) {
        for (char** a = c->argv; *a; a++) free(*a);
        free(c->argv);
    }
    free(c->in_file);
    free(c->out_file);
    c->argv = NULL; c->in_file = c->out_file = NULL; c->append = false;
}

static void free_pipeline(Pipeline* pl) {
    for (int i = 0;i < pl->n_cmds;i++) free_command(&pl->cmds[i]);
    pl->n_cmds = 0; pl->background = false;
}

static bool parse_pipeline(TokenList* tl, Pipeline* pl) {
    pl->n_cmds = 0; pl->background = false;
    int i = 0;
    while (tl->items[i].type != T_END) {
        if (pl->n_cmds >= MAX_CMDS) { fprintf(stderr, "Too many pipeline commands\n"); return false; }
        Command* cmd = &pl->cmds[pl->n_cmds];
        memset(cmd, 0, sizeof(*cmd));
        char* argv_buf[256]; int argc = 0;
        while (1) {
            Token tk = tl->items[i];
            if (tk.type == T_WORD) {
                if (argc >= 255) { fprintf(stderr, "Too many args\n"); return false; }
                argv_buf[argc++] = xstrdup(tk.text);
                i++;
            }
            else if (tk.type == T_IN || tk.type == T_OUT || tk.type == T_OUT_APP) {
                TokType what = tk.type; i++;
                if (tl->items[i].type != T_WORD) { fprintf(stderr, "Redirection requires filename\n"); return false; }
                char* fname = xstrdup(tl->items[i].text); i++;
                if (what == T_IN) { free(cmd->in_file); cmd->in_file = fname; }
                else { free(cmd->out_file); cmd->out_file = fname; cmd->append = (what == T_OUT_APP); }
            }
            else {
                break;
            }
        }

        if (argc == 0) { fprintf(stderr, "Syntax error: empty command\n"); return false; }

        cmd->argv = xmalloc((argc + 1) * sizeof(char*));
        for (int k = 0;k < argc;k++) cmd->argv[k] = argv_buf[k];
        cmd->argv[argc] = NULL;

        if (tl->items[i].type == T_PIPE) { i++; pl->n_cmds++; continue; }
        else if (tl->items[i].type == T_AMP) { pl->background = true; i++; pl->n_cmds++; break; }
        else if (tl->items[i].type == T_END) { pl->n_cmds++; break; }
    }

    if (pl->background) {
        while (tl->items[i].type != T_END) i++;
    }
    return pl->n_cmds > 0;
}

static volatile sig_atomic_t fg_pgid = 0;

static void sigint_handler(int signo) {
    (void)signo;
    pid_t pg = fg_pgid;
    if (pg > 0) {
        kill(-pg, SIGINT);
    }
}

static void sigchld_handler(int signo) {
    (void)signo;
    int saved = errno;
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) break;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            fprintf(stderr, "[bg] pid %d done\n", pid);
        }
    }
    errno = saved;
}

static int builtin_cd(char** argv) {
    const char* path = argv[1] ? argv[1] : getenv("HOME");
    if (!path) path = ".";
    if (chdir(path) != 0) { perror("cd"); return 1; }
    return 0;
}

static int builtin_pwd(void) {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) { printf("%s\n", buf); return 0; }
    else { perror("pwd"); return 1; }
}

static bool is_builtin(const char* cmd) {
    return (strcmp(cmd, "cd") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "pwd") == 0);
}

static int run_builtin(char** argv) {
    if (strcmp(argv[0], "cd") == 0) return builtin_cd(argv);
    if (strcmp(argv[0], "pwd") == 0) return builtin_pwd();
    if (strcmp(argv[0], "exit") == 0) exit(0);
    return 1;
}

static void apply_redirs(const Command* cmd) {
    if (cmd->in_file) {
        int fd = open(cmd->in_fil_
