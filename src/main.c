#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>

// Array of char pointers. A string is a char pointer, so this is an array of strings
const char *SUPPORTED_COMMANDS[] = {"ls", "cat", "cd", "echo", "pwd", "grep"};
const int NUM_SUPPORTED = sizeof(SUPPORTED_COMMANDS) / sizeof(char *);
const int MAX_COMMAND = 50; // Max number of tokens for a single command
const char *COMMAND_DELIMITER = " ";
const char *PIPE_DELIMITER = "|";
const char *OUTPUT_REDIR_DELIM = ">";
const char *OUTPUT_REDIR_DELIM2 = ">>";
const char *INPUT_REDIR_DELIM = "<";
pid_t parent_id; 


// Used to run an individual command. This is executed by a child process, so no need to fork in here
// NOTE: processes than run this command MAY have stdout redirected to the write end of a pipe (except for 
// the last such process). So, we cannot print to stdout here, only to stderr in the case of errors
void run_command(char* command_str){ 
    //fprintf(stderr, "\nrun_command()");
    //fprintf(stderr,"\n%s\n", command_str);
    //fprintf(stderr, "Process ID: %d\n", getpid());

    // We first split on >>, in case there is output redirection occuring. 
    char *output_redir = strstr(command_str, OUTPUT_REDIR_DELIM2);
    char *output_file = NULL;
    bool append = false;
    
    if (output_redir != NULL){
        *output_redir = '\0'; // dereference pointer to access the char value in command_str
        output_file = output_redir + strlen(OUTPUT_REDIR_DELIM2);

        // Remove any leading whitespace between the >> and the output file name
        while (output_file != NULL && isspace(*output_file)){
            output_file++;
        }

        // Remove any trailing whitespace from the output file name
        char *end = output_file + strlen(output_file) - 1;
        while (isspace(*end)){
            *end = '\0';
            end -= 1;
        }

        append = true;
    } 
    // If >> was not found in the command, we can go ahead and check for >
    else if ((output_redir = strchr(command_str, OUTPUT_REDIR_DELIM[0])) != NULL) {
        *output_redir = '\0'; // dereference pointer to access the char value in command_str
        output_file = output_redir + 1;

        // Remove any leading whitespace between the >> and the output file name
        while (output_file != NULL && isspace(*output_file)){
            output_file++;
        }

        // Remove any trailing whitespace from the output file name
        char *end = output_file + strlen(output_file) - 1;
        while (isspace(*end)){
            *end = '\0';
            end -= 1;
        }
    }  

    // We also split on <, in case there is input redirection occuring. Note that its possible for both input
    // and output redirection to occur.
    char *input_redir = strchr(command_str, INPUT_REDIR_DELIM[0]);
    char *input_file = NULL;

    if (input_redir != NULL){
        *input_redir = '\0';
        input_file = command_str;
        command_str = input_redir + 1;

        // Remove any trailing whitespace between the input file name and the <
        char *end = input_file + strlen(input_file) - 1;
        while (isspace(*end)){
            *end = '\0';
            end -= 1;
        }

        // Remove any leading whitespace from the input file name
        while (input_file != NULL && isspace(*input_file)){
            input_file++;
        }
    }


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
            perror("Not a supported command");
            exit(1);
        }        
    }

    char *command[MAX_COMMAND+1];
    command[0] = curr_token;
    int currSize = 1;

    // strtok will ignore contiguous occurrances of " " between tokens, as well as trailing whitespace
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
            perror("Too many or too few arguments for cd command");
            return;
        } 
        //printf("running cd\n");
        //printf("%s\n", command[1]);
        chdir(command[1]); // first argument should be the path
        exit(0);
    }

    // First, we redirect stdout to the specified file if the user chose to do so.
    if (output_file != NULL){
        int flags = O_WRONLY | O_CREAT;

        if (append){
            flags = flags | O_APPEND;
        } else{
            flags = flags | O_TRUNC;
        }
        
        int fd = open(output_file, flags, S_IRWXU);

        if (fd == -1){
            perror("\nFailed to open output file");
            return;
        }

        dup2(fd, STDOUT_FILENO);
    }

    // We also redirect stdin to the specified file if the user chose to do so
    if (input_file != NULL){
        int fd = open(input_file, O_RDONLY, S_IRWXU);

        if (fd == -1){
            perror("\nFailed to open input file");
            return;
        }

        dup2(fd, STDIN_FILENO);
    }


    execvp(command[0], command);
}


