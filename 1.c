#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

#define MAX_ARGS 64
#define MAX_INPUT 1024
#define MAX_COMMANDS 25

/* ======Functions====== */

// 1. Kill current terminal
void handle_killterm() {
    printf("Terminating current f25shell...\n");
    pid_t pid = getpid();
    // Terminate this process via SIGKILL to pid
    int kill_status = kill(pid, SIGKILL);

    // Print based on kill return value
    if (kill_status == -1) {
        printf("Failed to terminate f25shell\n");
    } else {
        printf("Current f25shell killed\n");
    }
}

// 2. Kill all terminals
int killed = 0;
void handle_killallterms() {

    // first get all processes
    collect_processes();
    pid_t shell_id = getpid();

    for (int i = 0; i < process_count; ++i) {
        pid_t pid = session_processes[i];

        // process list has ourselves also so skip it
        if (pid == shell_id)    continue;

        // read the executable name
        char path[64];
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);

        FILE *fp = fopen(path, "r");
        if (!fp) continue;

        char name[32] = {0};
        if (fgets(name, sizeof(name), fp)) {
            // strip trailing newline
            name[strcspn(name, "\n")] = '\0';

            //comparing the process name with f25shell
            if (strcmp(name, "f25shell") == 0) {
                if (kill(pid, SIGTERM) == 0) {
                    printf("Killed f25shell PID %d\n", pid);
                    killed = killed + 1;
                } else {
                    printf("Failed to kill f25shell PID %d\n", pid);
                }
            }
        }
        fclose(fp);
    } 
    if (killed == 0) {
        printf("No other f25shell instances found.\n");
    }
}

#define MAX_CHILDREN 256
pid_t children[MAX_CHILDREN];
int child_count = 0;

#define MAX_PROCESSES 1024
int process_count;
pid_t session_processes[MAX_PROCESSES];

#define MAX_BG_JOBS 256
pid_t bg_jobs[MAX_BG_JOBS];
int bg_job_count = 0;

// helper to add background job
static void add_background_job(pid_t pid) {
    if (bg_job_count < MAX_BG_JOBS) {
        bg_jobs[bg_job_count++] = pid;
    } else {
        printf("Too many background jobs\n");
    }
}

// helper to get all processes and its count
void collect_processes(void) {
    pid_t shell_id = getpid();
    pid_t my_pgid = getpgid(shell_id);
    pid_t bash_id = getppid();
    
    process_count = 0;  // Reset count
    
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        perror("opendir /proc");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL) {
        // Skip non-numeric entries because only PIDs are numeric
        if (!isdigit(entry->d_name[0])) continue;
        
        pid_t pid = atoi(entry->d_name);
              
        // Check if process exists and get its process group
        pid_t pgid = getpgid(pid);
        if (pgid == -1) continue;
        
        // Only collect processes in our process group
        if (pgid == my_pgid) {
            if (process_count < MAX_PROCESSES) {
                session_processes[process_count++] = pid;
            } else {
                printf("MAX_PROCESSES limit reached\n");
                break;
            }
        }
    } 
    closedir(proc_dir);
}
// 3. Count bg processes
void count_bg_processes(void) {
    int alive = 0;  
    // Iterate through stored background PIDs
    for (int i = 0; i < bg_job_count; i++) {
        // Check if process is still alive
        if (kill(bg_jobs[i], 0) == 0) {
            alive = alive + 1;
        }
    }
    printf("Number of background processes in current session: %d\n", alive);
}

// 4. Kill all processes other than current and bash
void kill_all_processes() {
    collect_processes();
    int processes_killed = 0;
    pid_t bash_id = getppid();
    pid_t current_shell_id = getpid();

    for(int i = 0; i < process_count; i++) {
        pid_t pid = session_processes[i];

        // Check pid of all processes gathered is not equal to bash id or current shell id
        if (pid != bash_id && pid != current_shell_id) {
            if (kill(pid, SIGKILL) == 0) {
                printf("Killed process %d\n", pid);
                processes_killed = processes_killed + 1;
            } else if (errno == ESRCH) {
                // Process already dead - not an error
                processes_killed++;  // Count as success
            } else {
                printf("Failed to kill process %d\n", pid);
            }
        }
    }
}

// 5. Print token for debugging only
void print_tokens(char *tokens[], int num_args) {
    for (int i = 0; i < num_args; i++) {
        printf("Token %d: %s\n", i, tokens[i]);
    }
}

/* helpers */
void shell_err(const char *fmt, ...);
int redirect_input(const char *file);
int redirect_output(const char *file, int append);
void add_bg(pid_t pid);
void count_bg(void);
void kill_bg(void);

/* file ops */
int  count_words(const char *file);
int  append_files(const char *f1, const char *f2);
int  concat_files(int n, const char **files, const char *out);

/* chaining & piping */
void exec_chain(const char *input);
void exec_pipe(char **cmds, int n, int reverse);

/* ======Main Function====== */
int main(int num_args, char *arguments[]) {

    // Argument validation
    if (num_args != 1) {
        printf("No Arguments needed\n");
        return 1;
    }

    // token size and array for tokens
    char input[MAX_INPUT];
    char *tokens[MAX_ARGS];

    while (1) {
        printf("f25shell$: ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            // EOF or read error
            printf("\n");
            break;
        }

        // Remove trailing newline
        int length = strlen(input);
        if (length > 0 && input[length - 1] == '\n') {
            input[length - 1] = '\0';
        }

        // Tokenize input first
        int num_tokens = 0;
        char *token = strtok(input, " ");
        while (token != NULL && num_tokens < MAX_ARGS - 1) {
            tokens[num_tokens++] = token;
            token = strtok(NULL, " ");
        }

        /* execvp needs NULL termination */
        tokens[num_tokens] = NULL;

        if (num_tokens == 0) continue; // empty input: prompt again checking again

        int command_matched = 0;
        // Array of valid commands
        const char *commands[] = {
            "killterm", "killallterms", "numbg", "killbp"
        };

        for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            if (strcmp(tokens[0], commands[i]) == 0) {
                command_matched = 1;
                switch (i + 1) {
                case 1: // 1. Kill current terminal - killterm
                    if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                    }
                    handle_killterm();
                    break;

                case 2: // 2. Kill all terminals - killallterms
                    if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                    }
                    handle_killallterms();
                    break;

                case 3: // 3. Count bg processes - numbg
                    if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                    }
                    count_bg_processes();
                    break;

                case 4: // 4. kill all process other than current and bash - killbp
                    if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                    }
                    kill_all_processes();
                    break;

                }
                break;
            }
        }

        if (!command_matched) {
            // Check if it's a background process (ends with "&")
            int is_background = 0;
            if (num_tokens > 0 && strcmp(tokens[num_tokens - 1], "&") == 0) {
                tokens[num_tokens - 1] = NULL;  // Remove "&" from args
                num_tokens--;  // Adjust count
                is_background = 1;
            }
            
            int pid = fork();
            if (pid == 0) {
                print_tokens(tokens, num_tokens);
                execvp(tokens[0], tokens);

                // if this line executes means execvp failed
                printf("Exec failed for %s\n", tokens[0]);
                exit(1);
            } else if (pid > 0) {
                if (is_background) {
                    // Don't wait for background processes
                    printf("Background process started with PID: %d\n", pid);
                    add_background_job(pid);
                } else {
                    // wait for foreground process to finish
                    wait(pid, NULL, 0);
                }
            } else {
                printf("Fork failed\n");
            }
        }
    }
}