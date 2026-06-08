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
#include <linux/limits.h>

#define HIST_SIZE 100
#define MAX_ALIASES 50

typedef struct {
    char *name;
    char *value;
} Alias;

extern Alias aliases[MAX_ALIASES];
extern int alias_count;
extern char *history[HIST_SIZE];
extern int hist_count;

char **split_line(char *line);
int route_command(char **args);
void add_to_history(const char *cmd);
void print_history(void);
void free_history(void);
void load_default_aliases(void);
void load_config_file(void);
char *read_line_with_autocomplete(void);

#endif