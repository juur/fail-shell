CC:=/home/build/opt/host/bin/tcc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra
LDFLAGS:=-static

default:	test id kill

clean:
		rm -f test test.o id id.o kill kill.o

test:	test.c

id:		id.c

.PHONY:	default clean
