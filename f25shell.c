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
#define MAX_BG_JOBS 256

pid_t session_processes[MAX_PROCESSES];
int process_count;

pid_t bg_jobs[MAX_BG_JOBS];
int bg_job_count = 0;

/* ======Functions====== */

// helper to get all processes and its count
void collect_processes(void) {
    pid_t shell_id = getpid();
    pid_t my_pgid = getpgid(shell_id);
    pid_t bash_id = getppid();
    
    process_count = 0;
    
    // Reading /proc to get the process list
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        printf("Error opening /proc");
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

    for (int i = 0; i < process_count; i++) {
        pid_t pid = session_processes[i];

        // process list has ourselves also so skip it
        if (pid == shell_id)    continue;

        // read the executable name skip if not f25shell
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
        } fclose(fp);
    } 
    if (killed == 0)
        printf("No other f25shell instances found.\n");
}

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
                processes_killed = processes_killed + 1;  // Count as success
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
        printf("Command argc must be 2\n");
        return;
    }

    // opening the file - argc 1
    char *filename = tokens[1]; 
    int fd = open(filename, O_RDONLY);
    if(fd < 0) {
        printf("Failed to open file %s\n", filename);
        return;
    }

    // Reading the file content
    char buffer[4096];
    int bytes_read;
    int word_count = 0;
    int in_word = 0;

    while((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        for(int i=0; i<bytes_read; i++) {
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
    printf("Total word count is: %d\n", word_count);
}

// 7. File concatenation
void file_concat(char *tokens[], int num_tokens) {
    // Count number of files
    int file_count = 0;
    char *files[MAX_COMMANDS];
    
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "+") != 0) {
            files[file_count] = tokens[i];
            // files will be 1 more than the + operators
            file_count = file_count + 1;
        }
    }

    int plus_count = num_tokens - file_count;
    
    // Validate file count
    if(file_count < 2) {
        printf("Need at least 2 files\n");
        return;
    }

    // Check max concatenation operations
    if(plus_count > 4) {
        printf("Maximum 4 concatenation only\n");
        return;
    }

    // Read and print each file
    for(int i=0; i<file_count; i++) {
        int fd = open(files[i], O_RDONLY);
        if(fd < 0) {
            printf("Failed to open file %s\n", files[i]);
            continue;
        }

        // Read and write to stdout
        char buffer[4096];
        int bytes_read;
        while((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            write(1, buffer, bytes_read);
        }
        close(fd);
    }
}

// 8. File append operation
void file_append(char *tokens[], int num_tokens) {
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

    char buffer1[4096];
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

    char buffer2[4096];
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

// helper function to check for file operations
void check_file_ops(char *tokens[], int num_tokens) {
    // Check for word count operator
    if(strcmp(tokens[0], "#") == 0) {
        file_wordcount(tokens, num_tokens);
        return;
    }

    // Check for file append
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "++") == 0) {
            file_append(tokens, num_tokens);
            return;
        }
    }

    // Check for file concatenation
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "+") == 0) {
            file_concat(tokens, num_tokens);
            return;
        }
    }
}

