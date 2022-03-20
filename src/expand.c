#define _XOPEN_SOURCE 700

#include <stdbool.h>
#include <stdlib.h>
#include <err.h>
#include <getopt.h> 
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

static int  opt_single_tab = 8;
static bool opt_tablist    = false;
static int  opt_tl_size    = 0;
static int *tabs           = NULL;

__attribute__((noreturn))
static void show_usage(void)
{
	errx(EXIT_FAILURE, "Usage: expand [-t tablist] [file...]");
}

static bool isnumber(const char *str)
{
	const char *ptr = str;

	while (*ptr)
	{
		if (!isdigit(*(ptr++)))
			return false;
	}

	return true;
}

inline static long max(long a, long b)
{
	return a > b ? a : b;
}

inline static long min(long a, long b)
{
	return a < b ? a : b;
}

static void do_expand(FILE *fp)
{
	char *line = NULL;
	size_t len = 0;
	int tab, pos, acctab, offset;
	char *ptr;

	while (getline(&line, &len, fp) != -1)
	{
		ptr = line;
		tab = 0;
		pos = 0;
		acctab = 0;
		offset = 0;

		if (!line)
			break;

		while (*ptr)
		{
			if (*ptr != '\t') {
				if (fputc(*(ptr++), stdout) == EOF)
					goto done;

				pos++;
				continue;
			}

			ptr++;

			/* seek to the next tab */
			if (opt_tablist) {
				while (tab < opt_tl_size) 
					if ((acctab = tabs[tab++]) > pos)
						break;
			} else {
				while (acctab <= pos)
					acctab += opt_single_tab;
			}

			offset = max(1, acctab - pos);

			if (fprintf(stdout,"%*s", offset, " ") < 0)
				goto done;

			pos += offset;

		}

		free(line);
		line = NULL;
		len = 0;
	}
	return;

done:
	if (line)
		free(line);
}

int main(int argc, char *argv[])
{
	char *tablist = NULL;

	{
		int opt;

		while ((opt = getopt(argc, argv, "ht:")) != -1)
		{
			switch(opt)
			{
				case 't':
					tablist = optarg;
					break;
				default:
					show_usage();
			}
		}
	}

	if (tablist && !isnumber(tablist)) {
		opt_tablist = true;
		if (!strchr(tablist, ','))
			errx(EXIT_FAILURE, "tablist must be comma seperated");

		char *ptr;

		for (opt_tl_size = 1, ptr = tablist; *ptr; ptr++)
			if (*ptr == ',')
				opt_tl_size++;

		if ((tabs = calloc(opt_tl_size + 1, sizeof(int *))) == NULL)
			err(EXIT_FAILURE, "calloc");

		ptr = strtok(tablist, ",");

		int largest = 0, val, cnt = 0;
		do
		{
			errno = 0;
			val = atoi(ptr);

			if (val < 0 || val <= largest || errno)
				errx(EXIT_FAILURE, "invalid tablist: tabs must be positive integers in ascending order");

			if (cnt == opt_tl_size)
				errx(EXIT_FAILURE, "opt_tl_size");

			largest = tabs[cnt++] = val;

			ptr = strtok(NULL, ",");
		} while(ptr);

	} else if (tablist)
		opt_single_tab = atoi(tablist);

	FILE *fp;

	if (argc == optind) {
		do_expand(stdin);
		goto done;
	}

	for (; optind < argc; optind++) {
		if ((fp = fopen(argv[optind], "r")) == NULL) {
			warn("fopen");
		} else {
			do_expand(fp);
			fclose(fp);
		}
	}

done:
	if (tabs)
		free(tabs);

	exit(EXIT_SUCCESS);
}
