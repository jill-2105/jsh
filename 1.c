#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 

#define MAX_ARGS 64
#define MAX_INPUT 1024        

int main() {

    char input[MAX_INPUT];
    char *args[MAX_ARGS];        
    
    while (1) {
        printf("f25shell$: ");
        // Read input from keyboard
        fgets(input, sizeof(input), stdin);

        // Remove the Enter key (\n) from the end
        int length = strlen(input);
        if (length > 0 && input[length - 1] == '\n') {
            input[length - 1] = '\0';
        }

        int argc = 0;
        char *token = strtok(input, " ");
        while (token != NULL && argc < MAX_ARGS - 1) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        args[argc] = NULL; // execvp needs this

        if (argc == 0) continue; // empty input

        if (strcmp(args[0], "exit") == 0) break;

        pid_t pid = fork();

        if (pid == 0) {
            execvp(args[0], args);
            perror("exec failed");
            exit(1);
        } else if (pid > 0) {
            wait(NULL);
        } else {
            perror("fork failed");
        }
    }
}