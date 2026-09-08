#include "modules.h"

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
                    custom_perror("\nChild process failed");
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
        custom_perror("Failed to create pipe");
        if (getpid() != parent_id) exit(1); // May be a child process
        return;
    }

    pid_t pid = fork();

    if (pid == -1){
        custom_perror("Failed to fork process");
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
            custom_perror("\nChild process failed");
        }
        
        pid_t pid2 = fork();

        if (pid2 == -1){
            custom_perror("Failed to fork process");
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
                custom_perror("Child process failed");
                if (getpid() != parent_id) exit(1);              
            }

            if (getpid() != parent_id) exit(0);         
        }
    }    

    return;
}
