#include "../include/shell.h"
#include <errno.h>

Alias aliases[MAX_ALIASES];
int alias_count = 0;

void add_alias(const char *name, const char *value) {
    if (alias_count < MAX_ALIASES) {
        aliases[alias_count].name = strdup(name);
        aliases[alias_count].value = strdup(value);
        alias_count++;
    }
}

void load_default_aliases(void) {
    add_alias("ll", "ls -l");
    add_alias("la", "ls -A");
    add_alias("l", "ls -CF");
    add_alias("rmrf", "rm -rf");
    add_alias("grep", "grep --color=auto");
}

char *check_alias(const char *cmd) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(cmd, aliases[i].name) == 0) return aliases[i].value;
    }
    return NULL;
}

char *history[HIST_SIZE];
int hist_count = 0;
pid_t background_jobs[100];
int job_count = 0;

void add_to_history(const char *cmd) {
    if (hist_count < HIST_SIZE) {
        history[hist_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < HIST_SIZE; i++) history[i - 1] = history[i];
        history[HIST_SIZE - 1] = strdup(cmd);
    }
}

void print_history(void) {
    for (int i = 0; i < hist_count; i++) printf(" %d  %s\n", i + 1, history[i]);
}

void free_history(void) {
    for (int i = 0; i < hist_count; i++) free(history[i]);
}

static int launch_proc(char **args) {
    pid_t pid;
    int status;
    int background = 0;
    int i = 0;

    while (args[i] != NULL) i++;
    if (i > 0 && strcmp(args[i-1], "&") == 0) {
        background = 1;
        args[i-1] = NULL;
    }

    pid = fork();
    if (pid == 0) { 
        for (int j = 0; args[j] != NULL; j++) {
            if (strcmp(args[j], ">") == 0) {
                args[j] = NULL;
                int fd = open(args[j+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("shell"); exit(EXIT_FAILURE); }
                dup2(fd, STDOUT_FILENO); close(fd); break;
            } else if (strcmp(args[j], "<") == 0) {
                args[j] = NULL;
                int fd = open(args[j+1], O_RDONLY);
                if (fd < 0) { perror("shell"); exit(EXIT_FAILURE); }
                dup2(fd, STDIN_FILENO); close(fd); break;
            }
        }
        if (execvp(args[0], args) == -1) {
            if (errno == ENOENT) fprintf(stderr, "shell: command not found: %s\n", args[0]);
            else perror("shell");
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        perror("shell: fork error");
    } else { 
        if (background) {
            printf("[+] Job started in background with PID: %d\n", pid);
            if (job_count < 100) background_jobs[job_count++] = pid;
        } else {
            do {
                waitpid(pid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        }
    }
    return 1;
}

static int exec_pipe(char **args, int pipe_idx) {
    args[pipe_idx] = NULL;
    char **cmd1 = args;
    char **cmd2 = &args[pipe_idx + 1];
    int pipefd[2];

    if (pipe(pipefd) == -1) { perror("shell: pipe error"); return 1; }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd1[0], cmd1); perror("shell"); exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd2[0], cmd2); perror("shell"); exit(EXIT_FAILURE);
    }

    close(pipefd[0]); close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    return 1;
}

int route_command(char **args) {
    if (args[0] == NULL) return 1;

    char *real_cmd = check_alias(args[0]);
    if (real_cmd) {
        char full_line[2048] = "";
        strcpy(full_line, real_cmd);
        for (int i = 1; args[i] != NULL; i++) {
            strcat(full_line, " ");
            strcat(full_line, args[i]);
        }
        char **new_args = split_line(full_line);
        int status = route_command(new_args);
        free(new_args);
        return status;
    }

    if (strcmp(args[0], "alias") == 0) {
        if (args[1] != NULL) {
            char *eq = strchr(args[1], '=');
            if (eq) { *eq = '\0'; add_alias(args[1], eq + 1); }
        }
        return 1;
    }

    if (strcmp(args[0], "source") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "shell: source: filename argument required\n");
            return 1;
        }
        FILE *file = fopen(args[1], "r");
        if (!file) { perror("shell: source"); return 1; }

        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) == 0 || line[0] == '#') continue;
            char **sub_args = split_line(line);
            route_command(sub_args);
            free(sub_args);
        }
        fclose(file);
        printf("Configuration reloaded successfully!\n");
        return 1;
    }

    if (strcmp(args[0], "disown") == 0) {
        if (job_count > 0) {
            printf("Disowned last background job (PID: %d).\n", background_jobs[job_count-1]);
            job_count--;
        } else {
            fprintf(stderr, "shell: disown: no background jobs running.\n");
        }
        return 1;
    }

    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            char *home = getenv("HOME"); if (home) chdir(home);
        } else {
            if (chdir(args[1]) != 0) perror("shell");
        }
        return 1;
    }

    if (strcmp(args[0], "history") == 0) { print_history(); return 1; }
    if (strcmp(args[0], "exit") == 0) return 0;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) return exec_pipe(args, i);
    }

    return launch_proc(args);
}