// helper function for all redirection operations
void handle_redirection(char *tokens[], int num_tokens, const char *operator, int flags, const char *error_msg) {
    int redir_pos = -1;
    
    // Find position of operator
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], operator) == 0) {
            redir_pos = i;
            break;
        }
    }
    
    // Validate redirection syntax
    if(redir_pos == -1 || redir_pos + 1 >= num_tokens) {
        printf("%s\n", error_msg);
        return;
    }
    
    // Validate argc for command part
    if(redir_pos < 1 || redir_pos > 5) {
        printf("Command argc must be between 1 and 5\n");
        return;
    }
    
    // Build command array
    char *cmd[MAX_ARGS];
    for(int i=0; i<redir_pos; i++) {
        cmd[i] = tokens[i];
    }
    cmd[redir_pos] = NULL;
    
    char *filename = tokens[redir_pos + 1];
    
    int fork_res = fork();
    if(fork_res == 0) {
        // Child process - setup redirection
        int fd = open(filename, flags, 0644);
        if(fd < 0) {
            printf("Failed to open file %s\n", filename);
            exit(1);
        }
        
        // Redirect based on flags (input vs output)
        if(flags == O_RDONLY) {
            dup2(fd, STDIN_FILENO);
        } else {
            dup2(fd, STDOUT_FILENO);
        }
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

// 9. Input redirection
void redir_in(char *tokens[], int num_tokens) {
    handle_redirection(tokens, num_tokens, "<", O_RDONLY, "Invalid input redirection syntax");
}

// 10. Output redirection
void redir_out(char *tokens[], int num_tokens) {
    handle_redirection(tokens, num_tokens, ">", O_WRONLY | O_CREAT | O_TRUNC, "Invalid output redirection syntax");
}

// 11. Append output redirection
void redir_append(char *tokens[], int num_tokens) {
    handle_redirection(tokens, num_tokens, ">>", O_WRONLY | O_CREAT | O_APPEND, "Invalid append redirection syntax");
}

// 12. Sequential execution
void seqexec(char *tokens[], int num_tokens) {
    // Count number of semicolons
    int semi_count = 0;
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], ";") == 0) {
            semi_count = semi_count + 1;
        }
    }
    
    // Validate max commands
    if(semi_count > 4) {
        printf("Maximum 4 sequential commands allowed\n");
        return;
    }
    
    // Execute commands sequentially
    int start = 0;
    for(int i=0; i<=num_tokens; i++) {
        if(i == num_tokens || strcmp(tokens[i], ";") == 0) {
            int cmd_len = i - start;
                       
            // Validate argc for this segment
            if(cmd_len < 1 || cmd_len > 5 || cmd_len == 0) {
                printf("Each command argc must be between 1 and 5\n");
                return;
            }
            
            // Build command array for this segment
            char *cmd[MAX_ARGS];
            for(int j=0; j<cmd_len; j++) {
                cmd[j] = tokens[start + j];
            }
            cmd[cmd_len] = NULL;
            
            // Execute this command
            int fork_res = fork();
            if(fork_res == 0) {
                execvp(cmd[0], cmd);
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

// 13. Pipe execution
void pipeexec(char *tokens[], int num_tokens) {
    // Phase 1: Parse and store all commands
    char *cmds[MAX_COMMANDS][MAX_ARGS];
    int cmd_lens[MAX_COMMANDS];
    int cmd_count = 0;
    
    // Count pipe operators
    int pipe_count = 0;
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "|") == 0) {
            pipe_count = pipe_count + 1;
        }
    }

    if(pipe_count > 4) {
        printf("Maximum 4 piping operations allowed\n");
        return;
    }
    
    // Split tokens into command segments
    int start = 0;
    for(int i=0; i<=num_tokens; i++) {
        if(i == num_tokens || strcmp(tokens[i], "|") == 0) {
            int cmd_len = i - start;
            
            if(cmd_len < 1 || cmd_len > 5 || cmd_len == 0) {
                printf("Each piped command argc must be between 1 and 5\n");
                return;
            }
            
            // Store command
            for(int j=0; j<cmd_len; j++) {
                cmds[cmd_count][j] = tokens[start + j];
            }
            cmds[cmd_count][cmd_len] = NULL;
            cmd_lens[cmd_count] = cmd_len;
            cmd_count = cmd_count + 1;   
            start = i + 1;
        }
    }

    // creating all pipes    
    int pipes[cmd_count-1][2];
    for(int p=0; p<cmd_count-1; p++) {
        if(pipe(pipes[p]) < 0) {
            printf("Pipe creation failed\n");
            return;
        }
    }
    
    // Fork all children
    for(int c=0; c<cmd_count; c++) {
        int fork_res = fork();
        
        if(fork_res == 0) {
            // Connect input pipe if not first command
            if(c > 0) {
                dup2(pipes[c-1][0], STDIN_FILENO);
            }
            
            // Connect output pipe if not last command
            if(c < cmd_count-1) {
                dup2(pipes[c][1], STDOUT_FILENO);
            }
            
            // Close all pipe file descriptors
            int x;
            for(x=0; x<cmd_count-1; x++) {
                close(pipes[x][0]);
                close(pipes[x][1]);
            }
            
            execvp(cmds[c][0], cmds[c]);
            exit(1);
        } else if(fork_res < 0) {
            printf("Fork failed\n");
            return;
        }
    }
    
    // Parent process - close all pipes
    for(int p=0; p<cmd_count-1; p++) {
        close(pipes[p][0]);
        close(pipes[p][1]);
    }
    
    // Wait for all children
    for(int c=0; c<cmd_count; c++) {
        wait(NULL);
    }
}

