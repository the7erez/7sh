#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

#define MAX_ALIASES 100

typedef struct {
    char key[50];
    char value[250];
} Alias;

extern Alias alias_table[MAX_ALIASES];
extern int alias_count;

char **split_line(char *line);
void expand_tilde(char *arg, char *out_arg);
void strip_quotes(char *src, char *dest);
void add_alias(const char *key, const char *value);
char* resolve_alias(const char *key);

#endif