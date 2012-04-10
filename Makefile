CC = gcc
CFLAGS = -O3 -fno-guess-branch-probability -Wall -g
LDFLAGS = -levent

ALL : lisabench

lisabench : lisabench.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS)

clean : 
	rm -f *.o lisabench
