#include "../include/shell.h"
#include "../include/parser.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <termios.h>
#include <sys/wait.h>
#include <strings.h>

/* External environment variable array provided by the system */
extern char **environ;

/* Forward Declarations to fix implicit declaration errors */
int execute_single_command(char **args);
int parse_and_execute_line(char *line);

/* Global terminal tracking variables to rescue terminal state during SIGINT (Ctrl+C) */
static struct termios orig_termios;
static char *global_buf = NULL;
static int *global_len = NULL;
static int *global_pos = NULL;

/**
 * Signal handler for SIGINT (Ctrl+C).
 * Resets the buffer, position, and restores Cooked Mode to prevent residual autocomplete artifacts.
 */
void handle_sigint(int sig) {
    (void)sig;
    
    /* Flush and reset the active input line buffer state safely */
    if (global_buf && global_len && global_pos) {
        memset(global_buf, 0, 1024);
        *global_len = 0;
        *global_pos = 0;
    }
    
    /* Force restore terminal configuration back to baseline Cooked Mode immediately */
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    
    printf("\n\x1b[36m❯\x1b[0m ");
    fflush(stdout);
}

/**
 * Computes terminal visible character width for UTF-8 compliant strings
 */
int get_visual_width(const char *str, int end_pos) {
    int width = 0;
    int i = 0;
    while (i < end_pos && str[i] != '\0') {
        if ((str[i] & 0xC0) != 0x80) width++;
        i++;
    }
    return width;
}

/**
 * Atomic line rendering function to redraw terminal state consistently
 */
void refresh_line(const char *prompt, const char *buf, int pos) {
    printf("\r\33[2K"); 
    printf("%s%s", prompt, buf);
    
    int total_width = get_visual_width(buf, strlen(buf));
    int cursor_width = get_visual_width(buf, pos);
    int move_back = total_width - cursor_width;
    
    for (int i = 0; i < move_back; i++) printf("\b");
    fflush(stdout);
}

/**
 * Generates the customized or default fallback shell prompt layout
 */
void get_shell_prompt(char *prompt_out, size_t max_len) {
    char *ps1 = getenv("PS1");
    if (ps1 != NULL) {
        snprintf(prompt_out, max_len, "%s", ps1);
    } else {
        char hostname[64] = "7sh";
        FILE *f = fopen("/etc/hostname", "r");
        if (f) {
            if (fgets(hostname, sizeof(hostname), f)) {
                hostname[strcspn(hostname, "\r\n")] = '\0';
            }
            fclose(f);
        }
        snprintf(prompt_out, max_len, "\x1b[38;5;242m%s \x1b[36m❯\x1b[0m ", hostname);
    }
}

/**
 * Initializes and creates a default .7shrc config file if missing
 */
void initialize_config_file(const char *path) {
    if (access(path, F_OK) == 0) return;

    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "# vim: set ft=sh :\n\n");
        fprintf(file, "# --- 7sh System Runcom Configuration ---\n\n");
        fprintf(file, "export PS1=\"\\x1b[32m7erez@arch\\x1b[0m:\\x1b[34m\\w\\x1b[0m❯ \"\n\n");
        fprintf(file, "if command -v eza >/dev/null 2>&1; then\n");
        fprintf(file, "    alias ls=\"eza --icons --color=always\"\n");
        fprintf(file, "    alias ll=\"eza -l --icons --git\"\n");
        fprintf(file, "    alias la=\"eza -a --icons\"\n");
        fprintf(file, "else\n");
        fprintf(file, "    alias ls=\"ls --color=auto\"\n");
        fprintf(file, "    alias ll=\"ls -l --color=auto\"\n");
        fprintf(file, "    alias la=\"ls -A --color=auto\"\n");
        fprintf(file, "fi\n\n");
        fclose(file);
    }
}

/**
 * Executes a single command array after stripping any trailing background tokens
 */
int execute_single_command(char **args) {
    if (args == NULL || args[0] == NULL) return 1;

    int i = 0;
    while (args[i] != NULL) i++;
    
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        args[i - 1] = NULL; 
    }

    return route_command(args); 
}