// 14. Reverse pipe execution
void revpipe(char *tokens[], int num_tokens) {
    // Phase 1: Parse commands like normal pipe
    char *cmds[MAX_COMMANDS][MAX_ARGS];
    int cmd_lens[MAX_COMMANDS];
    int cmd_count = 0;
    
    // Count reverse pipe operators
    int rpipe_count = 0;
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "~") == 0) {
            rpipe_count = rpipe_count + 1;
        }
    }
    
    // Validate max reverse pipes
    if(rpipe_count > 5) {
        printf("Maximum 5 reverse piping operations allowed\n");
        return;
    }
    
    // Split tokens into command segments
    int start = 0;
    for(int i=0; i<=num_tokens; i++) {
        if(i == num_tokens || strcmp(tokens[i], "~") == 0) {
            int cmd_len = i - start;
            
            if(cmd_len < 1 || cmd_len > 5 || cmd_len == 0) {
                printf("Each reverse piped command argc must be between 1 and 5\n");
                return;
            }
            
            // Store command
            int j;
            for(j=0; j<cmd_len; j++) {
                cmds[cmd_count][j] = tokens[start + j];
            }
            cmds[cmd_count][cmd_len] = NULL;
            cmd_lens[cmd_count] = cmd_len;
            cmd_count = cmd_count + 1; 
            start = i + 1;
        }
    }
    
    // reversing the commands order for reversing pipe logic
    int left = 0;
    int right = cmd_count - 1;
    while(left < right) {
        // Swap commands at left and right positions
        char *temp[MAX_ARGS];
        int j;
        
        // Copy left to temp
        for(j=0; j<=cmd_lens[left]; j++) {
            temp[j] = cmds[left][j];
        }
        
        // Copy right to left
        for(j=0; j<=cmd_lens[right]; j++) {
            cmds[left][j] = cmds[right][j];
        }
        
        // Copy temp to right
        for(j=0; j<=cmd_lens[left]; j++) {
            cmds[right][j] = temp[j];
        }
        
        // Swap lengths
        int temp_len = cmd_lens[left];
        cmd_lens[left] = cmd_lens[right];
        cmd_lens[right] = temp_len;
        
        left = left + 1;
        right = right - 1;
    }
    
    // executing the normal logic of pipe
    int pipes[cmd_count-1][2];
    
    // Create all pipes
    for(int p=0; p<cmd_count-1; p++) {
        if(pipe(pipes[p]) < 0) {
            printf("Pipe creation failed\n");
            return;
        }
    }
    
    // Fork all children
    for(int c=0; c<cmd_count; c++) {
        int fork_res = fork();
        
        if(fork_res == 0) {
            // Connect input pipe if not first command
            if(c > 0) {
                dup2(pipes[c-1][0], STDIN_FILENO);
            }
            
            // Connect output pipe if not last command
            if(c < cmd_count-1) {
                dup2(pipes[c][1], STDOUT_FILENO);
            }
            
            // Close all pipe file descriptors
            for(int x=0; x<cmd_count-1; x++) {
                close(pipes[x][0]);
                close(pipes[x][1]);
            }
            
            execvp(cmds[c][0], cmds[c]);
            exit(1);
        } else if(fork_res < 0) {
            printf("Fork failed\n");
            return;
        }
    }
    
    // Parent process - close all pipes
    for(int p=0; p<cmd_count-1; p++) {
        close(pipes[p][0]);
        close(pipes[p][1]);
    }
    
    // Wait for all children
    for(int c=0; c<cmd_count; c++) {
        wait(NULL);
    }
}

