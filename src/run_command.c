#include "modules.h"

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
            custom_perror("Not a supported command");
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
            custom_perror("Too many or too few arguments for cd command");
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
            custom_perror("\nFailed to open output file");
            return;
        }

        dup2(fd, STDOUT_FILENO);
    }

    // We also redirect stdin to the specified file if the user chose to do so
    if (input_file != NULL){
        int fd = open(input_file, O_RDONLY, S_IRWXU);

        if (fd == -1){
            custom_perror("\nFailed to open input file");
            return;
        }

        dup2(fd, STDIN_FILENO);
    }

    // If stdout is pointing to the terminal, we need to replace each \n with \r\n, since we're in Raw mode. We do 
    // this by writing the output to a pipe, rather than stdout. Then, we have another process run 
    // "sed -z 's/\n/\r\n/g'" to replace each \n with \r\n, then have that process write to stdout.
    // If stdout is pointing elsewhere, do nothing, since Raw mode is only applied to the terminal
    if (isatty(STDOUT_FILENO)){        
        int pipefd[2];
        int pipe_res = pipe(pipefd);

        if (pipe_res == -1){
            custom_perror("Failed to create pipe");
            exit(1);
        }

        pid_t pid = fork();
        
        if (pid == -1){
            custom_perror("Failed to fork process");
            exit(1); // May be a child process
        }

        // Child process. Set stdout to write end of pipe. Close the read fd of the pipe.
        // This process will execute the command itself
        else if (pid == 0){
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            execvp(command[0], command);  
        }

        // Parent process. Close the write fd of the pipe. Redirect stdin to the read end of the pipe, then close the read fd of the pipe as well.
        // Wait for first child process to complete execution. Close write end of pipe. Then execute the sed command in new child process. 
        // This output is what will be written to stdout
        else {
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);

            int status;
            pid_t res = waitpid(pid, &status, 0);

            if (res == -1){
                custom_perror("\nChild process failed");
                exit(1);
            }

            char *sed_args[] = {"sed", "s/$/\\r/g", NULL};
            execvp(sed_args[0], sed_args); 
            custom_perror("Failed to exec on sed command");
            exit(1);
        }

    } else{
        execvp(command[0], command);
        custom_perror("Failed to exec on sed command");
        exit(1);
    }
  
}