// Run a command with two or more commands piped together
void run_piped(char* piped_command){
    //fprintf(stderr, "\nrun_piped()");
    //fprintf(stderr,"\n%s", piped_command);
    //fprintf(stderr,"\nProcess ID: %d", getpid());

    // If there are pipes in the command, we must split on then, and run each command seperately
    // We split on the first occurance of |. We execute the first part as it, and recursively process the 
    // second part, since it may contain more pipes    
    char *first_pipe = strchr(piped_command, PIPE_DELIMITER[0]);
    char *command1 = piped_command;
    char *command2 = NULL;

    // If there is a pipe in the command, we replace it with a null terminator
    if (first_pipe != NULL){
        *first_pipe = '\0';
        command2 = first_pipe + 1;
    }

    //fprintf(stderr,"\ncommand 1: %s", command1);
    while (command2 != NULL && isspace(*command2)){
        command2++;
        //fprintf(stderr,"\ncommand 2: %s", command2);
    }   

    // If there is only one command, just fork one time and run the command.
    // Forking is only necessary if the (original) parent is calling this function. Otherwise, if its a child,
    // it can just call run_command directly. 
    if (command2 == NULL){
        if (getpid() == parent_id){
            pid_t pid = fork();

            if (pid == -1){
                return;
            }

            // Child process.
            else if (pid == 0){
                run_command(command1);            
            }

            // Parent process. Wait for child process to complete execution
            else{
                int status;
                pid_t res = waitpid(pid, &status, 0);

                if (res == -1){
                    perror("\nChild process failed");
                }
            }
            return;
        } else{
            run_command(command1);
        }
    }

    int pipefd[2];
    int pipe_res = pipe(pipefd);

    if (pipe_res == -1){
        perror("Failed to create pipe");
        if (getpid() != parent_id) exit(1); // May be a child process
        return;
    }

    pid_t pid = fork();

    if (pid == -1){
        perror("Failed to fork process");
        if (getpid() != parent_id) exit(1); // May be a child process
        return;
    }

    // First child process. Set stdout to write end of pipe. Close the read end. We can call run_command directly here, 
    // since this command is guaranteed to be a single command (i.e. this command will not have any pipes in it)
    else if (pid == 0){
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        run_command(command1);   
    }

    // Parent process. Wait for first child process to complete execution. Close write end of pipe. Then execute second command in new child process
    // Since stderr for the child process still points to the terminal, error handling is automatically handled. 
    else {
        close(pipefd[1]);
        int status;
        pid_t res = waitpid(pid, &status, 0);    
    
        if (res == -1){
            perror("\nChild process failed");
        }
        
        pid_t pid2 = fork();

        if (pid2 == -1){
            perror("Failed to fork process");
            if (getpid() != parent_id) exit(1); // May be a child process
            return;
        }

        // Second child process. Set stdin to read end of pipe. We then close pipefd[0] since its now redundant. stdin closes itself once the process
        // is terminated. Since this command MAY have multiple pipes in it, we recursively call run_piped().
        else if (pid2 == 0){
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            run_piped(command2);          
        }

        // Parent. Wait for second child to complete execution. Since this case can be reached by a child (when a child calls run_piped() with a piped
        // command), we need to handle this case by comparing if the current process' PID matches the original parent or not
        else{
            close(pipefd[0]);
            int status2;
            pid_t res2 = waitpid(pid2, &status2, 0);    
        
            if (res2 == -1){
                perror("Child process failed");
                if (getpid() != parent_id) exit(1);              
            }

            if (getpid() != parent_id) exit(0);         
        }
    }    

    return;
}


// Prompt user to enter a command
// Each call to run() exists on a seperate stack frame. However, each stack frame for a run() call is placed in the same 
// location in the stack (right above main()), so char x[50] is also placed in the same location as the previous call to 
// run(). If fgets reads nothing and hits an EOF, x will still point to the same data as the previous run, which would be the
// previous-most command that was processed. 
void run() {
    //fprintf(stderr,"\nrun()\n");
    parent_id = getpid();
    //fprintf(stderr,"Process ID: %d\n", parent_id);
    char x[100];
    printf("mysh> ");
    char * res = fgets(x, sizeof(x), stdin);

    // If there is no input, return to calling location, which will reprompt user for input
    if (res == NULL){
        return;
    }

    x[strcspn(x, "\n")] = '\0';
    run_piped(x);    
    return;    
}


int main () {
    int x = 0;
    while (x < 10){
        run();
        x += 1;
    }

    return 0;
}