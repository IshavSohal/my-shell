#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

// Array of char pointers. A string is a char pointer, so this is an array of strings
const char *SUPPORTED_COMMANDS[] = {"ls", "cat", "cd", "echo", "pwd", "grep"};
const int NUM_SUPPORTED = sizeof(SUPPORTED_COMMANDS) / sizeof(char *);
const int MAX_COMMAND = 50; // Max number of tokens for a single command
const char *COMMAND_DELIMITER = " ";
const char *PIPE_DELIMITER = "|";


// Used to run an individual command. This is executed by a child process, so no need to fork in here
void run_command(char* command_str){ 
    // Parse user input, determine which valid command was executed, if any
    char *token_ptr;
    char *curr_token = strtok_r(command_str, COMMAND_DELIMITER, &token_ptr);

    if (curr_token != NULL){
        // First token must be the name of a supported command. Otherwise we cannot process user input
        bool is_supported = false;
        for (int i = 0; i < NUM_SUPPORTED; i++){            
            if (strcmp(SUPPORTED_COMMANDS[i], curr_token) == 0){
                is_supported = true;
                break;
            }
        }
        
        // Avoid invalid commands
        if (!is_supported){
            fprintf(stderr, "Not a supported command\n");
            return;
        }        
    }

    char *command[MAX_COMMAND+1];
    command[0] = curr_token;
    int currSize = 1;

    
    while (curr_token != NULL && currSize < MAX_COMMAND){
        curr_token = strtok_r(NULL, COMMAND_DELIMITER, &token_ptr);
        command[currSize] = curr_token;
        currSize += 1;
    }
    command[currSize] = NULL;

    // Since `cd` is meant to alter the state of the shell, it cannot be deferred to a
    // child process. The current working directory should be changed to the path provided 
    // by the user
    if (strcmp(command[0], "cd") == 0){
        if (command[2] != NULL){
            fprintf(stderr, "Too many or too few arguments for cd command\n");
            return;
        } 
        printf("running cd\n");
        printf("%s\n", command[1]);
        chdir(command[1]); // first argument should be the path
        return;
    }

    // The child process calls exec to run the command
    execvp(command[0], command);
}

// Prompt user to enter a command
void run() {
    char x[50];
    printf("mysh> ");
    fgets(x, sizeof(x), stdin);
    x[strcspn(x, "\n")] = '\0';

    // If there are pipes in the command, we must split on then, and run each command seperately
    char *token_ptr;
    char *individual_command = strtok_r(x, PIPE_DELIMITER, &token_ptr);

    // For now assume there are only two commands
    if (individual_command == NULL){
        return;
    }

    char *command1 = individual_command;
    char *command2 = strtok_r(NULL, PIPE_DELIMITER, &token_ptr);
    printf("%s\n", command1);
    //printf("%s\n", command2);


    // If there is only one command, just fork one time and run the command
    if (command2 == NULL){
        pid_t pid = fork();

        if (pid == -1){
            return;
        }

        // Child process
        else if (pid == 0){
            run_command(command1);
        }

        // Parent process. Wait for child process to complete execution
        else{
            int status;
            pid_t res = waitpid(pid, &status, 0);

            if (res == -1){
                perror("Child process 1 failed");
            }
        }

        return;
    }


    int pipefd[2];
    int pipe_res = pipe(pipefd);

    if (pipe_res == -1){
        return;
    }

    pid_t pid = fork();

    if (pid == -1){
        return;
    }

    // First child process. Set stdout to write end of pipe. Close the read end
    else if (pid == 0){
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        run_command(command1);
        close(pipefd[1]);
    }

    // Parent process. Wait for first child process to complete execution. Close write end of pipe. Then execute second command in new child process
    // Since stderr for the child process still points to the terminal, error handling is automatically handled. 
    else {
        close(pipefd[1]);
        int status;
        pid_t res = waitpid(pid, &status, 0);    
    
        if (res == -1){
            perror("Child process 1 failed");
        }
        
        pid_t pid2 = fork();

        if (pid2 == -1){
            return;
        }

        // Second hild process. Set stdin to read end of pipe.
        else if (pid2 == 0){
            dup2(pipefd[0], STDIN_FILENO);
            run_command(command2);
            close(pipefd[0]);
        }

        // Parent. Wait for second child to complete execution
        else{
            int status2;
            pid_t res2 = waitpid(pid2, &status2, 0);    
        
            if (res2 == -1){
                perror("Child process 2 failed");
            }        
            close(pipefd[0]);
        }
    }    

    return;
}



int main () {
    while (true){
        run();
    }
    
    return 0;
}