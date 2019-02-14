CC:=/home/build/opt/host/bin/tcc
CFLAGS:=-std=c99 -Wpedantic -Wall -Wextra
LDFLAGS:=-static
UTILS:=sh id kill true false ls cp

default:	$(UTILS)

clean:
		rm -f $(UTILS)

.PHONY:	default clean
