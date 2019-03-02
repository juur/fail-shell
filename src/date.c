#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <time.h>
#include <locale.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: date [-u] [+format]\n"
			"       date [-u] mmddhhmm[[cc]yy]\n");
	exit(EXIT_FAILURE);
}

static int opt_tz_utc = 0;

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");

	{
		int opt;

		while ((opt = getopt(argc, argv, "u")) != -1)
		{
			switch (opt)
			{
				case 'u':
					opt_tz_utc = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	const char *fmt = NULL;

	if (optind == argc)
		fmt = "%a %b %e %H:%M:%S %Z %Y";
	else if (*argv[optind] != '+')
		show_usage();
	else 
		fmt = argv[optind]+1;

	const time_t t = time(NULL);

	if (t == -1) {
		err(EXIT_FAILURE, NULL);
	}

	const struct tm *tm;
	
	if (opt_tz_utc)
		tm = gmtime(&t);
	else
		tm = localtime(&t);

	if (tm == NULL) {
		err(EXIT_FAILURE, NULL);
	}

	char buf[BUFSIZ];

	if (strftime(buf, BUFSIZ, fmt, tm) == 0) {
		err(EXIT_FAILURE, NULL);
	}

	printf("%s\n", buf);
}
