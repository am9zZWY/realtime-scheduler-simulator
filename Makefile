CC = gcc
WARNINGS = -Wall -Wextra -Wdocumentation -Wdocumentation-unknown-command -Wsign-compare -Wsign-conversion -Wmissing-variable-declarations
CFLAGS = -I $(WARNINGS) -std=c99
DEPS = simulator.h
OBJ = simulator.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

simulator: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o
