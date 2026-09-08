CC = gcc
CFLAGS = -Wall
DEPS = modules.h
OBJ = main.o run.o run_command.o run_piped.o

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

shell: $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(OBJ) shell

