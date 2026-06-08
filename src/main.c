#include "../include/shell.h"
#include <signal.h>
#include <unistd.h>

void handle_sigint(int sig) {
    (void)sig; 
    printf("\n\x1b[36m❯\x1b[0m ");
    fflush(stdout);
}

/* Get terminal visible columns used by a UTF-8 string up to end_pos */
int get_visual_width(const char *str, int end_pos) {
    int width = 0;
    int i = 0;
    while (i < end_pos && str[i] != '\0') {
        if ((str[i] & 0xC0) != 0x80) width++;
        i++;
    }
    return width;
}

/* Atomic terminal line redrawing function to ensure mixed layout stability */
void refresh_line(const char *prompt, const char *buf, int pos) {
    printf("\r\33[2K"); /* Clear entire current line and carriage return */
    printf("%s%s", prompt, buf);
    
    /* Calculate precise move-back offset using visual widths */
    int total_width = get_visual_width(buf, strlen(buf));
    int cursor_width = get_visual_width(buf, pos);
    int move_back = total_width - cursor_width;
    
    for (int i = 0; i < move_back; i++) printf("\b");
    fflush(stdout);
}

void load_config_file(void) {
    char config_path[1024];
    char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(config_path, sizeof(config_path), "%s/.7shrc", home);
    FILE *file = fopen(config_path, "r");
    if (!file) return; 

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0'; 
        if (strlen(line) == 0 || line[0] == '#') continue; 

        char **args = split_line(line);
        route_command(args);
        free(args);
    }
    fclose(file);
}

char *read_line_with_autocomplete(void) {
    struct termios oldt, newt;
    char *buf = malloc(1024 * sizeof(char));
    int pos = 0;      
    int len = 0;      
    int c;
    int current_hist_idx = hist_count; 
    char prompt_str[PATH_MAX + 64];

    char cwd[PATH_MAX];
    char *current_dir = (getcwd(cwd, sizeof(cwd)) != NULL) ? strrchr(cwd, '/') : NULL;
    current_dir = current_dir ? current_dir + 1 : cwd;
    snprintf(prompt_str, sizeof(prompt_str), "\x1b[38;5;242m%s \x1b[36m❯\x1b[0m ", current_dir);

    if (!buf) { perror("shell: allocation error"); exit(EXIT_FAILURE); }
    memset(buf, 0, 1024);

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    /* Render initial prompt layout */
    refresh_line(prompt_str, buf, pos);

    while (1) {
        c = getchar();

        if (c == '\n' || c == EOF) {
            buf[len] = '\0'; putchar('\n'); break;
        }
        else if (c == 127 || c == 8) { /* Backspace tracking */
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
        else if (c == 23) { /* Ctrl + W word deletion */
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
        }
        else if (c == 27) { /* Escape sequence routing */
            int next1 = getchar(); int next2 = getchar();
            if (next1 == 91) { 
                if (next2 == 65) { /* History Up */
                    if (current_hist_idx > 0) {
                        current_hist_idx--;
                        strcpy(buf, history[current_hist_idx]); pos = len = strlen(buf);
                        refresh_line(prompt_str, buf, pos);
                    }
                } 
                else if (next2 == 66) { /* History Down */
                    if (current_hist_idx < hist_count) {
                        current_hist_idx++;
                        if (current_hist_idx < hist_count) strcpy(buf, history[current_hist_idx]);
                        else buf[0] = '\0';
                        pos = len = strlen(buf); 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 67) { /* Arrow Right */
                    if (pos < len) {
                        int step = 1;
                        while(pos + step < len && (buf[pos + step] & 0xC0) == 0x80) step++;
                        pos += step; 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 68) { /* Arrow Left */
                    if (pos > 0) {
                        int step = 1;
                        while(pos - step > 0 && (buf[pos - step] & 0xC0) == 0x80) step++;
                        pos -= step; 
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 51) { /* Delete Key handling */
                    int next3 = getchar();
                    if (next3 == 126 && pos < len) {
                        int bytes_to_del = 1;
                        while (pos + bytes_to_del < len && (buf[pos + bytes_to_del] & 0xC0) == 0x80) bytes_to_del++;
                        for (int i = pos; i < len - bytes_to_del; i++) buf[i] = buf[i + bytes_to_del];
                        len -= bytes_to_del; buf[len] = '\0';
                        refresh_line(prompt_str, buf, pos);
                    }
                }
                else if (next2 == 49) { /* Ctrl + Arrows */
                    int next3 = getchar(); int next4 = getchar(); int next5 = getchar(); 
                    if (next3 == 59 && next4 == 53) {
                        if (next5 == 67) { /* Ctrl + Right */
                            while (pos < len) {
                                int step = 1; while(pos + step < len && (buf[pos + step] & 0xC0) == 0x80) step++;
                                pos += step; if (buf[pos] == ' ') break;
                            }
                            refresh_line(prompt_str, buf, pos);
                        }
                        else if (next5 == 68) { /* Ctrl + Left */
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
            char dir_path[1024] = "."; char search_term[256] = "";
            char *last_slash = strrchr(full_word, '/');
            
            if (last_slash) {
                int dir_len = last_slash - full_word + 1;
                strncpy(dir_path, full_word, dir_len); dir_path[dir_len] = '\0';
                strcpy(search_term, last_slash + 1);
            } else {
                strcpy(search_term, full_word);
            }

            int search_len = strlen(search_term);
            DIR *d = opendir(dir_path); struct dirent *dir;
            char *matches[128]; int match_count = 0; int is_directory[128] = {0};

            if (d) {
                while ((dir = readdir(d)) != NULL) {
                    if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) continue;
                    if (search_len == 0 || strncmp(dir->d_name, search_term, search_len) == 0) {
                        matches[match_count] = strdup(dir->d_name);
                        is_directory[match_count] = (dir->d_type == DT_DIR);
                        match_count++; if (match_count >= 128) break;
                    }
                }
                closedir(d);

                if (match_count == 1) {
                    char *completion = matches[0] + search_len;
                    while (*completion) { buf[pos++] = *completion; len++; completion++; }
                    if (is_directory[0]) { buf[pos++] = '/'; len++; }
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
                        if (all_match) {
                            buf[pos++] = current_char; len++; l++;
                        } else break;
                    }
                    
                    if (l == 0) {
                        printf("\n");
                        for (int i = 0; i < match_count; i++) printf("%s%s  ", matches[i], is_directory[i] ? "/" : "");
                        printf("\n");
                    }
                    for(int i = 0; i < match_count; i++) free(matches[i]);
                    refresh_line(prompt_str, buf, pos);
                }
            }
        }
        else { /* Pure byte insertion mode */
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

int main(void) {
    char *line; char **args; int status = 1;
    signal(SIGINT, handle_sigint);
    
    load_config_file();
    load_default_aliases();

    while (status) {
        /* Fallback to non-interactive mode if input is piped (automated testing) */
        if (!isatty(STDIN_FILENO)) {
            char testing_buf[1024];
            if (fgets(testing_buf, sizeof(testing_buf), stdin) == NULL) {
                break; 
            }
            testing_buf[strcspn(testing_buf, "\n")] = '\0';
            if (strlen(testing_buf) == 0) continue;

            args = split_line(testing_buf);
            status = route_command(args);
            free(args);
        } 
        else {
            /* Standard interactive mode for user input */
            line = read_line_with_autocomplete();
            if (strlen(line) == 0) { free(line); continue; }

            add_to_history(line);
            args = split_line(line); 
            status = route_command(args);

            free(line); free(args);
        }
    }
    free_history();
    return EXIT_SUCCESS;
}