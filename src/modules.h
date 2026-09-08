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

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MOD(a, b) (((a) % (b) + (b)) % (b))     

// Array of char pointers. A string is a char pointer, so this is an array of strings
extern const char *SUPPORTED_COMMANDS[];
extern const int NUM_SUPPORTED;
extern const int MAX_COMMAND; // Max number of tokens for a single command
extern const char *COMMAND_DELIMITER;
extern const char *PIPE_DELIMITER;
extern const char *OUTPUT_REDIR_DELIM;
extern const char *OUTPUT_REDIR_DELIM2;
extern const char *INPUT_REDIR_DELIM;
extern pid_t parent_id; 

// TODO: define a struct for the history variables
enum {NUM_COMMANDS = 50, BUFFER_SIZE=200};
extern char **command_history;
extern int command_idx;

void run_command(char* command_str);
void run_piped(char* piped_command);
void run();
int get_history_index(int position, int direction);
void custom_perror(char* error);
int next_history_index();
 
