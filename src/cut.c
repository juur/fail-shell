#define _XOPEN_SOURCE 700

#include <err.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

struct range {
	int from;
	int to;
};

static void show_usage(const char *message)
{
	if (message)
		warnx("%s", message);
	fprintf(stderr,
			"Usage: cut -b list [-n] [file...]\n"
			"       cut -c list [file...]\n"
			"       cut -f list [-d delim] [-s] [file...]\n"
		);
	exit(EXIT_FAILURE);
}

static int opt_bytes = 0;
static int opt_chars = 0;
static int opt_fields = 0;
static int opt_no_split_chars = 0;
static int opt_hide_unmatched = 0;
static int opt_delim_not_default = 0;
static char delim = '\t';

struct range **parse_list(const char *string)
{
	struct range **list = NULL;
	int num_entries = 0;
	int entry_l = sizeof(struct range *);
	struct range *current = NULL;
	char *ptr = (char *)string;

	int entry_pos = 0;

	if ((current = calloc(1, sizeof(struct range))) == NULL)
		err(EXIT_FAILURE, NULL);

	current->from = -1;
	current->to = -1;

	while (1)
	{
		if (isdigit(*ptr)) 
		{
			char *from = ptr;
			while (*ptr && isdigit(*ptr)) ptr++;
			int num = ptr - from;
			
			char *num_str;
			if ((num_str = strndup(from, num)) == NULL)
				err(EXIT_FAILURE, NULL);
			num = atoi(num_str);
			free(num_str);

			if (entry_pos)
				current->to = num;
			else
				current->from = num;

		} 
		else if(*ptr == '-') 
		{
			if (++entry_pos > 1)
				errx(EXIT_FAILURE, "ranges are of the form x-y");
			ptr++;

		} 
		else if(*ptr == ',' || *ptr == '\0') 
		{
			if (entry_pos == 0 && current->from == -1 && current->to == -1)
				err(EXIT_FAILURE, "invalid range");
			if (entry_pos == 1 && current->from == -1 && current->to == -1)
				err(EXIT_FAILURE, "missing range end");

			/* number */
			if (entry_pos == 0)
				current->to = current->from;
			/* -number */
			else if (current->from == -1)
				current->from = 0; // FIXME 0 or 1?
			/* number- */
			else if (current->to == -1)
				; // FIXME is -1 ok?

			if ((list = realloc(list, entry_l * (num_entries+1))) == NULL)
				err(EXIT_FAILURE, NULL);
			list[num_entries++] = current;

			if (*ptr == '\0') break;

			if ((current = calloc(1, sizeof(struct range))) == NULL)
				err(EXIT_FAILURE, NULL);

			current->from = -1;
			current->to = -1;
			
			entry_pos = 0;
			ptr++;
		} else
			errx(EXIT_FAILURE, "invalid character '%c'", *ptr);
	}

	/* NULL terminator for list */
	if ((list = realloc(list, entry_l * (num_entries+1))) == NULL)
		err(EXIT_FAILURE, NULL);
	list[num_entries++] = NULL;

	return list;
}

int main(int argc, char *argv[])
{
	char *list_str = NULL;
	struct range **range_list = NULL;

	{
		int opt;
		while ((opt = getopt(argc, argv, "b:nc:f:d:s")) != -1)
		{
			switch (opt)
			{
				case 'b':
					if (opt_bytes++)
						show_usage("duplicate use of -b");
					list_str = optarg;
					break;

				case 'c':
					if (opt_chars++)
						show_usage("duplicate use of -c");
					list_str = optarg;
					break;

				case 'f':
					if (opt_fields++)
						show_usage("duplicate use of -f");
					list_str = optarg;
					break;

				case 'd':
					if (optarg == NULL || !*optarg || strlen(optarg) != 1)
						show_usage("-d must contain a single character delimiter");
					opt_delim_not_default = 1;
					delim = *optarg;
					break;

				case 'n':
					opt_no_split_chars = 1;
					break;

				case 's':
					opt_hide_unmatched = 1;
					break;
			}
		}

		if (opt_bytes + opt_chars + opt_fields != 1)
			show_usage("only one of -b, -c and -f may be specified");

		if (opt_no_split_chars && !opt_bytes)
			show_usage("-n can only be used with -b");

		if (opt_hide_unmatched && !opt_fields)
			show_usage("-s can only be used with -f");

		if (list_str == NULL || !*list_str || strlen(list_str) < 1)
			show_usage("invalid list: empty");

		if (!opt_fields && opt_delim_not_default)
			show_usage("-d can only be used with -f");

		if (opt_no_split_chars)
			errx(EXIT_FAILURE, "-n is not supported");

		range_list = parse_list(list_str);
	}

	for (int i = 0; range_list[i]; i++)
	{
		printf("%02d: %4d to %4d\n", i, range_list[i]->from, range_list[i]->to);
	}
	fflush(stdout);
}
