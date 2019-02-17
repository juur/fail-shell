#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	if (argc != 1)
		err(EXIT_FAILURE, "Usage: tty");

	char *name = ttyname(STDIN_FILENO);
	if (name)
		printf("%s\n", name);
	else
		printf("not a tty\n");
}
