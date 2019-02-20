#define _XOPEN_SOURCE 700

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <strings.h>
#include <regex.h>

static void show_usage()
{
	fprintf(stderr,
			"Usage: grep [-E|-F] [-c|-l|-q] [-insvx] -e pattern_lists [-e pattern_file]... \n"
			"              [-f pattern_file]... [file...]\n"
			"       grep [-E|-F] [-c|-l|-q] [-insvx] [-e pattern_lists]...\n"
			"              -f pattern_file... [-f pattern_file]... [file...]\n"
			"       grep [-E|-F] [-c|-l|-q] [-insvx] pattern_lists [file...]\n"
		   );
	exit(EXIT_FAILURE);
}

static int opt_ere = 0;
static int opt_strings = 0;
static int opt_write_count = 0;
static int opt_case_insensitive = 0;
static int opt_write_filenames = 0;
static int opt_write_lineno = 0;
static int opt_quiet = 0;
static int opt_supress_enoent = 0;
static int opt_not_matching = 0;
static int opt_match_entire_line = 0;

static int total_files = 1;

static int do_grep(char **patterns, char *file)
{
	FILE *fp = NULL;
	int rc = 0;
	int lineno = 0;
	int fn_written = 0;
	int total_match = 0;
	int close_fd = 0;

	if (file == NULL) {
		fp = stdin;
	} else if ((fp = fopen(file, "r")) == NULL) {
		if (!opt_supress_enoent)
			warn("%s", file);
		return EXIT_FAILURE;
	} else
		close_fd = 1;

	while (1)
	{
		if (feof(fp)) break;
		if (ferror(fp)) { rc = 2; break; }

		char buf[BUFSIZ];
		char *line;

		if ((line = fgets(buf, BUFSIZ, fp)) == NULL)
			continue;

		lineno++;
		int match = 0;
		char *pattern;

		for (int i = 0; patterns[i]; i++)
		{
			pattern = patterns[i];

			if (opt_strings) {
				if (opt_match_entire_line && !opt_case_insensitive) match = !strcasecmp(line, pattern);
				else if (opt_match_entire_line && opt_case_insensitive) match = !strcmp(line, pattern);
				else if (!opt_match_entire_line && !opt_case_insensitive) match = (strstr(line, pattern) != NULL);
				else if (!opt_match_entire_line && opt_case_insensitive) errx(EXIT_FAILURE, "no strcasestr");
			} else {
				errx(EXIT_FAILURE, "no RE or ERE");
			}
		}

		if ( (!match && opt_not_matching) || (match && !opt_not_matching) ) {
			total_match++;
			if (!opt_quiet) {
				if (opt_write_filenames) {
					if (!fn_written) {
						fn_written = 1;
						puts(file);
					}
				} else if (opt_write_count) {
				} else {
					if (opt_write_lineno) 
						printf("%d:", lineno);
					fprintf(stdout, "%s", line);
				}

				if (feof(stdout) || ferror(stdout))
					errx(EXIT_FAILURE, "problem with stdout");
			}
		}
	}

	if (!opt_quiet) {
		if (!opt_write_filenames && opt_write_count) {
			if (total_files > 1)
				printf("%s:", file);
			printf("%d\n", total_match);
		}

		if (feof(stdout) || ferror(stdout))
			errx(EXIT_FAILURE, "problem with stdout");
	}

	if (close_fd)
		fclose(fp);

	if (!rc)
		rc = total_match ? 0 : 1;

	return rc;
}

inline static int max(const int a, const int b)
{
	return a > b ? a : b;
}

inline static char **add_to_list(char **list, int *cnt, char *string)
{
	char **ret = NULL;

	if ((ret = realloc(list, sizeof(char *) * (*cnt + 1))) == NULL)
		err(EXIT_FAILURE, NULL);
	
	if (string) {
		if ((ret[*cnt++] = strdup(string)) == NULL) {
			err(EXIT_FAILURE, NULL);
		}
	} else
		ret[*cnt] = NULL;

	return ret;
}

int main(int argc, char *argv[])
{
	char **pattern_lists = NULL;
	char **pattern_files = NULL;
	int list_count = 0;
	int file_count = 0;

	/* get options */
	{
		int opt;

		while ((opt = getopt(argc, argv, "EFclqinsvxe:f:")) != -1)
		{
			switch (opt)
			{
				case 'E':
					opt_ere = 1;
					break;
				case 'F':
					opt_strings = 1;
					break;
				case 'c':
					opt_write_count = 1;
					break;
				case 'e':
					pattern_lists = add_to_list(pattern_lists, &list_count, optarg);
					break;
				case 'f':
					pattern_files = add_to_list(pattern_files, &file_count, optarg);
					break;
				case 'i':
					opt_case_insensitive = 1;
					break;
				case 'l':
					opt_write_filenames = 1;
					break;
				case 'n':
					opt_write_lineno = 1;
					break;
				case 'q':
					opt_quiet = 1;
					break;
				case 's':
					opt_supress_enoent = 1;
					break;
				case 'v':
					opt_not_matching = 1;
					break;
				case 'x':
					opt_match_entire_line = 1;
					break;
				default:
					show_usage();
			}
		}
	}

	/* terminate the pattern_list and pattern_file lists from 
	 * usage form 1 and 2 */
	pattern_lists = add_to_list(pattern_lists, &list_count, NULL);
	pattern_files = add_to_list(pattern_files, &file_count, NULL);

	if (opt_ere + opt_strings > 1)
		show_usage();

	/* check for third usage form, with a single pattern_list argument */
	if (file_count + list_count == 0) {
		if (optind >= argc) {
			show_usage();
		} else {
			pattern_lists = add_to_list(pattern_lists, &list_count, argv[optind++]);
		}
	}

	for (int i = 0; pattern_files[i]; i++)
	{
		FILE *f;
		if ((f = fopen(pattern_files[i], "r")) == NULL)
			err(EXIT_FAILURE, "%s", pattern_files[i]);

		// TODO: read all pattern_files into pattern_lists
		
		fclose(f);
	}

	/* default to no lines selected */
	int rc = 1;

	/* interate over filenames, or use stdin if none provided */
	if (argc - optind > 0) {
		rc = 0;
		total_files = (argc - optind);

		for (int i = optind; i < argc; i++) 
		{
			/* if we have an error (>1) don't hide it with no lines (1)
			 * or lines (0) */
			rc = max(rc, do_grep(pattern_lists, argv[i]));
		}
	} else {
		rc = do_grep(pattern_lists, NULL);
	}

	exit(rc);
}
