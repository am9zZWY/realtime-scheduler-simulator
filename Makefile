CC=gcc
CFLAGS=-I -Wall -Werror -std=c89
DEPS = simulator.h
OBJ = simulator.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

simulator: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f *.o
