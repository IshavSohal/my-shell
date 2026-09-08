#include "modules.h"

// Gets the next position in the command_history array, with direction considered. Wrapping is handled. 1 is up, -1 is down
int get_history_index(int position, int direction){
    //printf("poition: %d\r\n", position);
    //printf("direction: %d\r\n", direction);
    int index = MOD((position - (direction)), NUM_COMMANDS);
    //printf("index: %d\r\n", index);

    return index;
}

// Returns the next position in the history array to add a command. Wrapping is handled
// If this is the first command being stored, we store it at `command_idx`. Otherwise we increment
// command_idx (with wrapping) and store it at the resulting location
int next_history_index(){
    if (strlen(command_history[command_idx]) != 0){
        command_idx = MOD((command_idx + 1), NUM_COMMANDS);
    }    
    return command_idx;
}


// Prompt user to enter a command
// Each call to run() exists on a seperate stack frame. However, each stack frame for a run() call is placed in the same 
// location in the stack (right above main()), so char x[50] is also placed in the same location as the previous call to 
// run(). If fgets reads nothing and hits an EOF, x will still point to the same data as the previous run, which would be the
// previous-most command that was processed. 
void run() {
    parent_id = getpid();
    char buffer[BUFFER_SIZE];
    char c; // Used to store the character most recently read
    int i = 0; // number of characters read into the buffer thus far  

    // For keeping track of which historical command we're on for the current run(). A value of -1 means no history command is selected
    int history_index = -1;  

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

        // For up arrow key, move to next position in history array, replace whats in the current line of the terminal with this command,
        // and also replace the contents of the buffer with this command. The terminal escape sequence for the keys on the right side of 
        // the keyboard is: \e or ^[. Once this is identified, we need to determine what key is pressed
        // Input for up arrow: ^[ [ A
        // Input for down arrow: ^[ [ B
        // Input for left arrow: ^[ [ C
        // Input for down arrow: ^[ [ D
        if (c == '\x1b'){       
            char char_seq[10];
            int j = 0;

            if (read(STDIN_FILENO, &c, 1) == 1){
                char_seq[j] = c;
                j += 1;
            } 
            
            if (read(STDIN_FILENO, &c, 1) == 1){
                char_seq[j] = c;
                j += 1;
            } 
            char_seq[j] = '\0';

            // Up arrow key
            if (strcmp(char_seq, "[A") == 0){
                if (history_index == -1){
                    history_index = command_idx;
                } else{
                    history_index = get_history_index(history_index, 1);
                }
                //printf("history index: %d\r\n", history_index);
                char *history_command = command_history[history_index];
                int k = 0;

                // Calloc sets the bytes of allocated memory to 0, but the strlen is 0, because
                // no chars are identified
                if (strlen(history_command) != 0){
                    // Clear the currently written command in the terminal
                    while (i > 0) {
                        i -= 1;
                        printf("\b \b");
                        fflush(stdout);                
                    }

                    // Update buffer to reflect the history command. Rather than having the buffer point to the 
                    // history command, we set the chars of the buffer one at a time. 
                    while (history_command[k] != '\0'){
                        buffer[k] = history_command[k];
                        k += 1;
                    }
                    buffer[k] = '\0';
                    i = k;
                
                    printf("%s", buffer);
                    fflush(stdout);
                } else{ // Reset history_index to original value, or back to -1 if the history array is empty
                    history_index = (history_index != command_idx) ? get_history_index(history_index, -1) : -1;
                }
            }

            // Down arrow key. This allows users to go back to previous commands in the history array that have already been traversed
            // in the current run(). If history_index is -1, do nothing, as there are no other history commands to traverse.
            // If history_index is command_idx, we set the text in the terminal to empty, to allow the user to type a new command without
            // having to manually delete chars of a historical command
            if (strcmp(char_seq, "[B") == 0 && history_index != -1){
                if (history_index == command_idx){
                    // Clear the currently written command in the terminal
                    while (i > 0) {
                        i -= 1;
                        printf("\b \b");
                        fflush(stdout);                
                    }
                    history_index = -1;
                } else{
                    history_index = get_history_index(history_index, -1);
                    char *history_command = command_history[history_index];
                    int k = 0;
        
                    // Clear the currently written command in the terminal
                    while (i > 0) {
                        i -= 1;
                        printf("\b \b");
                        fflush(stdout);                
                    }

                    // Update buffer to reflect the history command. Rather than having the buffer point to the 
                    // history command, we set the chars of the buffer one at a time. 
                    while (history_command[k] != '\0'){
                        buffer[k] = history_command[k];
                        k += 1;
                    }
                    buffer[k] = '\0';
                    i = k;
                
                    printf("%s", buffer);
                    fflush(stdout);
                }                
            }

            // left arrow key

            // right arrow key

            // delete key

            // home key
    
            // end key

            // insert key

            // pg up key (same as up arrow key)

            // pg down key (same as down arrow key)
            continue;
            //char *history_command = get_history(1);
        }

        // TODO: handle left/right arrow press for changing cursor position. Prevent the cursor from moving into the system prompt, otherwise user would be
        // able to delete it
        // TODO: shift characters to the right of the typing location, rather than replacing them
        buffer[i] = c;
        i += 1;        
        history_index = -1; // Once at least one character has been manually written, we exit "history mode" by resetting the history index
        printf("%c", c); // echo the character to the terminal
        fflush(stdout); 
    }

    // printf("Buffer: %s\r\n", buffer);
    // fflush(stdout);

    // If there is no input, return to calling location, which will reprompt user for input
    if (i == 0){
        return;
    }

    // Increment command_idx, then write whats in the buffer to that index. 
    // TODO: When a history command is used, it gets written into the history array again
    int history_write_index = next_history_index();
    //printf("write index: %d \r\n", history_write_index);
    strcpy(command_history[history_write_index], buffer);

    run_piped(buffer);    
    return;    
}
