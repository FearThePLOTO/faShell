#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <linux/limits.h>

#define MAX_LINE 100

int main(){

    char *args[MAX_LINE/2 + 1];
    char input[MAX_LINE];
    int run_flag = 1;

    while(run_flag){
        char buf[PATH_MAX];
        if(getcwd(buf, sizeof(buf)))
            printf("FaShell ✦ %s > ", buf);
        else
            printf("FaShell ✦ > ");
        fflush(stdout);
        if(fgets(input, sizeof(input), stdin) == NULL){
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        int cnt = 0;
        char *token = strtok(input, " ");
        while(token != NULL){
            args[cnt++] = token;
            token = strtok(NULL, " ");
        }
        args[cnt] = NULL;

        if(args[0] == NULL) continue;

        if(strcmp(args[0], "exit") == 0){
            run_flag = 0;
            continue;
        }
        if(strcmp(args[0], "cd") == 0){
            if(chdir(args[1] ? args[1] : getenv("HOME")) == -1)
                perror("cd failed");
            continue;
        }
        pid_t pid = fork();
        if(pid == 0){
            if (execvp(args[0], args) == -1){
                perror("Command execution failed");
                exit(1);
            }
        } else if(pid > 0) {
            wait(NULL);
        } else {
            perror("forking failed");
        }
        
    }


    return 0;
}