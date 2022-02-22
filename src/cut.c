#define _XOPEN_SOURCE 700

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

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
			"       cut -f list [-d delim] [-s] [file...]\n");
	exit(EXIT_FAILURE);
}

inline static int min(const int a, const int b)
{
	return a < b ? a : b;
}

static int opt_bytes = 0;
static int opt_chars = 0;
static int opt_fields = 0;
static int opt_no_split_chars = 0;
static int opt_hide_unmatched = 0;
static int opt_delim_not_default = 0;

static char delim = '\t';

static void cut_one_file_lines(FILE *fp, struct range **ranges, const char *filename)
{
	char buf[BUFSIZ];
	char printed[BUFSIZ];
	char *line;
	int lines_read = 0;

	while ((line = fgets(buf, BUFSIZ, fp)) != NULL)
	{
		int op = 0;
		int len = strlen(line);
		if (buf[len-1] != '\n')
			errx(EXIT_FAILURE, "buffer overflow: %s: line %d", filename, lines_read);
		/* skip back from newline and erase it */
		buf[--len] = '\0';
		lines_read++;
		char *end = buf + len;

		memset(printed, 0, len);
		struct range *rg;

		/* iterate over each range in the list */
		for (int i = 0; (rg = ranges[i]); i++)
		{

			/* for bytes and chars */
			if (opt_bytes || opt_chars ) 
			{
				int end = min(ranges[i]->to, len);
				for (int pos = ranges[i]->from-1; pos < end; pos++) 
				{
					if(printed[pos]) continue;
					printed[pos] = 1;
					putchar(buf[pos]);
					op++;
				}
			} 
			/* for delimiter based */
			else if (opt_fields) 
			{
				/* short circuit if no delim on the line */
				if (!strchr(line, delim))
					break;

				int skip = -1;
				int found = 0;

				/* scan for unprinted ranges */
				for (int i = rg->from-1; i<rg->to; i++)
					if(!printed[i]) { skip = i; break; }

				/* entire line already printed, don't bother */
				if (skip == -1)
					continue;

				/* start at the beginning of the line */
				char *first = line;

				/* scan over each elemtent of the range x-y */
				for (int j = 0; j<rg->to; j++)
				{
					/* have we ran out of line or trokens? */
					if (!first || first+1 >= end)
					{
						break;
					/* skip tokens before x in range:x-y */
					} 
					else if (j < rg->from-1) 
					{
						first = strchr(first+1, delim);
						continue;
					}
					/* skip over the token itself */
					else if (*first == ':') 
					{
						first++;
					}

					/* bump the number of found, needed, tokens and 
					 * calculate the byte offset */
					int first_off = first-line;
					found++;

					/* find the next token (x-y) or skip to the end (x-) */
					char *next;
					if (rg->to == INT_MAX)
						next = end;
					else
						next = strchr(first+1, delim);

					/* calculate the offset, if there is one */
					int next_off = next ? next-line : len;

					/* save the string length between first and next offsets */
					int plen = next_off - first_off;

					/* set the bytes as already printed */
					memset(&printed[first_off], 1, plen);
					
					/* and print them */
					fwrite(first, 1, plen, stdout);

					/* record the fact we printed something for \n */
					op++;

					/* shuffle along */
					first = next;
				}
			}
		}

		if (op) { 
			putchar('\n');
		}
		else if (opt_fields && !opt_hide_unmatched)
		{
			puts(line);
		}
	}

	if (feof(fp))
		return;

	err(EXIT_FAILURE, "%s", filename);
}

static int count_range(struct range **lst)
{
	int i;
	for (i = 0; lst[i]; i++) ;
	return i;
}

static int rangesort(const void *a, const void *b)
{
	struct range *r1 = *(struct range **)a;
	struct range *r2 = *(struct range **)b;

	int rc;

	if (r1->from == r2->from) {
		rc = r1->to - r2->to;
	} else {
		rc = r1->from - r2->from;
	}

	return rc;
}

static struct range **parse_list(const char *string)
{
	struct range **list = NULL;
	int num_entries = 0;
	struct range *current = NULL;
	char *ptr = (char *)string;

	const int entry_l = sizeof(struct range *);

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
				current->from = 0;
			/* number- */
			else if (current->to == -1)
				current->to = INT_MAX;

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

	/* sort the entries in the list */
	int list_len = count_range(list);
	qsort(list, list_len, sizeof(struct range *), rangesort);

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

	if (optind >= argc) 
	{
		cut_one_file_lines(stdin, range_list, "<stdin>");
	} else 
	{
		for (int i = optind; i < argc; i++)
		{
			if (strcmp(argv[i], "-"))
			{
				FILE *file = NULL;
				if ((file = fopen(argv[i], "r")) == NULL)
					err(EXIT_FAILURE, "%s", argv[i]);
				cut_one_file_lines(file, range_list, argv[i]);
				fclose(file);
			} 
			else 
			{
				cut_one_file_lines(stdin, range_list, "<stdin>");
			}
					
		}
	}

	exit(EXIT_SUCCESS);
}
