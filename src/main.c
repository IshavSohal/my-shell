#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

const char *SUPPORTED_COMMANDS[] = {"ls", "cat", "cd", "echo"};
const int NUM_SUPPORTED = 4;
const int MAX_COMMAND = 50; // Max number of tokens for a single command
const char *COMMAND_DELIMITER = " ";


// Prompt user to enter a command
void run() {
    char x[50];
    printf("mysh> ");
    fgets(x, sizeof(x), stdin);
    x[strcspn(x, "\n")] = '\0';
    //printf("%s\n", x);

    // Parse user input, determine which valid command was executed, if any
    char *curr_token = strtok(x, COMMAND_DELIMITER);


    if (curr_token != NULL){
        // First token must be the name of a supported command
        bool is_supported = false;
        //printf("curr_token: %s\n", curr_token);
        for (int i = 0; i < NUM_SUPPORTED; i++){
            //printf("Supported command: %s\n", SUPPORTED_COMMANDS[i]);
            
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

        char *command[MAX_COMMAND+1];
        command[0] = curr_token;
        int currSize = 1;

        while (curr_token != NULL && currSize < MAX_COMMAND){
            curr_token = strtok(NULL, COMMAND_DELIMITER);
            command[currSize] = curr_token;
            currSize += 1;
        }
        command[currSize] = NULL;

        // Run command by forking process and calling exec. Define a uni-directional pipe so that parent can read stdout of child
        // int pipefd[2];
        // if (pipe(pipefd) != 0){
        //     fprintf(STDERR_FILENO, "Failed to create pipe");
        //     return;
        // }

        pid_t pid = fork();

        if (pid < 0){
            fprintf(stderr, "Failed to fork process");
            return;
        }
        else if (pid == 0){
            // Child process executes command, and sents output back to parent process using the pipe
            //close(pipefd[0]);            
            execvp(command[0], command);
        } 
        else{
            // parent process reads stdout from child process and outputs it to the command line
            //close(pipefd[1]);
            int status;
            waitpid(pid, &status, 0);
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