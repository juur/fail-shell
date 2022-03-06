#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

int main(int argc, char *argv[])
{
	if (argc != 2)
		errx(EXIT_FAILURE, "Usage: dirname path");

	printf("%s\n", dirname(argv[1]));
}