// 15. Conditional execution
void condexec(char *tokens[], int num_tokens) {
    // Phase 1: Parse into separate arrays for commands and operators
    char *cmds[MAX_COMMANDS][MAX_ARGS];
    char *ops[MAX_COMMANDS];
    int cmd_lens[MAX_COMMANDS];
    int cmd_count = 0;
    int op_count = 0;
    
    // Count conditional operators
    int cond_count = 0;
    for(int i=0; i<num_tokens; i++) {
        if(strcmp(tokens[i], "&&") == 0 || strcmp(tokens[i], "||") == 0) {
            cond_count = cond_count + 1;
        }
    }
    
    // Validate max operators
    if(cond_count > 5) {
        printf("Maximum 5 conditional operators allowed\n");
        return;
    }
    
    // Parse commands and operators
    int start = 0;
    for(int i=0; i<=num_tokens; i++) {
        if(i == num_tokens || strcmp(tokens[i], "&&") == 0 || strcmp(tokens[i], "||") == 0) {
            int cmd_len = i - start;

            if(cmd_len < 1 || cmd_len > 5 || cmd_len == 0) {
                printf("Each conditional command argc must be between 1 and 5\n");
                return;
            }
            
            // Store command
            int j;
            for(j=0; j<cmd_len; j++) {
                cmds[cmd_count][j] = tokens[start + j];
            }
            cmds[cmd_count][cmd_len] = NULL;
            cmd_lens[cmd_count] = cmd_len;
            cmd_count = cmd_count + 1;
            
            // Store operator if exists
            if(i < num_tokens) {
                ops[op_count] = tokens[i];
                op_count = op_count + 1;
            }   
            start = i + 1;
        }
    }
    
    // execute after checking conditions
    for(int c=0; c<cmd_count; c++) {
        int fork_res = fork();
        
        if(fork_res == 0) {
            execvp(cmds[c][0], cmds[c]);
            exit(1);
        } else if(fork_res > 0) {
            int status;
            waitpid(fork_res, &status, 0);
            
            int exec_failed = 0;
            if(WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                exec_failed = 1;
            }
            
            // Check if we should continue to next command
            if(c < cmd_count - 1) {
                char *op = ops[c];
                
                if(strcmp(op, "&&") == 0) {
                    if(exec_failed) {
                        return;
                    }
                } else if(strcmp(op, "||") == 0) {
                    if(!exec_failed) {
                        return;
                    }
                }
            }
        } else {
            printf("Fork failed\n");
            return;
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

        // execvp needs NULL termination
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
                if (num_tokens != 1) {
                        printf("Few/many arguments received\n");
                        break;
                }
                switch (i + 1) {
                case 1: // 1. Kill current terminal - killterm
                    handle_killterm(); break;

                case 2: // 2. Kill all terminals - killallterms
                    handle_killallterms(); break;

                case 3: // 3. Count bg processes - numbg
                    count_bg_processes(); break;

                case 4: // 4. kill all process other than current and bash - killbp
                    kill_all_processes(); break;

                case 5: // 5. Exit
                    return 0; 

                }
                break;
            }
        }

        if (!command_matched) {
            int has_file_op = 0;
            
            // Check for # operator
            if(strcmp(tokens[0], "#") == 0) {
                has_file_op = 1;
                check_file_ops(tokens, num_tokens);
                continue;
            }
            
            // Check for ++ or +
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "++") == 0 || strcmp(tokens[i], "+") == 0) {
                    has_file_op = 1;
                    check_file_ops(tokens, num_tokens);
                    break;
                }
            }
            
            if(has_file_op) continue;
            
            int has_redir = 0;
            
            // Check for >> before >
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], ">>") == 0) {
                    has_redir = 1;
                    redir_append(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for > operator
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], ">") == 0) {
                    has_redir = 1;
                    redir_out(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for < operator
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "<") == 0) {
                    has_redir = 1;
                    redir_in(tokens, num_tokens);
                    break;
                }
            }
            if(has_redir) continue;
            
            // Check for conditional execution
            int has_cond = 0;
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "&&") == 0 || strcmp(tokens[i], "||") == 0) {
                    has_cond = 1;
                    condexec(tokens, num_tokens);
                    break;
                }
            }
            if(has_cond) continue;
            
            // Check for reverse pipe
            int has_revpipe = 0;
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "~") == 0) {
                    has_revpipe = 1;
                    revpipe(tokens, num_tokens);
                    break;
                }
            }
            if(has_revpipe) continue;
            
            // Check for normal pipe
            int has_pipe = 0;
            for(int i=0; i<num_tokens; i++) {
                if(strcmp(tokens[i], "|") == 0) {
                    has_pipe = 1;
                    pipeexec(tokens, num_tokens);
                    break;
                }
            }
            if(has_pipe) continue;
            
            // Check for sequential execution
            int has_semi = 0;
            for(int i=0; i<num_tokens; i++) {
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
                tokens[num_tokens - 1] = NULL;
                num_tokens = num_tokens - 1;
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