/**
 * High-level pipeline parser with precise foreground/background workflow separation
 */
int parse_and_execute_line(char *line) {
    if (line == NULL || strlen(line) == 0) return 1;

    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    char *commands[64];
    int bg_flags[64] = {0};
    int cmd_count = 0;

    char *ptr = line;
    commands[cmd_count] = ptr;
    cmd_count++;

    while (*ptr) {
        if (*ptr == '&' && *(ptr + 1) == '&') {
            *ptr = '\0'; *(ptr + 1) = '\0'; ptr += 2;
            commands[cmd_count] = ptr;
            cmd_count++;
        } else if (*ptr == '|' && *(ptr + 1) == '|') {
            *ptr = '\0'; *(ptr + 1) = '\0'; ptr += 2;
            commands[cmd_count] = ptr;
            cmd_count++;
        } else if (*ptr == '&') {
            *ptr = '\0'; ptr += 1;
            bg_flags[cmd_count - 1] = 1;
            commands[cmd_count] = ptr;
            cmd_count++;
        } else if (*ptr == ';') {
            *ptr = '\0'; ptr += 1;
            commands[cmd_count] = ptr;
            cmd_count++;
        } else {
            ptr++;
        }
    }

    for (int i = 0; i < cmd_count; i++) {
        char *cmd_trim = commands[i];
        while (*cmd_trim == ' ' || *cmd_trim == '\t') cmd_trim++;

        if (strlen(cmd_trim) == 0) continue;

        char **args = split_line(cmd_trim);
        if (args && args[0] != NULL) {
            
            if (bg_flags[i] == 1) {
                int argc = 0;
                while (args[argc] != NULL) argc++;
                args = realloc(args, (argc + 2) * sizeof(char*));
                args[argc] = strdup("&");
                args[argc + 1] = NULL;
            }

            execute_single_command(args);
            free(args);

            if (bg_flags[i] == 1) {
                usleep(50000);
            }
        }
    }

    return 1;
}

/**
 * Custom environment/profile loader tailored for both interactive and login shells.
 * Uses the built-in 'source' mechanism to load files directly inside the main process.
 */
void load_config_file(int is_login) {
    char config_path[1024];
    char *home = getenv("HOME");
    if (!home) return;
    
    /* 1. If flagged as login shell, execute the profile config in-process */
    if (is_login) {
        snprintf(config_path, sizeof(config_path), "%s/.7sh_profile", home);
        if (access(config_path, F_OK) == 0) {
            char **args = malloc(3 * sizeof(char*));
            args[0] = strdup("source");
            args[1] = strdup(config_path);
            args[2] = NULL;
            route_command(args);
            free(args[0]); free(args[1]); free(args);
        }
    }

    /* 2. Load the standard interactive runtime config (.7shrc) in-process */
    snprintf(config_path, sizeof(config_path), "%s/.7shrc", home);
    initialize_config_file(config_path);

    char **args = malloc(3 * sizeof(char*));
    args[0] = strdup("source");
    args[1] = strdup(config_path);
    args[2] = NULL;
    route_command(args);
    free(args[0]); free(args[1]); free(args);
}

/**
 * Autocomplete and line editing utility operating over temporary raw termios configs
 */
