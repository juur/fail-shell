#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

static int opt_bytes = 0;
static int opt_chars = 0;
static int opt_lines = 0;
static int opt_words = 0;

/* global counts */
static int glines = 0, gwords = 0, gbytes = 0, gchars = 0;

static void show_usage(void)
{
	errx(EXIT_FAILURE, "Usage: wc [-c|-m] [-lw] [file...]");
}

static void do_wc(const char *file)
{
	const bool do_stdin = file == NULL ? true : false;
	FILE *fp;

	if (!do_stdin && (fp = fopen(file, "r")) == NULL) {
		warn("fopen '%s'", file);
		return;
	} else if (do_stdin)
		fp = stdin;

	char *line = NULL;
	size_t len = 0;
	ssize_t rc;
	const char *ptr;

	int nlines=0, nbytes=0, nchars=0, nwords=0;

	while ((rc = getline(&line, &len, fp)) != -1)
	{
		if (len == 0)
			continue;

		nbytes += rc;
		nlines++;
		ptr = line;

		while (*ptr)
		{
			/* skip any leading spaces */
			while (*ptr && isspace(*ptr)) ptr++;
			if (!*ptr)
				break;

			/* now skip the word */
			while (*ptr && !isspace(*ptr)) ptr++;
			nwords++;
		}

		free(line);
		line = NULL;
		len = 0;
	}

	if (opt_lines)
		printf("%d ", nlines);
	if (opt_words)
		printf("%d ", nwords);
	if (opt_bytes)
		printf("%d ", nbytes);
	else if (opt_chars)
		printf("%d ", nchars);
	if (file)
		printf("%s", file);

	glines += nlines;
	gwords += nwords;
	gbytes += nbytes;
	gchars += gchars;

	fputc('\n', stdout);

	if (!do_stdin)
		fclose(fp);
}

int main(int argc, char *argv[])
{
	{
		int opt;

		while ((opt = getopt(argc, argv, "cmlwh")) != -1)
		{
			switch (opt)
			{
				case 'c':
					opt_bytes = 1;
					break;
				case 'm':
					opt_chars = 1;
					errx(EXIT_FAILURE, "multibyte character support is not implemented");
					break;
				case 'w':
					opt_words = 1;
					break;
				case 'l':
					opt_lines = 1;
					break;
				default:
					show_usage();
			}
		}

		if (opt_bytes + opt_words > 1)
			show_usage();

		if (opt_bytes + opt_words + opt_lines + opt_chars == 0)
			opt_lines = opt_words = opt_bytes = 1;
	}

	/* figure out of we're doing multiple files or not */
	const bool summary = (argc - optind > 1) ? true : false;

	if (optind == argc) {
		do_wc(NULL);
	} else
		while (optind < argc)
			do_wc(argv[optind++]);

	if (summary) {
		if (opt_lines)
			printf("%d ", glines);
		if (opt_words)
			printf("%d ", gwords);
		if (opt_bytes)
			printf("%d ", gbytes);
		else if (opt_chars)
			printf("%d ", gchars);
		printf("total\n");
	}

	exit(EXIT_SUCCESS);
}
