#include "modules.h"

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

char **command_history;
int command_idx = 0;

void custom_perror(char* error){
    perror(error);
    printf("\r");
    fflush(stdout);
}


// Used to switch from Canonical mode to Raw mode. This disables a lot of the responsibilities of the TTY (backspace, echoing back 
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

void disable_raw_mode(){
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

    // Enable canonical mode (line buffering) and echo. 
    raw.c_lflag |= (ICANON | ECHO);

    // Disable input processing (ex. ctrl+c, ctrl+z, CR to NL)
    raw.c_iflag |= (BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // Disable output post-processing
    raw.c_oflag |= (OPOST);
    raw.c_cflag &= ~(CS8);

    // Apply changes to the terminal
    if (tcsetattr(fd, TCSANOW, &raw) == -1){
        custom_perror("tcsetattr");
    }
}


void cleanup(){
    for (int i = 0; i < NUM_COMMANDS; i++){
        free(command_history[i]);
    }
    free(command_history);
}


int main () {
    enable_raw_mode(); // switch from canonical to raw mode

    // Need to switch back to canoncial mode when the program exits on its own. Otherwise the terminal get stuck in raw mode
    if (atexit(disable_raw_mode) != 0){
        custom_perror("Failed to set exit function.");
    }
    
    // Need to deallocate the allocated memory prior to exiting to avoid memory leaks
    if (atexit(cleanup) != 0){
        custom_perror("Failed to set exit function.");
    }

    // Need to allocate space for the history array. We will support NUM_COMMANDS historical commands, 
    // and for each historical command, we will support at most BUFFER_SIZE chars
    command_history = malloc(NUM_COMMANDS * sizeof(char *));
    if (command_history == NULL){
        exit(1);
    }

    for (int i = 0; i < NUM_COMMANDS; i++){
        command_history[i] = calloc(BUFFER_SIZE, sizeof(char));
        if (command_history[i] == NULL){
            exit(1);
        }
    }

    // For debugging purposes only
    int x = 0;
    while (x < 10){
        run();
        x += 1;
    }

    return 0;
}
