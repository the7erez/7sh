#include "../include/shell.h"
#include "../include/parser.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Global runtime state architecture definitions */
char *history[HIST_SIZE];
int hist_count = 0;
pid_t background_jobs[100];
int job_count = 0;

/**
 * Calculates absolute file layout descriptors pointing to (~/.7sh_history)
 */
static void get_history_path(char *path, size_t max_len) {
    char *home = getenv("HOME");
    if (home) {
        snprintf(path, max_len, "%s/.7sh_history", home);
    } else {
        snprintf(path, max_len, ".7sh_history");
    }
}

/**
 * Hydrates active buffer memory with log entries from disk at boot up
 */
void load_history(void) {
    char path[1024];
    get_history_path(path, sizeof(path));
    
    FILE *file = fopen(path, "r");
    if (!file) return;

    char line[1024];
    while (fgets(line, sizeof(line), file) && hist_count < HIST_SIZE) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) > 0) {
            history[hist_count++] = strdup(line);
        }
    }
    fclose(file);
}

/**
 * Flushes distinct execution lines to local registers and system streams
 */
void add_to_history(const char *cmd) {
    if (strlen(cmd) == 0) return;

    if (hist_count > 0 && strcmp(history[hist_count - 1], cmd) == 0) {
        return;
    }

    if (hist_count < HIST_SIZE) {
        history[hist_count++] = strdup(cmd);
    } else {
        free(history[0]);
        for (int i = 1; i < HIST_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HIST_SIZE - 1] = strdup(cmd);
    }

    char path[1024];
    get_history_path(path, sizeof(path));
    FILE *file = fopen(path, "a");
    if (file) {
        fprintf(file, "%s\n", cmd);
        fclose(file);
    }
}

/**
 * Displays user sequential historical transaction records on the console
 */
void print_history(void) {
    for (int i = 0; i < hist_count; i++) {
        printf(" %d  %s\n", i + 1, history[i]);
    }
}

/**
 * Safely deallocates system heap slices assigned to the history buffer
 */
void free_history(void) {
    for (int i = 0; i < hist_count; i++) {
        free(history[i]);
    }
}

/**
 * Spawns isolated process workspaces executing detached or interactive system processes
 */
static int launch_proc(char **args) {
    pid_t pid;
    int status;
    int background = 0;
    int argc_count = 0;

    while (args[argc_count] != NULL) argc_count++;
    if (argc_count > 0 && strcmp(args[argc_count - 1], "&") == 0) {
        background = 1;
        args[argc_count - 1] = NULL; 
    }

    pid = fork();
    if (pid == 0) { 
        /* Child Process Execution Context */
        if (background) {
            /* * Immunize background children from Ctrl+C (SIGINT) and Ctrl+Z (SIGTSTP).
             * SIGHUP is NOT ignored, and setsid() is omitted, keeping the job linked 
             * to the terminal group so it terminates naturally when Kitty closes.
             */
            signal(SIGINT, SIG_IGN);
            signal(SIGTSTP, SIG_IGN);
        }

        for (int j = 0; args[j] != NULL; j++) {
            if (strcmp(args[j], ">") == 0) {
                args[j] = NULL;
                int fd = open(args[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("7sh: output error"); exit(EXIT_FAILURE); }
                dup2(fd, STDOUT_FILENO); 
                close(fd); 
                break;
            } else if (strcmp(args[j], "<") == 0) {
                args[j] = NULL;
                int fd = open(args[j + 1], O_RDONLY);
                if (fd < 0) { perror("7sh: input error"); exit(EXIT_FAILURE); }
                dup2(fd, STDIN_FILENO); 
                close(fd); 
                break;
            }
        }
        
        if (execvp(args[0], args) == -1) {
            if (errno == ENOENT) {
                fprintf(stderr, "7sh: command not found: %s\n", args[0]);
            } else {
                perror("7sh: execution fault");
            }
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        perror("7sh: fork process allocation error");
    } else { 
        /* Parent Process Execution Context */
        if (background) {
            if (job_count < 100) {
                background_jobs[job_count++] = pid;
            }
            printf("[+] Job %d started in background\n", pid);
            
            /* Clean up zombie tracking entries asynchronously */
            waitpid(pid, &status, WNOHANG);
        } else {
            do {
                waitpid(pid, &status, WUNTRACED);
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
            return WEXITSTATUS(status);
        }
    }
    return 0;
}

/**
 * Establishes system multi-stage pipelines transferring descriptors across child workflows
 */
static int exec_pipe(char **args, int pipe_idx) {
    args[pipe_idx] = NULL;
    char **cmd1 = args;
    char **cmd2 = &args[pipe_idx + 1];
    int pipefd[2];

    if (pipe(pipefd) == -1) { perror("7sh: channel creation error"); return 1; }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd1[0], cmd1); 
        perror("7sh: pipeline source error"); 
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        execvp(cmd2[0], cmd2); 
        perror("7sh: pipeline target error"); 
        exit(EXIT_FAILURE);
    }

    close(pipefd[0]); close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    return 0;
}

/**
 * Evaluates execution pathways and directs tokens to core built-ins or custom launchers
 */
int route_command(char **args) {
    if (args == NULL || args[0] == NULL) return 0;

    if (strcmp(args[0], "~") == 0) {
        char *home = getenv("HOME");
        if (home != NULL) {
            chdir(home);
        }
        return 0;
    }

    if (strcmp(args[0], "source") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "7sh: source: filename argument required\n");
            return 1;
        }
        FILE *file = fopen(args[1], "r");
        if (!file) { perror("7sh: source mapping failure"); return 1; }

        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0';
            if (strlen(line) == 0 || line[0] == '#') continue;
            char **sub_args = split_line(line);
            route_command(sub_args);
            free(sub_args);
        }
        fclose(file);
        return 0;
    }

    if (strcmp(args[0], "disown") == 0) {
        if (job_count > 0) {
            printf("[INFO] Disowned active background job with PID: %d\n", background_jobs[job_count - 1]);
            job_count--;
            return 0;
        } else {
            fprintf(stderr, "7sh: disown: no background jobs running.\n");
            return 1;
        }
    }

    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            char *home = getenv("HOME"); 
            if (home) chdir(home);
        } else {
            if (chdir(args[1]) != 0) perror("7sh: directory access denied");
        }
        return 0;
    }
    
    if (strcmp(args[0], "history") == 0) { print_history(); return 0; }
    if (strcmp(args[0], "exit") == 0) return 0;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) return exec_pipe(args, i);
    }

    /* Fallback framework to inject background token for 'qs' if missing */
    int argc_count = 0;
    int has_bg = 0;
    while (args[argc_count] != NULL) {
        if (strcmp(args[argc_count], "&") == 0) {
            has_bg = 1;
        }
        argc_count++;
    }

    if (!has_bg && argc_count > 0 && strcmp(args[0], "qs") == 0) {
        char **new_args = malloc((argc_count + 2) * sizeof(char*));
        for (int i = 0; i < argc_count; i++) {
            new_args[i] = args[i];
        }
        new_args[argc_count] = "&";      
        new_args[argc_count + 1] = NULL; 
        
        int ret = launch_proc(new_args);
        free(new_args);
        return ret;
    }

    return launch_proc(args);
}