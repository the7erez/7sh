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
#include <strings.h> /* Required for case-insensitive strcasecmp */

/* Forward Declarations to fix implicit declaration errors */
int execute_single_command(char **args);
int parse_and_execute_line(char *line);

/**
 * Signal handler for SIGINT (Ctrl+C) to keep prompt line stable
 */
void handle_sigint(int sig) {
    (void)sig; 
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
        
        fprintf(file, "# Custom core prompt variable layout\n");
        fprintf(file, "export PS1=\"\\x1b[32m7erez@arch\\x1b[0m:\\x1b[34m\\w\\x1b[0m❯ \"\n\n");
        
        fprintf(file, "# Modern CLI Tools Substitutions\n");
        fprintf(file, "if command -v eza >/dev/null 2>&1; then\n");
        fprintf(file, "    alias ls=\"eza --icons --color=always\"\n");
        fprintf(file, "    alias ll=\"eza -l --icons --git\"\n");
        fprintf(file, "    alias la=\"eza -a --icons\"\n");
        fprintf(file, "else\n");
        fprintf(file, "    alias ls=\"ls --color=auto\"\n");
        fprintf(file, "    alias ll=\"ls -l --color=auto\"\n");
        fprintf(file, "    alias la=\"ls -A --color=auto\"\n");
        fprintf(file, "fi\n\n");

        fprintf(file, "if command -v bat >/dev/null 2>&1; then\n");
        fprintf(file, "    alias cat=\"bat\"\n");
        fprintf(file, "fi\n\n");
        
        fprintf(file, "# Core Utilities\n");
        fprintf(file, "alias i=\"yay -S\"\n");
        fprintf(file, "alias rmrf=\"rm -rf\"\n");
        fprintf(file, "alias grep=\"grep --color=auto\"\n");
        
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
    
    /* Strict Cleanup: Strip trailing background character to prevent external binary confusion */
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

    /* Clean inline comments safely before doing token scans */
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';

    char *commands[64];
    int bg_flags[64] = {0}; /* 1 = Asynchronous Background Job, 0 = Standard Foreground */
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
            bg_flags[cmd_count - 1] = 1; /* Mark previous pipeline slice as background target */
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

    int last_status = 0; 
    for (int i = 0; i < cmd_count; i++) {
        char *cmd_trim = commands[i];
        while (*cmd_trim == ' ' || *cmd_trim == '\t') cmd_trim++;

        if (strlen(cmd_trim) == 0) continue;

        /* SAFETY CHECK: If the command is just "disown" and the previous one was a background job, 
           we handle it safely to bypass race conditions */
        if (strcmp(cmd_trim, "disown") == 0 && i > 0 && bg_flags[i-1] == 1) {
            char *disown_args[] = {"disown", NULL};
            route_command(disown_args);
            continue; 
        }

        char **args = split_line(cmd_trim);
        if (args && args[0] != NULL) {
            
            /* If the parser explicitly tagged this command block for background isolation */
            if (bg_flags[i] == 1) {
                int argc = 0;
                while (args[argc] != NULL) argc++;
                args = realloc(args, (argc + 2) * sizeof(char*));
                args[argc] = strdup("&");
                args[argc + 1] = NULL;
            }

            last_status = execute_single_command(args);
            free(args);
            
            /* Give the kernel parent context 50ms to register the PID securely before processing next commands */
            if (bg_flags[i] == 1) {
                usleep(50000);
            }
        }
    }

    (void)last_status;
    return 1;
}

/**
 * Processes runtime runcom profiles to structure working environments
 */
void load_config_file(void) {
    char config_path[1024];
    char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(config_path, sizeof(config_path), "%s/.7shrc", home);
    initialize_config_file(config_path);

    FILE *file = fopen(config_path, "r");
    if (!file) return; 

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0'; 
        if (strlen(line) == 0 || line[0] == '#') continue; 

        parse_and_execute_line(line);
    }
    fclose(file);
}

/**
 * Custom line-noise/interactive input processing system with autocomplete
 */
char *read_line_with_autocomplete(void) {
    struct termios oldt, newt;
    char *buf = malloc(1024 * sizeof(char));
    int pos = 0;      
    int len = 0;      
    int c;
    int current_hist_idx = hist_count; 
    char prompt_str[PATH_MAX + 128];

    get_shell_prompt(prompt_str, sizeof(prompt_str));

    if (!buf) { perror("shell: allocation error"); exit(EXIT_FAILURE); }
    memset(buf, 0, 1024);

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
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

            /* Case-insensitive sorting for matching items */
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
                
                if (is_command_mode) {
                    buf[pos++] = ' '; len++; 
                } else if (is_directory[0]) {
                    buf[pos++] = '/'; len++; 
                }
                free(matches[0]);
                refresh_line(prompt_str, buf, pos);
            }
            /* Multiple matches handling with structured column padding */
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
                    
                    /* Calculate the maximum filename length to determine dynamic column width */
                    int max_width = 0;
                    for (int i = 0; i < match_count; i++) {
                        int len = strlen(matches[i]);
                        if (len > max_width) max_width = len;
                    }
                    max_width += 3; /* Add padding spacing between columns */

                    /* Set standard terminal viewport boundaries and calculate available columns */
                    int term_width = 80; 
                    int cols = term_width / max_width;
                    if (cols <= 0) cols = 1;

                    for (int i = 0; i < match_count; i++) {
                        if (is_command_mode) {
                            /* Render commands in bold green with fixed-width spacing */
                            printf("\x1b[1;32m%-*s\x1b[0m", max_width, matches[i]); 
                        } else if (is_directory[i]) {
                            /* Append directory slash inside the buffer to keep tabular alignment perfect */
                            char dir_buf[256];
                            snprintf(dir_buf, sizeof(dir_buf), "%s/", matches[i]);
                            printf("\x1b[1;34m%-*s\x1b[0m", max_width, dir_buf);
                        } else {
                            /* Render regular files with fixed-width spacing */
                            printf("%-*s", max_width, matches[i]); 
                        }
                        
                        /* Wrap to a new line based on calculated grid density dynamically */
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
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return buf;
}

/**
 * Application context main entry point
 */
int main(void) {
    char *line; int status = 1;
    signal(SIGINT, handle_sigint);
    
    load_config_file();
    load_history();

    while (status) {
        if (!isatty(STDIN_FILENO)) {
            char testing_buf[1024];
            if (fgets(testing_buf, sizeof(testing_buf), stdin) == NULL) break; 
            testing_buf[strcspn(testing_buf, "\n")] = '\0';
            if (strlen(testing_buf) == 0) continue;

            status = parse_and_execute_line(testing_buf);
        } 
        else {
            line = read_line_with_autocomplete();
            if (strlen(line) == 0) { free(line); continue; }

            add_to_history(line);

            status = parse_and_execute_line(line);

            free(line); 
        }
    }
    free_history();
    return EXIT_SUCCESS;
}