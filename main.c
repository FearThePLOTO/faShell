#include "shell.h"

/* Ctrl+C: abandon the input line like bash (GNU readline FAQ pattern) */
static sigjmp_buf g_ctrlc_env;
static volatile sig_atomic_t g_jump_active = 0;

static void on_sigint(int sig){
    (void)sig;
    if(!g_jump_active) return;      /* interrupt only while inside readline() */
    g_jump_active = 0;
    write(STDOUT_FILENO, "\n", 1);
    rl_free_line_state();
    rl_cleanup_after_signal();
    siglongjmp(g_ctrlc_env, 1);
}

/* boot splash */
static void show_banner(void){
    printf(FS_ACCENT
"\n ███████╗ █████╗ ███████╗██╗  ██╗███████╗██╗      ██╗\n"
" ██╔════╝██╔══██╗██╔════╝██║  ██║██╔════╝██║      ██║\n"
" █████╗  ███████║███████╗███████║█████╗  ██║      ██║\n"
" ██╔══╝  ██╔══██║╚════██║██╔══██║██╔══╝  ██║      ██║\n"
" ██║     ██║  ██║███████║██║  ██║███████╗███████╗ ███████╗\n"
" ╚═╝     ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝╚══════╝╚══════╝ ╚══════╝\n"
"                        (╥﹏╥)\n\n"
           FS_RESET);
}

