#define _XOPEN_SOURCE 700

#include <err.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static void show_usage()
{
	errx(EXIT_FAILURE, "Usage: cmp [-l|-s] file1 file2");
}

static int opt_each   = 0;
static int opt_silent = 0;

static int do_cmp(FILE *fd1, FILE *fd2, const char *fn1, const char *fn2)
{
	size_t  byte     = 0;
	size_t  line     = 0;
	int     rc       = 0;
	int     fd1_chr;
	int     fd2_chr;

	while (1)
	{
		fd1_chr = fgetc(fd1);
		fd2_chr = fgetc(fd2);

		if (fd1_chr == EOF && fd2_chr == EOF) {
			if (ferror(fd1) || ferror(fd2))
				return 2;
			return 0;
		}

		if (fd1_chr == EOF) {
			if (!opt_silent)
				warnx("cmp: EOF on %s: char %ld, line %ld\n", fn1, byte, line);
			return 1;
		}

		if (fd2_chr == EOF) {
			if (!opt_silent)
				warnx("cmp: EOF on %s: char %ld, line %ld\n", fn2, byte, line);
			return 1;
		}

		byte++;

		if (fd1_chr == '\n')
			line++;

		if (fd1_chr != fd2_chr) {
			if (!opt_each) {
				if (!opt_silent)
					printf("%s %s differ: char %ld, line %ld\n",
							fn1, fn2, byte, line);
				return 1;
			}
			rc = 1;
			printf("%ld %o %o\n", byte, fd1_chr, fd2_chr);
		}

	}

	return rc;
}

int main(int argc, char *argv[])
{
	{
		int opt;
		while ((opt = getopt(argc, argv, "ls")) != -1)
		{
			switch (opt)
			{
				case 'l':
					if (opt_silent)
						show_usage();
					opt_each = 1;
					break;

				case 's':
					if (opt_each)
						show_usage();
					opt_silent = 1;
					break;

				default:
					show_usage();
			}
		}
	}

	if (argc - optind != 2)
		show_usage();

	FILE *fd1, *fd2;

	if ((fd1 = fopen(argv[optind], "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed for %s", argv[optind]);

	if ((fd2 = fopen(argv[optind + 1], "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed for %s", argv[optind + 1]);

	exit(do_cmp(fd1, fd2, argv[optind], argv[optind + 1]));

	fclose(fd2);
	fclose(fd1);
}
