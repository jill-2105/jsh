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
#include <fcntl.h>


#define MAX_ARGS 64
#define MAX_INPUT_SIZE 1024
#define MAX_COMMANDS 25

#define MAX_PROCESSES 1024
int process_count;
pid_t session_processes[MAX_PROCESSES];

/* ======Functions====== */

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

// 6. Word count for text file
void file_wordcount(char *tokens[], int num_tokens) {
    // Validate arguments
    if(num_tokens != 2) {
        printf("Few/many arguments received\n");
        return;
    }

    // Validate argc rule
    if(num_tokens < 1 || num_tokens > 5) {
        printf("Command argc must be between 1 and 5\n");
        return;
    }

    char *filename = tokens[1];
    
    // Open file with low-level call
    int fd = open(filename, O_RDONLY);
    if(fd < 0) {
        printf("Failed to open file %s\n", filename);
        return;
    }

    // Read file content into buffer
    char buffer[4096];
    int bytes_read;
    int word_count = 0;
    int in_word = 0;

    while((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        // Manual word counting
        int i;
        for(i=0; i<bytes_read; i++) {
            char ch = buffer[i];
            
            // Check if character is whitespace
            if(ch==' ' || ch=='\n' || ch=='\t' || ch=='\r') {
                if(in_word) {
                    word_count = word_count + 1;
                    in_word = 0;
                }
            } else {
                in_word = 1;
            }
        }
    }

    // Count last word if file doesn't end with whitespace
    if(in_word) {
        word_count = word_count + 1;
    }

    close(fd);
    printf("%d\n", word_count);
}


// 7. File concatenation
void file_concat(char *tokens[], int num_tokens) {
    // Count number of files (skip + operators)
    int file_count = 0;
    char *files[MAX_COMMANDS];
    
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "+") != 0) {
            files[file_count] = tokens[i];
            file_count = file_count + 1;
        }
    }

    // Validate file count
    if(file_count < 2) {
        printf("Need at least 2 files for concatenation\n");
        return;
    }

    // Check max concatenation operations
    int plus_count = num_tokens - file_count;
    if(plus_count > 4) {
        printf("Maximum 4 concatenation operations allowed\n");
        return;
    }

    // Read and print each file
    for(i=0; i<file_count; i++) {
        int fd = open(files[i], O_RDONLY);
        if(fd < 0) {
            printf("Failed to open file %s\n", files[i]);
            continue;
        }

        // Read and write to stdout
        char buffer[4096];
        int bytes_read;
        while((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            // Write to stdout using file descriptor 1
            write(1, buffer, bytes_read);
        }

        close(fd);
    }
}


// 8. File append operation
void file_append(char *tokens[], int num_tokens) {
    // Validate format: file1.txt ++ file2.txt
    if(num_tokens != 3) {
        printf("Few/many arguments received\n");
        return;
    }

    // Check if middle token is ++
    if(strcmp(tokens[1], "++") != 0) {
        printf("Invalid format for file append\n");
        return;
    }

    char *file1 = tokens[0];
    char *file2 = tokens[2];

    // Read file1 content
    int fd1 = open(file1, O_RDONLY);
    if(fd1 < 0) {
        printf("Failed to open file %s\n", file1);
        return;
    }

    char buffer1[16384];
    int bytes1 = read(fd1, buffer1, sizeof(buffer1));
    if(bytes1 < 0) {
        printf("Failed to read file %s\n", file1);
        close(fd1);
        return;
    }
    close(fd1);

    // Read file2 content
    int fd2 = open(file2, O_RDONLY);
    if(fd2 < 0) {
        printf("Failed to open file %s\n", file2);
        return;
    }

    char buffer2[16384];
    int bytes2 = read(fd2, buffer2, sizeof(buffer2));
    if(bytes2 < 0) {
        printf("Failed to read file %s\n", file2);
        close(fd2);
        return;
    }
    close(fd2);

    // Append file2 content to file1
    int fd1_write = open(file1, O_WRONLY | O_APPEND);
    if(fd1_write < 0) {
        printf("Failed to open file %s for writing\n", file1);
        return;
    }
    write(fd1_write, buffer2, bytes2);
    close(fd1_write);

    // Append file1 original content to file2
    int fd2_write = open(file2, O_WRONLY | O_APPEND);
    if(fd2_write < 0) {
        printf("Failed to open file %s for writing\n", file2);
        return;
    }
    write(fd2_write, buffer1, bytes1);
    close(fd2_write);

    printf("Files appended successfully\n");
}


