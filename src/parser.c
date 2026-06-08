#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/parser.h"

/* Global alias table initialization */
Alias alias_table[MAX_ALIASES];
int alias_count = 0;

/**
 * Expands tilde (~) prefix to the user's HOME directory path
 */
void expand_tilde(char *arg, char *out_arg) {
    if (arg && arg[0] == '~') {
        char *home = getenv("HOME");
        if (home != NULL) {
            if (arg[1] == '\0') {
                strcpy(out_arg, home);
            } else if (arg[1] == '/') {
                sprintf(out_arg, "%s%s", home, arg + 1);
            } else {
                strcpy(out_arg, arg);
            }
        } else {
            strcpy(out_arg, arg);
        }
    } else {
        strcpy(out_arg, arg);
    }
}

/**
 * Strips single and double quotes from a given string
 */
void strip_quotes(char *src, char *dest) {
    int j = 0;
    for (int i = 0; src[i] != '\0'; i++) {
        if (src[i] != '\'' && src[i] != '"') {
            dest[j++] = src[i];
        }
    }
    dest[j] = '\0';
}

/**
 * Adds or updates an alias in the global alias table
 */
void add_alias(const char *key, const char *value) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].key, key) == 0) {
            strip_quotes((char*)value, alias_table[i].value);
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        strcpy(alias_table[alias_count].key, key);
        strip_quotes((char*)value, alias_table[alias_count].value);
        alias_count++;
    }
}

/**
 * Resolves a command key to its registered alias value
 */
char* resolve_alias(const char *key) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(alias_table[i].key, key) == 0) {
            return alias_table[i].value;
        }
    }
    return NULL;
}

/**
 * Splits the input line into separate tokens dynamically with no upper size bounds
 */
char **split_line(char *line) {
    /* Dynamic check for alias definitions safely */
    if (strchr(line, '=') != NULL) {
        char *line_copy = strdup(line); // تخصيص ديناميكي آمن تماماً وحجمه مرن
        char *key = strtok(line_copy, "=");
        char *value = strtok(NULL, "");
        
        if (key && value) {
            char *clean_key = malloc(strlen(key) + 1);
            if (strncmp(key, "alias ", 6) == 0) {
                strcpy(clean_key, key + 6);
            } else {
                strcpy(clean_key, key);
            }
            
            char *trimmed_key = strtok(clean_key, " \t\r\n");
            if (trimmed_key) {
                add_alias(trimmed_key, value);
            }
            free(clean_key);
        }
        free(line_copy);
        
        char **empty_args = malloc(sizeof(char*));
        empty_args[0] = NULL;
        return empty_args;
    }

    int bufsize = 64;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) { 
        perror("7sh: allocation error"); 
        exit(EXIT_FAILURE); 
    }

    int i = 0;
    int len = strlen(line);

    while (i < len) {
        while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')) {
            i++;
        }
        if (i >= len) break;

        if (line[i] == '"') {
            i++; 
            int start = i;
            while (i < len && line[i] != '"') i++;
            int token_len = i - start;
            token = malloc((token_len + 1) * sizeof(char));
            strncpy(token, &line[start], token_len);
            token[token_len] = '\0';
            if (line[i] == '"') i++; 
        } 
        else if (line[i] == '\'') {
            i++; 
            int start = i;
            while (i < len && line[i] != '\'') i++;
            int token_len = i - start;
            token = malloc((token_len + 1) * sizeof(char));
            strncpy(token, &line[start], token_len);
            token[token_len] = '\0';
            if (line[i] == '\'') i++; 
        }
        else {
            int start = i;
            while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n' && line[i] != '"' && line[i] != '\'') i++;
            int token_len = i - start;
            token = malloc((token_len + 1) * sizeof(char));
            strncpy(token, &line[start], token_len);
            token[token_len] = '\0';
        }

        if (token[0] == '$' && strlen(token) > 1) {
            char *env_val = getenv(token + 1);
            if (env_val) {
                free(token);
                token = strdup(env_val);
            }
        }

        /* Safe buffer calculation based on dynamically computed size constraints */
        size_t needed_space = strlen(token) + 256; 
        if (needed_space < 500) needed_space = 500;

        char *expanded = malloc(needed_space);
        expand_tilde(token, expanded);
        free(token);

        if (position == 0) {
            char *alias_val = resolve_alias(expanded);
            if (alias_val != NULL) {
                char *alias_copy = strdup(alias_val);
                char *sub_token = strtok(alias_copy, " \t\r\n");
                while (sub_token != NULL) {
                    tokens[position++] = strdup(sub_token);
                    if (position >= bufsize) {
                        bufsize += 64;
                        tokens = realloc(tokens, bufsize * sizeof(char*));
                        if (!tokens) { perror("7sh: reallocation error"); exit(EXIT_FAILURE); }
                    }
                    sub_token = strtok(NULL, " \t\r\n");
                }
                free(alias_copy);
                free(expanded);
                continue;
            }
        }

        tokens[position++] = expanded; // إسناد المؤشر المخصص ديناميكياً مباشرة بدون حد ثابت للـ Copy
        
        if (position >= bufsize) {
            bufsize += 64;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) { 
                perror("7sh: reallocation error"); 
                exit(EXIT_FAILURE); 
            }
        }
    }
    tokens[position] = NULL;
    return tokens;
}