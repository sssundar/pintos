CC = gcc

CFLAGS = -I. -Wall -g

OBJS = mysh.o parser.o

TOBJS = test_harness.o parser.o

all: mysh

mysh: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)\
	-L/usr/local/lib -I/usr/local/include -lreadline

test: $(TOBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean: 
	rm -f *.o *~ mysh test

.PHONY: all clean

