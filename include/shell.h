#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>

#define HIST_SIZE 100

/* Global runtime variables declarations */
extern char *history[HIST_SIZE];
extern int hist_count;
extern pid_t background_jobs[100];
extern int job_count;

/* Signal and core processing prototypes */
void handle_sigint(int sig);
int route_command(char **args);

/* History management persistent prototypes */
void load_history(void);
void add_to_history(const char *cmd);
void print_history(void);
void free_history(void);

#endif
