#define _XOPEN_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#include <stdio.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: nice [-n increment] utility [argument...]\n");
	exit(EXIT_FAILURE);
}

static int opt_increment = 10;

int main(int argc, char *argv[])
{
	{
		int opt;

		while ((opt = getopt(argc, argv, "n:")) != -1)
		{
			switch (opt)
			{
				case 'n':
					{
						char *endptr = NULL;
						opt_increment = strtol(optarg, &endptr, 10);

						if (*endptr != '\0') 
							err(EXIT_FAILURE, "%s: invalid number", optarg);
					}
					break;
				default:
					show_usage();
			}
		}
	}

	if (optind >= argc)
		show_usage();

	if (nice(opt_increment) == -1)
		err(EXIT_FAILURE, "cannot set niceness");

	if (execvp(argv[optind], &argv[optind]) == -1)
		err(EXIT_FAILURE, "%s", argv[optind]);
}