// Helper function to check for file operations
void check_file_ops(char *tokens[], int num_tokens) {
    // Check for word count operator
    if(strcmp(tokens[0], "#") == 0) {
        file_wordcount(tokens, num_tokens);
        return;
    }

    // Check for file append
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "++") == 0) {
            file_append(tokens, num_tokens);
            return;
        }
    }

    // Check for file concatenation
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "+") == 0) {
            file_concat(tokens, num_tokens);
            return;
        }
    }
}


// 9. Input redirection
void redir_in(char *tokens[], int num_tokens) {
    int redir_pos = -1;
    
    // Find position of < operator
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "<") == 0) {
            redir_pos = i;
            break;
        }
    }
    
    // Validate redirection syntax
    if(redir_pos == -1 || redir_pos + 1 >= num_tokens) {
        printf("Invalid input redirection syntax\n");
        return;
    }
    
    // Validate argc for command part
    if(redir_pos < 1 || redir_pos > 5) {
        printf("Command argc must be between 1 and 5\n");
        return;
    }
    
    // Build command array
    char *cmd[MAX_ARGS];
    for(i=0; i<redir_pos; i++) {
        cmd[i] = tokens[i];
    }
    cmd[redir_pos] = NULL;
    
    char *filename = tokens[redir_pos + 1];
    
    int fork_res = fork();
    if(fork_res == 0) {
        // Child process - setup input redirection
        int fd = open(filename, O_RDONLY);
        if(fd < 0) {
            printf("Failed to open file %s\n", filename);
            exit(1);
        }
        
        // Redirect stdin to file
        dup2(fd, STDIN_FILENO);
        close(fd);
        
        execvp(cmd[0], cmd);
        printf("Exec failed for %s\n", cmd[0]);
        exit(1);
    } else if(fork_res > 0) {
        wait(NULL);
    } else {
        printf("Fork failed\n");
    }
}


// 10. Output redirection
void redir_out(char *tokens[], int num_tokens) {
    int redir_pos = -1;
    
    // Find position of > operator
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], ">") == 0) {
            redir_pos = i;
            break;
        }
    }
    
    // Validate redirection syntax
    if(redir_pos == -1 || redir_pos + 1 >= num_tokens) {
        printf("Invalid output redirection syntax\n");
        return;
    }
    
    // Validate argc for command part
    if(redir_pos < 1 || redir_pos > 5) {
        printf("Command argc must be between 1 and 5\n");
        return;
    }
    
    // Build command array
    char *cmd[MAX_ARGS];
    for(i=0; i<redir_pos; i++) {
        cmd[i] = tokens[i];
    }
    cmd[redir_pos] = NULL;
    
    char *filename = tokens[redir_pos + 1];
    
    int fork_res = fork();
    if(fork_res == 0) {
        // Child process - setup output redirection
        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd < 0) {
            printf("Failed to open file %s\n", filename);
            exit(1);
        }
        
        // Redirect stdout to file
        dup2(fd, STDOUT_FILENO);
        close(fd);
        
        execvp(cmd[0], cmd);
        printf("Exec failed for %s\n", cmd[0]);
        exit(1);
    } else if(fork_res > 0) {
        wait(NULL);
    } else {
        printf("Fork failed\n");
    }
}


// 11. Append output redirection
void redir_append(char *tokens[], int num_tokens) {
    int redir_pos = -1;
    
    // Find position of >> operator
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], ">>") == 0) {
            redir_pos = i;
            break;
        }
    }
    
    // Validate redirection syntax
    if(redir_pos == -1 || redir_pos + 1 >= num_tokens) {
        printf("Invalid append redirection syntax\n");
        return;
    }
    
    // Validate argc for command part
    if(redir_pos < 1 || redir_pos > 5) {
        printf("Command argc must be between 1 and 5\n");
        return;
    }
    
    // Build command array
    char *cmd[MAX_ARGS];
    for(i=0; i<redir_pos; i++) {
        cmd[i] = tokens[i];
    }
    cmd[redir_pos] = NULL;
    
    char *filename = tokens[redir_pos + 1];
    
    int fork_res = fork();
    if(fork_res == 0) {
        // Child process - setup append redirection
        int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if(fd < 0) {
            printf("Failed to open file %s\n", filename);
            exit(1);
        }
        
        // Redirect stdout to file
        dup2(fd, STDOUT_FILENO);
        close(fd);
        
        execvp(cmd[0], cmd);
        printf("Exec failed for %s\n", cmd[0]);
        exit(1);
    } else if(fork_res > 0) {
        wait(NULL);
    } else {
        printf("Fork failed\n");
    }
}


