CC:=/home/build/opt/bin/tcc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra

default:	test

clean:
		rm -f test test.o

test:	test.c

.PHONY:	default clean
