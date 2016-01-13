CFLAGS = -Wall -g

OBJS = mysh.o

TOBJS = test_harness.o parser.o

all: mysh

mysh: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TOBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean: 
	rm -f *.o *~ mysh test

.PHONY: all clean

