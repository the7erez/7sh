#include "../include/shell.h"

char **split_line(char *line) {
    int bufsize = 64;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) { perror("shell: allocation error"); exit(EXIT_FAILURE); }

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
        else {
            int start = i;
            while (i < len && line[i] != ' ' && line[i] != '\t' && line[i] != '\r' && line[i] != '\n') i++;
            int token_len = i - start;
            token = malloc((token_len + 1) * sizeof(char));
            strncpy(token, &line[start], token_len);
            token[token_len] = '\0';
        }

        tokens[position++] = token;
        if (position >= bufsize) {
            bufsize += 64;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) { perror("shell: reallocation error"); exit(EXIT_FAILURE); }
        }
    }
    tokens[position] = NULL;
    return tokens;
}