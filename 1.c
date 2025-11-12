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
void handle_killallterms() {
    printf("Terminating all f25shell...\n");
    pid_t pgid = getpgrp();
    // Terminate this process via SIGKILL to pgid
    int kill_status = kill(-pgid, SIGKILL);

    // Print based on kill return value
    if (kill_status == -1) {
        printf("Failed to terminate all f25shell instances\n");
    } else {
        printf("All f25shell instances killed\n");
    }
}

#define MAX_CHILDREN 256
pid_t children[MAX_CHILDREN];
int child_count = 0;

#define MAX_PROCESSES 1024
int process_count;
pid_t session_processes[MAX_PROCESSES];

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
void count_bg_processes() {
    collect_processes();
    process_count =  process_count - 2; // Exclude bash and current shell
    printf("Number of background processes in current session: %d\n", process_count);
}

// 4. Kill all processes other than current and bash
void kill_all_bg_processes() {
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

/* ======Main Function====== */
int main(int num_args, char *arguments[]) {

    // Argument validation (1 <= num_args <= 5)
    if (num_args != 1) {
        printf("No Arguments needed\n");
        return 1;
    }

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
                    if (num_args != 1) {
                        printf("Few/many arguments received\n");
                        return 1;
                    }
                    handle_killterm();
                    break;

                case 2: // 2. Kill all terminals - killallterms
                    if (num_args != 1) {
                        printf("Few/many arguments received\n");
                        return 1;
                    }
                    handle_killallterms();
                    break;

                case 3: // 3. Count bg processes - numbg
                    if (num_args != 4) {
                        printf("Few/many arguments received\n");
                        return 1;
                    }
                    count_bg_processes();
                    break;

                case 4: // 4. kill all process other than current and bash - killbp
                    if (num_args != 1) {
                        printf("Few/many arguments received\n");
                        return 1;
                    }
                    kill_all_bg_processes();
                    break;

                default:
                    printf("Invalid command\n");
                }
                break;
            }
        }

        if (!command_matched) {
            // Tokenize input
            int num_args = 0;
            char *token = strtok(input, " ");
            while (token != NULL && num_args < MAX_ARGS - 1) {
                tokens[num_args++] = token;
                token = strtok(NULL, " ");
            }

            /* execvp needs NULL termination */
            tokens[num_args] = NULL;
            
            if (num_args > 0 && strcmp(tokens[num_args - 1], "&") == 0) {
                tokens[num_args - 1] = NULL;  // Remove "&" from args
                
                int pid = fork();
                if (pid == 0) {
                    print_tokens(tokens, num_args);
                    execvp(tokens[0] & , tokens);

                    // if this line executes means execvp failed
                    printf("Exec failed for %s\n", tokens[0]);
                    exit(1);
                } else if (pid > 0) {
                    // wait for execvp to finish
                    wait(NULL);
                } else {
                printf("Fork failed\n");
                }
            }

            if (num_args == 0) continue; // empty input: prompt again

            int pid = fork();
            if (pid == 0) {
                print_tokens(tokens, num_args);
                execvp(tokens[0], tokens);

                // if this line executes means execvp failed
                printf("Exec failed for %s\n", tokens[0]);
                exit(1);
            } else if (pid > 0) {
                // wait for execvp to finish
                wait(NULL);
            } else {
            printf("Fork failed\n");
            }
        }
    }
    break;
}