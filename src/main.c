#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>

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

// Used to save command history
const int NUM_COMMANDS = 50;
char *command_history[NUM_COMMANDS];
int saved_commands = 0;


void custom_perror(char* error){
    perror(error);
    printf("\r");
    fflush(stdout);
}

// Direction is 1 for up and -1 for down
char *get_history(int direction){
    int index = saved_commands + direction % NUM_COMMANDS;
    return command_history[saved_commands];
}


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
            printf("child process 2 about to run command \r\n");
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


// Prompt user to enter a command
// Each call to run() exists on a seperate stack frame. However, each stack frame for a run() call is placed in the same 
// location in the stack (right above main()), so char x[50] is also placed in the same location as the previous call to 
// run(). If fgets reads nothing and hits an EOF, x will still point to the same data as the previous run, which would be the
// previous-most command that was processed. 
void run() {
    parent_id = getpid();
    char buffer[1000];
    char c; // Used to store the character most recently read
    int i = 0; // number of characters read into the buffer thus far    

    // By default, stdout is line-buffered when it is connected to a terminal. That is, what we write to stdout is buffered until 
    // a newline is received, or once the buffer is filled up. fflush() will flush everything buffered in stdout into its destination file/location, 
    // which in this case is the terminal
    printf("mysh> ");
    fflush(stdout);

    while (read(STDIN_FILENO, &c, 1) == 1){
        // Handle Enter press
        if (c == 10 || c == 13){
            buffer[i] = '\0';
            printf("\r\n");
            break;
        }

        // Handle backspace press
        if (c == 127 || c == 8){
            // Prevents user from deleting the system prompt itself
            if (i > 0) {
                i -= 1;
                printf("\b \b");
                fflush(stdout);                
            }
            continue;            
        }

        // For up arrow, move to next position in history array, replace whats in the current line of the terminal with this command,
        // and also replace the contents of the buffer with this command
        if (c == 8593){
            char *history_command = get_history(1);

        }

        // TODO: handle left/right arrow press for changing cursor position

        // TODO: shift characters to the right of the typing location, rather than replacing them

        buffer[i] = c;
        i += 1;
        printf("%c", c); // echo the character to the terminal
        fflush(stdout); 
    }

    // If there is no input, return to calling location, which will reprompt user for input
    if (i == 0){
        return;
    }

    command_history[saved_commands] = buffer;

    if (saved_commands == NUM_COMMANDS - 1){
        saved_commands = 0;
    }

    run_piped(buffer);    
    return;    
}

// Used to switch from Canonical mode to Raw mode. This essentially disables a lot of the responsibilities of the TTY (backspace, echoing back 
// to terminal, etc.), thus requiring this shell program to handle those responsibilities.
void enable_raw_mode(){
    struct termios raw;

    // Get the file descriptor for the teletypewriter (tty), which the terminal is guaranteed to be connected to
    // When we open /dev/tty, the tty driver will resolve this request to the terminal that was used to run this shell program
    // This ensures that even if stdin/out/err get redirected (somehow), that this function can still access the terminal
    int fd = open("/dev/tty", O_RDWR);

    if (fd == -1){
        custom_perror("open");
        return;
    }

    // Get the current terminal settings, and store them in raw.
    if (tcgetattr(fd, &raw) == -1){
        custom_perror("tcgetattr");
        return;
    }

    // Disable canonical mode (line buffering) and echo. This prevents characters written by the user in the terminal to actually 
    // appear in the terminal. The TTY will not automatically echo the characters back anymore. This is something we have to manually
    // do in the shell program
    raw.c_lflag &= ~(ICANON | ECHO);

    // Disable input processing (ex. ctrl+c, ctrl+z, CR to NL)
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Disable output post-processing
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);

    // Apply changes to the terminal
    if (tcsetattr(fd, TCSANOW, &raw) == -1){
        custom_perror("tcsetattr");
    }
}


int main () {
    enable_raw_mode(); // switch from canonical to raw mode

    // For debugging purposes only
    int x = 0;
    while (x < 10){
        run();
        x += 1;
    }

    return 0;
}