// 12. Sequential execution
void seqexec(char *tokens[], int num_tokens) {
    // Count number of semicolons
    int semi_count = 0;
    int i;
    for(i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], ";") == 0) {
            semi_count = semi_count + 1;
        }
    }
    
    // Validate max commands
    if(semi_count > 3) {
        printf("Maximum 4 sequential commands allowed\n");
        return;
    }
    
    // Execute commands sequentially
    int start = 0;
    for(i=0; i<=num_tokens; i++) {
        if(i == num_tokens || strcmp(tokens[i], ";") == 0) {
            int cmd_len = i - start;
            
            // Check if command segment is empty
            if(cmd_len == 0) {
                printf("Empty command in sequential execution\n");
                return;
            }
            
            // Validate argc for this segment
            if(cmd_len < 1 || cmd_len > 5) {
                printf("Each command argc must be between 1 and 5\n");
                return;
            }
            
            // Build command array for this segment
            char *cmd[MAX_ARGS];
            int j;
            for(j=0; j<cmd_len; j++) {
                cmd[j] = tokens[start + j];
            }
            cmd[cmd_len] = NULL;
            
            // Execute this command
            int fork_res = fork();
            if(fork_res == 0) {
                execvp(cmd[0], cmd);
                // If exec fails, child exits with error
                exit(1);
            } else if(fork_res > 0) {
                int status;
                waitpid(fork_res, &status, 0);
                
                // Check if child process failed to exec
                if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    // Command not found or exec failed - stop execution
                    printf("Command execution failed, stopping sequential execution\n");
                    return;
                }
            } else {
                printf("Fork failed\n");
                return;
            }
            
            start = i + 1;
        }
    }
}


/* ======Main Function====== */
int main(int num_args, char *arguments[]) {

    // Argument validation
    if (num_args != 1) {
        printf("No Arguments needed\n");
        return 1;
    }

    // token size and array for tokens
    char input[MAX_INPUT_SIZE];
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
            "killterm", "killallterms", "numbg", "killbp", "exit"
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

                case 5: // 5. Exit
                    if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                    }
                    return 0; 

                }
                break;
            }
        }

        if (!command_matched) {
            // Check for file operations first
            int has_file_op = 0;
            
            // Check for # operator
            if(strcmp(tokens[0], "#") == 0) {
                has_file_op = 1;
                check_file_ops(tokens, num_tokens);
                continue;
            }
            
            // Check for ++ or +
            int i;
            for(i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "++") == 0 || strcmp(tokens[i], "+") == 0) {
                    has_file_op = 1;
                    check_file_ops(tokens, num_tokens);
                    break;
                }
            }
            
            if(has_file_op) continue;
            
            // Check for redirection operators
            int has_redir = 0;
            
            // Check for >> first (before >)
            for(i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], ">>") == 0) {
                    has_redir = 1;
                    redir_append(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for > operator
            for(i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], ">") == 0) {
                    has_redir = 1;
                    redir_out(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for < operator
            for(i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "<") == 0) {
                    has_redir = 1;
                    redir_in(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for sequential execution
            int has_semi = 0;
            for(i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], ";") == 0) {
                    has_semi = 1;
                    seqexec(tokens, num_tokens);
                    break;
                }
            }
            if(has_semi) continue;
            
            // Check if it's a background process (ends with "&")
            int is_background = 0;
            if (num_tokens > 0 && strcmp(tokens[num_tokens - 1], "&") == 0) {
                tokens[num_tokens - 1] = NULL;  // Remove "&" from args
                num_tokens--;  // Adjust count
                is_background = 1;
            }
            
            int pid = fork();
            if (pid == 0) {
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
                    wait(NULL);
                }
            } else {
                printf("Fork failed\n");
            }
        }
    }
}
