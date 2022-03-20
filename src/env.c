#define _XOPEN_SOURCE 700

#include <getopt.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <libgen.h>
#include <stdbool.h>

extern char **environ;

static int opt_ignore_env = 0;

__attribute__((noreturn))
static void show_usage()
{
	errx(EXIT_FAILURE, "Usage: env [-ih] [name=value]... [utility [argument...]]");
}

inline static long min(const long a, const long b)
{
	return a < b ? a : b;
}

/* this function is part of glibc, but not part of POSIX.1-2017 */
static void clearenv(void)
{
	/* TODO this creates a memory leak but as the C library has altered the
	 * (new) environ, it can't be easily free()'d */
	if ((environ = calloc(1, sizeof(char *))) == NULL)
		err(EXIT_FAILURE,"clearenv");
}

int main(int argc, char *argv[])
{
	{
#ifdef __GNU_LIBRARY__
		/* 
		 * glibc will continue to look for options after a non-option has been 
		 * found. this will break the [argument] section of the command line
		 */
		putenv("POSIXLY_CORRECT=1");
#endif
		int opt;
		while ((opt = getopt(argc, argv, "ih")) != -1)
		{
			switch (opt)
			{
				case 'i':
					opt_ignore_env = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	if (opt_ignore_env)
		clearenv();

	/* copy across (any) name=value arguments */
	while (optind < argc && (strchr(argv[optind], '=')) != NULL)
		if (putenv(argv[optind++]) != 0)
			err(EXIT_FAILURE, "putenv");

	char *cmd = NULL;

	if (optind < argc)
		cmd = argv[optind++];

	char **new_argv, *tmp;

	if ((new_argv = calloc(argc - optind + 2, sizeof(char *))) == NULL)
		err(EXIT_FAILURE, "calloc");

	if ((tmp = strdup(cmd ? cmd : "")) == NULL)
		err(EXIT_FAILURE, "strdup");

	new_argv[0] = basename(tmp);

	int i = 1;

	while (optind < argc)
		new_argv[i++] = argv[optind++];

	if (cmd) {
		execve(cmd, new_argv, environ);
		err(EXIT_FAILURE, "execve");
	}

	for (i = 0; environ[i]; i++)
		printf("%s\n", environ[i]);

	free(tmp);
	free(new_argv);
	exit(EXIT_SUCCESS);
}