char *read_line_with_autocomplete(void) {
    struct termios newt;
    char *buf = malloc(1024 * sizeof(char));
    int pos = 0;      
    int len = 0;      
    int c;
    int current_hist_idx = hist_count; 
    char prompt_str[PATH_MAX + 128];

    get_shell_prompt(prompt_str, sizeof(prompt_str));

    if (!buf) { perror("shell: allocation error"); exit(EXIT_FAILURE); }
    memset(buf, 0, 1024);

    /* Bind tracking pointers to global variables for runtime clearing under SIGINT context */
    global_buf = buf;
    global_len = &len;
    global_pos = &pos;

    /* Cache current terminal context parameters safely */
    tcgetattr(STDIN_FILENO, &orig_termios);
    newt = orig_termios;
    newt.c_lflag &= ~(ICANON | ECHO);
    
    /* Engage operational raw key tracking constraints */
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    refresh_line(prompt_str, buf, pos);

    while (1) {
        c = getchar();

        if (c == '\n' || c == EOF) {
            buf[len] = '\0'; putchar('\n'); break;
        }
        else if (c == 12) { 
            printf("\033[H\033[J"); 
            fflush(stdout);
            refresh_line(prompt_str, buf, pos);
        }
        else if (c == 127) { 
            if (pos > 0) {
                int bytes_to_delete = 1;
                while (pos - bytes_to_delete > 0 && (buf[pos - bytes_to_delete] & 0xC0) == 0x80) {
                    bytes_to_delete++;
                }
                for (int i = pos - bytes_to_delete; i < len - bytes_to_delete; i++) {
                    buf[i] = buf[i + bytes_to_delete];
                }
                pos -= bytes_to_delete; len -= bytes_to_delete; buf[len] = '\0';
                refresh_line(prompt_str, buf, pos);
            }
        }
        else if (c == 21 || c == 8) { 
            memset(buf, 0, 1024);
            pos = 0; len = 0;
            refresh_line(prompt_str, buf, pos);
        }
        else if (c == 23) { 
            while (pos > 0 && buf[pos-1] == ' ') {
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i+1];
                pos--; len--; buf[len] = '\0';
            }
            while (pos > 0 && buf[pos-1] != ' ') {
                int b = 1;
                while (pos - b > 0 && (buf[pos - b] & 0xC0) == 0x80) b++;
                for (int i = pos - b; i < len - b; i++) buf[i] = buf[i+b];
                pos -= b; len -= b; buf[len] = '\0';
                refresh_line(prompt_str, buf, pos);
            }
        }
        else if (c == 27) { 
            int next1 = getchar(); 
            if (next1 == 127) {
                while (pos > 0 && buf[pos-1] == ' ') {
                    for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i+1];
                    pos--; len--; buf[len] = '\0';
                }
                while (pos > 0 && buf[pos-1] != ' ') {
                    int b = 1;
                    while (pos - b > 0 && (buf[pos - b] & 0xC0) == 0x80) b++;
                    for (int i = pos - b; i < len - b; i++) buf[i] = buf[i+b];
                    pos -= b; len -= b; buf[len] = '\0';
                }
                refresh_line(prompt_str, buf, pos);
                continue;
            }

            int next2 = getchar();
            if (next1 == 91) { 
                if (next2 == 65) { 
                    if (current_hist_idx > 0) {
                        current_hist_idx--;
                        strcpy(buf, history[current_hist_idx]); pos = len = strlen(buf);
                        refresh_line(prompt_str, buf, pos);
                    }
                } 
                else if (next2 == 66) { 
                    if (current_hist_idx < hist_count) {
                        current_hist_idx++;
                        if (current_hist_idx < hist_count) strcpy(buf, history[current_hist_idx]);
                        else buf[0] = '\0';
                        pos = len = strlen(buf); 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 67) { 
                    if (pos < len) {
                        int step = 1;
                        while(pos + step < len && (buf[pos + step] & 0xC0) == 0x80) step++;
                        pos += step; 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 68) { 
                    if (pos > 0) {
                        int step = 1;
                        while(pos - step > 0 && (buf[pos - step] & 0xC0) == 0x80) step++;
                        pos -= step; 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 51) { 
                    int next3 = getchar();
                    if (next3 == 126 && pos < len) {
                        int bytes_to_del = 1;
                        while (pos + bytes_to_del < len && (buf[pos + bytes_to_del] & 0xC0) == 0x80) bytes_to_del++;
                        for (int i = pos; i < len - bytes_to_del; i++) buf[i] = buf[i + bytes_to_del];
                        len -= bytes_to_del; buf[len] = '\0';
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 49) { 
                    int next3 = getchar(); int next4 = getchar(); int next5 = getchar(); 
                    if (next3 == 59 && next4 == 53) {
                        if (next5 == 67) { 
                            while (pos < len) {
                                int step = 1; while(pos + step < len && (buf[pos + step] & 0xC0) == 0x80) step++;
                                pos += step; if (buf[pos] == ' ') break;
                            }
                            refresh_line(prompt_str, buf, pos);
                        }
                        else if (next5 == 68) { 
                            while (pos > 0) {
                                int step = 1; while(pos - step > 0 && (buf[pos - step] & 0xC0) == 0x80) step++;
                                pos -= step; if (buf[pos-1] == ' ' || pos == 0) break;
                            }
                            refresh_line(prompt_str, buf, pos);
                        }
                    }
                }
            }
        }
        else if (c == '\t') { 
            buf[pos] = '\0';
            char *last_space = strrchr(buf, ' ');
            char *full_word = last_space ? last_space + 1 : buf;
            int is_command_mode = (last_space == NULL); 
            char *matches[512]; 
            int is_directory[512] = {0};
            int match_count = 0;
            int search_len = strlen(full_word);

            if (is_command_mode && search_len > 0) {
                char *path_env = getenv("PATH");
                if (path_env) {
                    char *path_copy = strdup(path_env);
                    char *dir_path = strtok(path_copy, ":");
                    while (dir_path != NULL) {
                        DIR *d = opendir(dir_path);
                        if (d) {
                            struct dirent *dir;
                            while ((dir = readdir(d)) != NULL) {
                                if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                                if (strncmp(dir->d_name, full_word, search_len) == 0) {
                                    int duplicate = 0;
                                    for (int i = 0; i < match_count; i++) {
                                        if (strcmp(matches[i], dir->d_name) == 0) { duplicate = 1; break; }
                                    }
                                    if (!duplicate) {
                                        matches[match_count] = strdup(dir->d_name);
                                        is_directory[match_count] = 0; 
                                        match_count++;
                                        if (match_count >= 512) break;
                                    }
                                }
                            }
                            closedir(d);
                        }
                        if (match_count >= 512) break;
                        dir_path = strtok(NULL, ":");
                    }
                    free(path_copy);
                }
            } 
            else {
                char dir_path[1024] = "."; 
                char search_term[256] = "";
                char *last_slash = strrchr(full_word, '/');
                if (last_slash) {
                    int dir_len = last_slash - full_word + 1;
                    strncpy(dir_path, full_word, dir_len); dir_path[dir_len] = '\0';
                    strcpy(search_term, last_slash + 1);
                } else {
                    strcpy(search_term, full_word);
                }

                if (dir_path[0] == '~') {
                    char expanded_dir[1024];
                    char *home = getenv("HOME");
                    if (home) {
                        if (dir_path[1] == '\0') strcpy(expanded_dir, home);
                        else if (dir_path[1] == '/') snprintf(expanded_dir, sizeof(expanded_dir), "%s%s", home, dir_path + 1);
                        else strcpy(expanded_dir, dir_path);
                        strcpy(dir_path, expanded_dir);
                    }
                } else if (search_term[0] == '~' && !last_slash) {
                    char *home = getenv("HOME");
                    if (home) { strcpy(dir_path, home); search_term[0] = '\0'; }
                }

                search_len = strlen(search_term);
                DIR *d = opendir(dir_path);
                if (d) {
                    struct dirent *dir;
                    while ((dir = readdir(d)) != NULL) {
                        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                        if (search_len == 0 || strncmp(dir->d_name, search_term, search_len) == 0) {
                            matches[match_count] = strdup(dir->d_name);
                            is_directory[match_count] = (dir->d_type == DT_DIR);
                            match_count++; if (match_count >= 512) break;
                        }
                    }
                    closedir(d);
                }
            }

            if (match_count > 1) {
                for (int i = 0; i < match_count - 1; i++) {
                    for (int j = i + 1; j < match_count; j++) {
                        if (strcasecmp(matches[i], matches[j]) > 0) {
                            char *temp_m = matches[i]; matches[i] = matches[j]; matches[j] = temp_m;
                            int temp_d = is_directory[i]; is_directory[i] = is_directory[j]; is_directory[j] = temp_d;
                        }
                    }
                }
            }

            if (match_count == 1) {
                char *completion = matches[0] + search_len;
                while (*completion) { buf[pos++] = *completion; len++; completion++; }
                if (is_command_mode) { buf[pos++] = ' '; len++; } 
                else if (is_directory[0]) { buf[pos++] = '/'; len++; }
                free(matches[0]);
                refresh_line(prompt_str, buf, pos);
            }
            else if (match_count > 1) {
                int l = 0;
                while (1) {
                    char current_char = matches[0][search_len + l];
                    if (current_char == '\0') break;
                    int all_match = 1;
                    for (int i = 1; i < match_count; i++) {
                        if (matches[i][search_len + l] != current_char) { all_match = 0; break; }
                    }
                    if (all_match) { buf[pos++] = current_char; len++; l++; }
                    else break;
                }
                
                if (l == 0) {
                    printf("\n");
                    int max_width = 0;
                    for (int i = 0; i < match_count; i++) {
                        int len = strlen(matches[i]);
                        if (len > max_width) max_width = len;
                    }
                    max_width += 3;
                    int term_width = 80; 
                    int cols = term_width / max_width;
                    if (cols <= 0) cols = 1;

                    for (int i = 0; i < match_count; i++) {
                        if (is_command_mode) {
                            printf("\x1b[1;32m%-*s\x1b[0m", max_width, matches[i]); 
                        } else if (is_directory[i]) {
                            char dir_buf[256];
                            snprintf(dir_buf, sizeof(dir_buf), "%s/", matches[i]);
                            printf("\x1b[1;34m%-*s\x1b[0m", max_width, dir_buf);
                        } else {
                            printf("%-*s", max_width, matches[i]); 
                        }
                        if ((i + 1) % cols == 0) printf("\n");
                    }
                    if (match_count % cols != 0) printf("\n");
                }
                for(int i = 0; i < match_count; i++) free(matches[i]);
                refresh_line(prompt_str, buf, pos);
            }
        }
        else { 
            if (len < 1023) {
                for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
                buf[pos] = c;
                pos++; len++; buf[len] = '\0';
                refresh_line(prompt_str, buf, pos);
            }
        }
    }
    
    /* Safely restore original baseline settings before dropping context back */
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
    return buf;
}

/**
 * Application context main entry point with argument vector parsing capabilities
 */
int main(int argc, char **argv) {
    char *line; 
    int status = 1;
    int is_login_shell = 0;

    /* Parse login and initialization flags provided by terminal environment loops */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--login") == 0) {
            is_login_shell = 1;
        }
    }

    if (is_login_shell || (argv[0] && argv[0][0] == '-')) {
        is_login_shell = 1;
        setenv("SHLVL", "1", 1);
    }
    
    /* Hook baseline platform system processing signals securely */
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, SIG_IGN);  
    signal(SIGQUIT, SIG_IGN);  
    signal(SIGTTIN, SIG_IGN);  
    signal(SIGTTOU, SIG_IGN);  
    
    load_config_file(is_login_shell);
    load_history();

    while (status) {
        if (!isatty(STDIN_FILENO)) {
            char testing_buf[1024];
            if (fgets(testing_buf, sizeof(testing_buf), stdin) == NULL) break; 
            testing_buf[strcspn(testing_buf, "\n")] = '\0';
            if (strlen(testing_buf) == 0) continue;

            if (strcmp(testing_buf, "exit") == 0) {
                break;
            }

            status = parse_and_execute_line(testing_buf);
        } 
        else {
            line = read_line_with_autocomplete();
            if (strlen(line) == 0) { free(line); continue; }

            if (strcmp(line, "exit") == 0) {
                free(line);
                break; 
            }

            add_to_history(line);
            status = parse_and_execute_line(line);
            free(line); 
        }
    }
    
    free_history();
    return EXIT_SUCCESS;
}