int main(){
    if(isatty(STDIN_FILENO))
        show_banner();          /* splash only on a real terminal: piped tests stay clean */
    signal(SIGINT, on_sigint);
    rl_catch_signals = 0;           /* we own signal handling */
    char *args[MAX_LINE/2 + 1];
    int run_flag = 1;

    while(run_flag){
        while(waitpid(-1, NULL, WNOHANG) > 0);   /* reap background children before they turn to zombies */

        if(sigsetjmp(g_ctrlc_env, 1) != 0){
            /* Ctrl+C landed: fall through to a fresh prompt */
        }
        g_jump_active = 1;

        char cwd[PATH_MAX];
        char prompt[PATH_MAX + 96];
        if(getcwd(cwd, sizeof(cwd))){
            /* $HOME collapses to ~ like a polite shell */
            const char *h = getenv("HOME");
            size_t hl = h ? strlen(h) : 0;
            if(hl && strncmp(cwd, h, hl) == 0)
                snprintf(prompt, sizeof(prompt), FS_C(FS_ACCENT)"✦ "FS_C(FS_FG)"~%s"FS_C(FS_MUTED)" > "FS_C(FS_RESET), cwd + hl);
            else
                snprintf(prompt, sizeof(prompt), FS_C(FS_ACCENT)"✦ "FS_C(FS_FG)"%s"FS_C(FS_MUTED)" > "FS_C(FS_RESET), cwd);
        } else {
            snprintf(prompt, sizeof(prompt), FS_C(FS_ACCENT)"✦ "FS_C(FS_MUTED)"> "FS_C(FS_RESET));
        }

        char *line = readline(prompt);
        g_jump_active = 0;
        if(line == NULL){
            putchar('\n');      /* Ctrl+D/EOF */
            break;
        }

        /* !! re-runs the last command; must run before add_history so the
           expanded command is stored, not the literal "!!" */
        if(strcmp(line, "!!") == 0){
            HIST_ENTRY **h = history_list();
            if(h == NULL || history_length == 0){
                printf("No commands in history.\n");
                free(line);
                continue;
            }
            printf("%s\n", h[history_length-1]->line);
            char *expanded = strdup(h[history_length-1]->line);
            free(line);
            line = expanded;
        }

        /* save the full line before strtok overwrites its spaces with NULs */
        if(line[strspn(line, " ")])
            add_history(line);

        int cnt = 0;
        char *token = strtok(line, " ");
        /* readline lines are unbounded; args is not */
        while(token != NULL && cnt < MAX_LINE/2){
            args[cnt++] = token;
            token = strtok(NULL, " ");
        }
        args[cnt] = NULL;

        if(args[0] == NULL){
            free(line);
            continue;
        }

        if(strcmp(args[0], "exit") == 0){
            free(line);
            run_flag = 0;
            continue;
        }
        if(strcmp(args[0], "cd") == 0){
            if(chdir(args[1] ? args[1] : getenv("HOME")) == -1)
                perror("cd failed");
            free(line);
            continue;
        }
        if(strcmp(args[0], "history") == 0){
            HIST_ENTRY **h = history_list();
            for(int i = 0; h && h[i]; i++)
                printf("%5d  %s\n", i + 1, h[i]->line);
            free(line);
            continue;
        }

        int waitingFlag = 0;
        if(strcmp(args[cnt-1], "&") == 0){
            waitingFlag = 1;
            args[cnt-1] = NULL;
        }

        /* pipeline: split '|' stages. spec caps graded tests at one pipe and
           forbids mixing with redirection; the general loop is the same fd
           dance repeated per stage, so we get N for the C price of ~2 */
        int has_pipe = 0, has_rd = 0;
        for(int i = 0; args[i]; i++){
            if(!strcmp(args[i], "|")) has_pipe = 1;
            else if(!strcmp(args[i], ">") || !strcmp(args[i], "<")) has_rd = 1;
        }
        if(has_pipe && has_rd){
            fprintf(stderr, "FaShell: redirection cannot be combined with a pipe\n");
            free(line);
            continue;
        }

        if(has_pipe){
            int nseg = 1;
            for(int i = 0; args[i]; i++)
                if(!strcmp(args[i], "|")) nseg++;

            char **seg = args;          /* current stage's argv */
            int prev_read = -1;         /* read end of the previous stage's pipe */
            int failed = 0;
            int npids = 0;

            for(int stage = 0; stage < nseg; stage++){
                char **next = seg;
                while(*next && strcmp(*next, "|")) next++;

                if(seg == next || *seg == NULL){    /* empty stage: |a, a|, a||b */
                    fprintf(stderr, "FaShell: empty command in pipeline\n");
                    failed = 1;
                    break;
                }
                int last = (*next == NULL);
                int fds[2] = {-1, -1};
                if(!last && pipe(fds) == -1){
                    perror("pipe failed");
                    failed = 1;
                    break;
                }
                pid_t pid = fork();
                if(pid < 0){
                    perror("forking failed");
                    if(!last){ close(fds[0]); close(fds[1]); }
                    failed = 1;
                    break;
                }
                if(pid == 0){
                    signal(SIGINT, SIG_DFL);
                    if(prev_read != -1){
                        dup2(prev_read, STDIN_FILENO);
                        close(prev_read);
                    }
                    if(!last){
                        dup2(fds[1], STDOUT_FILENO);
                        close(fds[0]); close(fds[1]);
                    }
                    *next = NULL;               /* end this stage's argv */
                    execvp(seg[0], seg);
                    perror("Command execution failed");
                    exit(1);
                }
                npids++;                if(prev_read != -1) close(prev_read);   /* consumed by this stage */
                if(!last){
                    close(fds[1]);              /* write end lives in the child now */
                    prev_read = fds[0];         /* carry the read end forward */
                } else break;
                seg = next + 1;
            }
            if(failed && prev_read != -1)
                close(prev_read);               /* stalled writers die on SIGPIPE */
            if(!failed && !waitingFlag)
                for(int k = 0; k < npids; k++)
                    wait(NULL);
            free(line);
            continue;
        }

        /* redirection: cut the operator and filename out of argv here (so the
           child never sees them), rewire the fd in the child, spec says
           one operator per command, never both */
        int rd = 0;                 /* 0 none, 1 = '>', 2 = '<' */
        char *rd_file = NULL;
        for(int i = 0; args[i]; i++){
            if(strcmp(args[i], ">") == 0 || strcmp(args[i], "<") == 0){
                rd = (args[i][0] == '>') ? 1 : 2;
                rd_file = args[i+1];
                args[i] = NULL;
                break;
            }
        }
        if(rd && args[0] == NULL){
            fprintf(stderr, "FaShell: missing command before '%s'\n",
                    rd == 1 ? ">" : "<");
            free(line);
            continue;
        }

        pid_t pid = fork();
        if(pid == 0){
            signal(SIGINT, SIG_DFL);
            if(rd){
                if(rd_file == NULL){
                    fprintf(stderr, "FaShell: missing file after '%s'\n",
                            rd == 1 ? ">" : "<");
                    exit(1);
                }
                int fd = (rd == 1)
                    ? open(rd_file, O_WRONLY|O_CREAT|O_TRUNC, 0644)
                    : open(rd_file, O_RDONLY);
                if(fd < 0){
                    perror("open failed");
                    exit(1);
                }
                if(dup2(fd, (rd == 1) ? STDOUT_FILENO : STDIN_FILENO) < 0){
                    perror("dup2 failed");
                    exit(1);
                }
                close(fd);
            }
            if (execvp(args[0], args) == -1){
                perror("Command execution failed");
                exit(1);
            }
        } else if(pid > 0) {
            if(!waitingFlag)
                wait(NULL);
        } else {
            perror("forking failed");
        }
        free(line);
    }


    return 0;
}