#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <signal.h>
#include <readline/readline.h>
#include <setjmp.h>

#define MAX_LINE 100

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

int main(){
    signal(SIGINT, on_sigint);
    rl_catch_signals = 0;           /* we own signal handling */
    char *args[MAX_LINE/2 + 1];
    int run_flag = 1;

    while(run_flag){
        while(waitpid(-1, NULL, WNOHANG) > 0);   /* reap finished background jobs */

        if(sigsetjmp(g_ctrlc_env, 1) != 0){
            /* Ctrl+C landed: fall through to a fresh prompt */
        }
        g_jump_active = 1;

        char cwd[PATH_MAX];
        char prompt[PATH_MAX + 32];
        if(getcwd(cwd, sizeof(cwd)))
            snprintf(prompt, sizeof(prompt), "FaShell ✦ %s > ", cwd);
        else
            snprintf(prompt, sizeof(prompt), "FaShell ✦ > ");

        char *line = readline(prompt);
        g_jump_active = 0;
        if(line == NULL){
            putchar('\n');      /* Ctrl+D/EOF */
            break;
        }

        int cnt = 0;
        char *token = strtok(line, " ");
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

        int waitingFlag = 0;
        if(strcmp(args[cnt-1], "&") == 0){
            waitingFlag = 1;
            args[cnt-1] = NULL;
        }
        pid_t pid = fork();
        if(pid == 0){
            signal(SIGINT, SIG_DFL);
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