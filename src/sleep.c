#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

int main(int argc, char *argv[])
{
	if (argc != 2)
		errx(EXIT_FAILURE, "Usage: sleep time");

	errno = 0;
	char *endptr = NULL;
	long duration = strtol(argv[1], &endptr, 10);

	if (errno)
		err(EXIT_FAILURE, "Error parsing '%s'", argv[1]);
	if (endptr != NULL)
		errx(EXIT_FAILURE, "Not a number '%s'", argv[1]);
	if (duration < 0)
		errx(EXIT_FAILURE, "Positive time only");

	signal(SIGALRM, SIG_IGN);
	long left = sleep(duration);
	signal(SIGALRM, SIG_DFL);

	if (left == 0)
		exit(EXIT_SUCCESS);
	err(EXIT_FAILURE, "Interrupted sleep");
}
