#ifndef FASHELL_H
#define FASHELL_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <setjmp.h>
#include <readline/readline.h>
#include <readline/history.h>

#define MAX_LINE 100

/* miasma theme palette, truecolor */
#define FS_ACCENT "\033[38;2;120;130;75m"
#define FS_FG     "\033[38;2;194;194;176m"
#define FS_MUTED  "\033[38;2;102;102;102m"
#define FS_RESET  "\033[0m"

/* \001..\002 tells readline these bytes are zero-width */
#define FS_C(x) "\001" x "\002"

#endif
